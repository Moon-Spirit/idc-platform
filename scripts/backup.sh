#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════════
# IDC Platform — Database Backup Script
# ═══════════════════════════════════════════════════════════════════════════════
# Usage:
#   ./scripts/backup.sh [options]
#
# Options:
#   -h, --help           Show this help message
#   -d, --db NAME        Database name (default: idc_platform)
#   -o, --output DIR     Backup output directory (default: /backup)
#   -r, --remote URL     Remote sync destination (e.g. s3://bucket/backups)
#   -k, --keep N         Number of local backups to keep (default: 30)
#   --no-s3              Disable S3 sync even if configured
#   --no-remote          Disable remote sync even if configured
#   --pre-deploy         Mark backup as pre-deploy (adds note to log)
#
# This script performs:
#   1. pg_dump with custom format (compressed, parallel-capable)
#   2. Optional WAL file cleanup (old WALs removed)
#   3. Optional remote sync (S3 via aws-cli or rsync over SSH)
#   4. Cleanup of old backups (keeps last N)
#   5. Backup integrity verification
#
# Idempotent: safe to run multiple times.
# ═══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ── Config ─────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Defaults
DB_NAME="idc_platform"
BACKUP_DIR="/backup"
REMOTE_URL=""
KEEP_BACKUPS=30
PRE_DEPLOY=false
NO_S3=false
NO_REMOTE=false

# Load optional config file
CONFIG_FILE="$SCRIPT_DIR/backup.conf"
if [ -f "$CONFIG_FILE" ]; then
    # shellcheck source=/dev/null
    source "$CONFIG_FILE"
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# ── Functions ──────────────────────────────────────────────────────────────────

usage() {
    sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \?//'
    exit 0
}

check_dependencies() {
    local missing=false
    for cmd in pg_dump pg_isready gzip find du date; do
        if ! command -v "$cmd" &>/dev/null; then
            err "Required command not found: $cmd"
            missing=true
        fi
    done

    # Optional: aws for S3, openssl for checksums
    if [ -n "$REMOTE_URL" ] && [ "$NO_S3" = false ] && [ "$NO_REMOTE" = false ]; then
        if echo "$REMOTE_URL" | grep -qE '^s3://'; then
            if ! command -v aws &>/dev/null; then
                warn "aws CLI not found — S3 sync will be skipped"
                NO_S3=true
            fi
        fi
        if echo "$REMOTE_URL" | grep -qE '^ssh://'; then
            if ! (command -v rsync &>/dev/null && command -v ssh &>/dev/null); then
                warn "rsync/ssh not found — remote SSH sync will be skipped"
                NO_REMOTE=true
            fi
        fi
    fi

    if [ "$missing" = true ]; then
        exit 1
    fi
}

verify_db_connection() {
    info "Verifying PostgreSQL connection..."

    if ! pg_isready -q -d "$DB_NAME" 2>/dev/null && \
       ! pg_isready -q 2>/dev/null; then
        err "PostgreSQL is not running or not accessible"
        exit 1
    fi

    # Test we can connect
    if ! psql -d "$DB_NAME" -c "SELECT 1;" &>/dev/null; then
        err "Cannot connect to database '$DB_NAME' as current user"
        err "Try: sudo -u postgres $0"
        exit 1
    fi

    ok "Database connection verified: $DB_NAME"
}

perform_backup() {
    local timestamp
    timestamp="$(date +%Y%m%d_%H%M%S)"
    local date_str
    date_str="$(date +%Y-%m-%d)"

    local backup_label="idc_platform_${date_str}"
    local backup_file="${BACKUP_DIR}/${backup_label}.dump"
    local checksum_file="${backup_file}.sha256"

    info "Starting backup: $backup_file"

    # Create backup directory
    mkdir -p "$BACKUP_DIR"

    # Remove previous backup for the same day (for idempotency)
    rm -f "$backup_file" "$checksum_file"

    # Perform the dump
    # -Fc: custom format (compressed, flexible restore)
    # -Z9: maximum compression
    # --no-owner: don't include ownership (safer for cross-environment restore)
    # --no-privileges: skip privilege restoration (handled separately)
    info "Running pg_dump (custom format, compressed)..."
    if pg_dump -Fc \
        --no-owner \
        --no-privileges \
        --dbname="$DB_NAME" \
        --file="$backup_file" \
        --verbose 2>&1 | while IFS= read -r line; do
            if [[ "$line" =~ ^pg_dump:\ (warning|error) ]]; then
                warn "  $line"
            fi
        done; then
        local file_size
        file_size=$(du -h "$backup_file" | cut -f1)
        ok "Backup completed: $file_size"
    else
        err "Backup failed!"
        rm -f "$backup_file"
        exit 1
    fi

    # Generate checksum
    info "Generating checksum..."
    sha256sum "$backup_file" > "$checksum_file"
    ok "Checksum saved: $(cut -d' ' -f1 < "$checksum_file")"

    # Create a symlink to the latest backup
    local latest_link="${BACKUP_DIR}/idc_platform_latest.dump"
    ln -sf "$backup_file" "$latest_link"

    # Record backup metadata
    local meta_file="${backup_file}.meta"
    cat > "$meta_file" << EOF
backup_file: $(basename "$backup_file")
database: $DB_NAME
timestamp: $timestamp
date: $date_str
size: $file_size
checksum: $(cut -d' ' -f1 < "$checksum_file")
pg_version: $(psql -d "$DB_NAME" -t -c "SELECT version();" | head -1 | xargs)
pre_deploy: $PRE_DEPLOY
EOF
    ok "Backup metadata saved"
}

verify_backup() {
    info "Verifying backup integrity..."

    local latest_link="${BACKUP_DIR}/idc_platform_latest.dump"
    if [ ! -f "$latest_link" ]; then
        warn "Latest backup symlink not found, trying direct file"
        return
    fi

    local backup_file
    backup_file="$(readlink -f "$latest_link")"

    # Verify checksum
    local checksum_file="${backup_file}.sha256"
    if [ -f "$checksum_file" ]; then
        if cd "$BACKUP_DIR" && sha256sum -c "$checksum_file" &>/dev/null; then
            ok "Checksum verification passed"
        else
            err "Checksum verification FAILED — backup may be corrupted!"
            return 1
        fi
    fi

    # Verify dump can be read (list contents)
    info "Verifying dump can be read..."
    if pg_restore -l "$backup_file" > /dev/null 2>&1; then
        local table_count
        table_count=$(pg_restore -l "$backup_file" 2>/dev/null | grep -c 'TABLE ' || true)
        ok "Dump readable: $table_count tables found"
    else
        warn "pg_restore -l could not read the dump — file may be corrupt!"
    fi
}

cleanup_old_backups() {
    info "Cleaning up old backups (keeping last $KEEP_BACKUPS)..."

    # Count existing full backups (not incremental, not wal-only)
    local backup_count
    backup_count=$(find "$BACKUP_DIR" -name "idc_platform_*.dump" -maxdepth 1 2>/dev/null | wc -l)

    if [ "$backup_count" -le "$KEEP_BACKUPS" ]; then
        info "Only $backup_count backups exist (limit: $KEEP_BACKUPS), no cleanup needed"
        return
    fi

    # Remove oldest backups beyond the keep limit
    find "$BACKUP_DIR" -name "idc_platform_*.dump" -maxdepth 1 \
        -printf '%T@ %p\n' | sort -n | head -n -"$KEEP_BACKUPS" | \
        while read -r timestamp filepath; do
            local base="${filepath%.dump}"
            rm -f "$filepath" "${base}.sha256" "${base}.meta"
            ok "Removed old backup: $(basename "$filepath")"
        done

    ok "Cleanup complete"
}

sync_to_remote() {
    if [ -z "$REMOTE_URL" ] || [ "$NO_REMOTE" = true ]; then
        return
    fi

    local latest_link="${BACKUP_DIR}/idc_platform_latest.dump"
    local backup_file
    backup_file="$(readlink -f "$latest_link")"

    info "Syncing backup to remote: $REMOTE_URL"

    if echo "$REMOTE_URL" | grep -qE '^s3://'; then
        if [ "$NO_S3" = false ] && command -v aws &>/dev/null; then
            run aws s3 cp "$backup_file" "${REMOTE_URL%/}/" \
                --storage-class STANDARD_IA 2>&1 || \
                warn "S3 sync failed"
            run aws s3 cp "${backup_file}.sha256" "${REMOTE_URL%/}/" 2>&1 || \
                warn "S3 checksum sync failed"
            ok "Synced to S3: $REMOTE_URL"
        fi
    elif echo "$REMOTE_URL" | grep -qE '^ssh://'; then
        # Format: ssh://user@host:/path
        local ssh_target="${REMOTE_URL#ssh://}"
        if command -v rsync &>/dev/null; then
            rsync -avz --progress "$backup_file" "${ssh_target}/" 2>&1 || \
                warn "Remote rsync failed"
            ok "Synced via SSH: $REMOTE_URL"
        fi
    else
        # Treat as local path
        local remote_dir="$REMOTE_URL"
        mkdir -p "$remote_dir"
        cp "$backup_file" "$remote_dir/" || warn "Copy to $remote_dir failed"
        cp "${backup_file}.sha256" "$remote_dir/" || true
        ok "Copied to local path: $remote_dir"
    fi
}

log_backup_event() {
    local log_file="${BACKUP_DIR}/backup_history.log"
    local date_str
    date_str="$(date +%Y-%m-%d\ %H:%M:%S)"
    local latest_link="${BACKUP_DIR}/idc_platform_latest.dump"
    local backup_file
    backup_file="$(readlink -f "$latest_link" 2>/dev/null || echo 'unknown')"
    local file_size
    file_size=$(du -h "$backup_file" 2>/dev/null | cut -f1 || echo 'unknown')

    echo "$date_str | $backup_file | $file_size | pre_deploy=$PRE_DEPLOY" >> "$log_file"
    ok "Backup event logged"
}

# ── Main ───────────────────────────────────────────────────────────────────────

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage ;;
        -d|--db) DB_NAME="$2"; shift 2 ;;
        -o|--output) BACKUP_DIR="$2"; shift 2 ;;
        -r|--remote) REMOTE_URL="$2"; shift 2 ;;
        -k|--keep) KEEP_BACKUPS="$2"; shift 2 ;;
        --no-s3) NO_S3=true; shift ;;
        --no-remote) NO_REMOTE=true; shift ;;
        --pre-deploy) PRE_DEPLOY=true; shift ;;
        *) err "Unknown option: $1"; usage ;;
    esac
done

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║       IDC Platform — Database Backup                        ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "  Database:    $DB_NAME"
echo "  Output:      $BACKUP_DIR"
echo "  Remote:      ${REMOTE_URL:-(none)}"
echo "  Keep:        $KEEP_BACKUPS backups"
echo "  Pre-deploy:  $PRE_DEPLOY"
echo ""

check_dependencies
verify_db_connection
perform_backup
verify_backup
sync_to_remote
cleanup_old_backups
log_backup_event

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║       Backup Complete                                       ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
ok "Backup saved to: $(readlink -f "${BACKUP_DIR}/idc_platform_latest.dump" 2>/dev/null || echo 'see above')"
info "To restore: pg_restore -d idc_platform --clean $(readlink -f "${BACKUP_DIR}/idc_platform_latest.dump")"
