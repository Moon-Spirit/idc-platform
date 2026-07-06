#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════════
# IDC Platform — One-Click Deploy Script
# ═══════════════════════════════════════════════════════════════════════════════
# Usage:
#   ./scripts/deploy.sh [options]
#
# Options:
#   -h, --help       Show this help message
#   -e, --env FILE   Path to .env.production file (default: /opt/idc-platform/.env.production)
#   -s, --source DIR Path to build artifacts directory (default: /opt/idc-platform)
#   -b, --backend-only  Deploy backend only (skip frontend/migrations)
#   -f, --frontend-only Deploy frontend only (skip backend/migrations)
#   -m, --migrate-only  Run migrations only (skip binary/frontend)
#   --skip-backup    Skip pre-deploy database backup
#   --dry-run        Show what would be done without making changes
#
# Prerequisites:
#   - Run as root or with sudo
#   - Build artifacts must be present in SOURCE_DIR
#   - Database must exist and be accessible
#
# This script is idempotent — safe to re-run.
# ═══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ── Config ─────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default paths (configurable via options)
ENV_FILE="/opt/idc-platform/.env.production"
SOURCE_DIR="/opt/idc-platform"
BACKEND_DIR="$SOURCE_DIR"
FRONTEND_DIR="$SOURCE_DIR/frontend"
MIGRATIONS_DIR="$SOURCE_DIR/backend/migrations"
BIN_DIR="/opt/idc-platform/bin"
FRONTEND_TARGET="/opt/idc-platform/frontend"
SYSTEMD_SERVICE="idc-platform.service"
NGINX_SITE="idc-platform"

# Overrides
DEPLOY_BACKEND=true
DEPLOY_FRONTEND=true
RUN_MIGRATIONS=true
SKIP_BACKUP=false
DRY_RUN=false

# ── Colors ─────────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# ── Functions ──────────────────────────────────────────────────────────────────

usage() {
    sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \?//'
    exit 0
}

run() {
    if [ "$DRY_RUN" = true ]; then
        echo -e "${YELLOW}[DRY-RUN]${NC} $*"
    else
        "$@"
    fi
}

check_prerequisites() {
    info "Checking prerequisites..."

    # Must be root
    if [ "$(id -u)" -ne 0 ]; then
        err "This script must be run as root (use sudo)."
        exit 1
    fi

    # Check required commands
    local cmds=("systemctl" "curl" "psql" "rsync")
    for cmd in "${cmds[@]}"; do
        if ! command -v "$cmd" &>/dev/null; then
            err "Required command not found: $cmd"
            exit 1
        fi
    done

    # Check paths exist
    if [ "$DEPLOY_BACKEND" = true ]; then
        if [ ! -f "$SOURCE_DIR/build/idc-platform" ] && [ ! -f "$SOURCE_DIR/idc-platform" ]; then
            warn "Backend binary not found at $SOURCE_DIR/build/idc-platform or $SOURCE_DIR/idc-platform"
            warn "Will build from source if CMakeLists.txt exists..."
        fi
    fi

    if [ "$DEPLOY_FRONTEND" = true ]; then
        if [ ! -f "$SOURCE_DIR/frontend/dist/index.html" ]; then
            warn "Frontend dist not found at $SOURCE_DIR/frontend/dist/"
            warn "Will build frontend if package.json exists..."
        fi
    fi

    ok "Prerequisites check passed"
}

pre_deploy_backup() {
    if [ "$SKIP_BACKUP" = true ]; then
        info "Skipping pre-deploy backup (--skip-backup)"
        return
    fi

    info "Taking pre-deploy database backup..."
    local backup_dir="/backup"
    run mkdir -p "$backup_dir"

    local timestamp
    timestamp="$(date +%Y%m%d_%H%M%S)"
    local backup_file="$backup_dir/idc_platform_pre_deploy_$timestamp.dump"

    if run sudo -u postgres pg_dump -Fc idc_platform > "$backup_file"; then
        ok "Pre-deploy backup saved: $backup_file ($(du -h "$backup_file" | cut -f1))"
    else
        warn "Pre-deploy backup failed! Continuing anyway..."
    fi
}

build_backend() {
    info "Building backend from source..."

    local build_dir="$SOURCE_DIR/build"
    run mkdir -p "$build_dir"

    if [ -f "$SOURCE_DIR/backend/CMakeLists.txt" ]; then
        run cmake -S "$SOURCE_DIR/backend" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
        run cmake --build "$build_dir" --parallel "$(nproc)"
        ok "Backend build complete"
    else
        err "No CMakeLists.txt found at $SOURCE_DIR/backend/"
        exit 1
    fi
}

build_frontend() {
    info "Building frontend..."

    if [ -f "$SOURCE_DIR/frontend/package.json" ]; then
        run cd "$SOURCE_DIR/frontend"
        run npm ci
        run npm run build
        run cd "$SOURCE_DIR"
        ok "Frontend build complete"
    else
        err "No package.json found at $SOURCE_DIR/frontend/"
        exit 1
    fi
}

deploy_backend() {
    info "Deploying backend..."

    # Ensure target directories exist
    run mkdir -p "$BIN_DIR" "$FRONTEND_TARGET"

    # Stop service before replacing binary
    if systemctl is-active --quiet "$SYSTEMD_SERVICE" 2>/dev/null; then
        info "Stopping $SYSTEMD_SERVICE..."
        run systemctl stop "$SYSTEMD_SERVICE"
    fi

    # Backup existing binary
    if [ -f "$BIN_DIR/idc-platform" ]; then
        run cp "$BIN_DIR/idc-platform" "$BIN_DIR/idc-platform.bak"
        ok "Existing binary backed up to idc-platform.bak"
    fi

    # Copy new binary
    local binary_source=""
    if [ -f "$SOURCE_DIR/build/idc-platform" ]; then
        binary_source="$SOURCE_DIR/build/idc-platform"
    elif [ -f "$SOURCE_DIR/idc-platform" ]; then
        binary_source="$SOURCE_DIR/idc-platform"
    elif [ -f "$SOURCE_DIR/backend/build/idc-platform" ]; then
        binary_source="$SOURCE_DIR/backend/build/idc-platform"
    fi

    if [ -n "$binary_source" ]; then
        run cp "$binary_source" "$BIN_DIR/idc-platform"
        run chmod +x "$BIN_DIR/idc-platform"
        run chown idcapp:idcapp "$BIN_DIR/idc-platform"
        ok "Binary deployed: $BIN_DIR/idc-platform"
    else
        warn "No binary found to deploy. Building..."
        build_backend
        if [ -f "$SOURCE_DIR/build/idc-platform" ]; then
            run cp "$SOURCE_DIR/build/idc-platform" "$BIN_DIR/idc-platform"
            run chmod +x "$BIN_DIR/idc-platform"
            run chown idcapp:idcapp "$BIN_DIR/idc-platform"
            ok "Binary deployed from build"
        fi
    fi

    # Copy config.json if present
    if [ -f "$SOURCE_DIR/config.json" ]; then
        run cp "$SOURCE_DIR/config.json" "/opt/idc-platform/config/config.json"
        run chown idcapp:idcapp "/opt/idc-platform/config/config.json"
        ok "Config file deployed"
    elif [ -f "$SOURCE_DIR/backend/config.json" ]; then
        run cp "$SOURCE_DIR/backend/config.json" "/opt/idc-platform/config/config.json"
        run chown idcapp:idcapp "/opt/idc-platform/config/config.json"
        ok "Config file deployed (from backend/)"
    fi

    ok "Backend deployment complete"
}

deploy_frontend() {
    info "Deploying frontend..."

    run mkdir -p "$FRONTEND_TARGET"

    local frontend_source="$SOURCE_DIR/frontend/dist"
    if [ ! -d "$frontend_source" ]; then
        warn "No dist found. Building frontend..."
        build_frontend
    fi

    if [ -d "$frontend_source" ]; then
        # Use rsync for efficient copy
        run rsync -a --delete "$frontend_source/" "$FRONTEND_TARGET/"
        run chown -R idcapp:idcapp "$FRONTEND_TARGET"
        ok "Frontend deployed to $FRONTEND_TARGET"
    else
        err "Frontend dist not found at $frontend_source"
        exit 1
    fi
}

run_migrations() {
    info "Running database migrations..."

    if [ ! -d "$MIGRATIONS_DIR" ]; then
        warn "Migrations directory not found at $MIGRATIONS_DIR"
        MIGRATIONS_DIR="$SOURCE_DIR/backend/migrations"
        if [ ! -d "$MIGRATIONS_DIR" ]; then
            err "Cannot find migrations directory"
            exit 1
        fi
    fi

    # Check database connectivity
    if ! sudo -u postgres psql -d idc_platform -c "SELECT 1;" &>/dev/null; then
        err "Cannot connect to database idc_platform"
        exit 1
    fi

    # Apply each migration in order
    for migration in "$MIGRATIONS_DIR"/*.sql; do
        [ -f "$migration" ] || continue
        local name
        name="$(basename "$migration")"

        # Skip README
        [ "$name" = "README.md" ] && continue

        info "Applying migration: $name"
        if run sudo -u postgres psql -d idc_platform -f "$migration"; then
            ok "Migration applied: $name"
        else
            err "Migration failed: $name"
            exit 1
        fi
    done

    ok "All migrations applied"
}

restart_services() {
    info "Restarting services..."

    # Start backend
    run systemctl daemon-reload
    run systemctl enable "$SYSTEMD_SERVICE" 2>/dev/null
    run systemctl start "$SYSTEMD_SERVICE"

    # Wait for backend to become healthy
    local max_retries=12
    local retry=0
    local healthy=false

    info "Waiting for backend to become healthy..."
    while [ $retry -lt $max_retries ]; do
        if curl -sf -o /dev/null http://localhost:8080/ 2>/dev/null || \
           curl -sf -o /dev/null http://127.0.0.1:8080/api/v1/auth/login \
             -X POST -H 'Content-Type: application/json' \
             -d '{"username":"admin","password":"admin123"}' 2>/dev/null; then
            healthy=true
            break
        fi
        sleep 2
        retry=$((retry + 1))
    done

    if [ "$healthy" = true ]; then
        ok "Backend is healthy"
    else
        warn "Backend health check did not pass within $((max_retries * 2)) seconds"
        warn "Check logs: journalctl -u $SYSTEMD_SERVICE -n 50"
    fi

    # Reload nginx
    if systemctl is-active --quiet nginx; then
        run nginx -t && run systemctl reload nginx || warn "Nginx reload failed"
        ok "Nginx reloaded"
    else
        warn "Nginx is not running — start it manually"
    fi
}

post_deploy_verify() {
    info "Running post-deploy verification..."

    local failures=0

    # 1. Check backend process
    if pgrep -x "idc-platform" >/dev/null; then
        ok "Backend process is running"
    else
        warn "Backend process not found"
        failures=$((failures + 1))
    fi

    # 2. Check port 8080
    if ss -tlnp | grep -q ':8080'; then
        ok "Backend listening on port 8080"
    else
        warn "Backend not listening on port 8080"
        failures=$((failures + 1))
    fi

    # 3. Check port 80/443
    if ss -tlnp | grep -qE ':80\b|:443\b'; then
        ok "Nginx listening on port 80/443"
    else
        warn "Nginx not listening on port 80/443"
        failures=$((failures + 1))
    fi

    # 4. Check frontend files exist
    if [ -f "$FRONTEND_TARGET/index.html" ]; then
        ok "Frontend files present"
    else
        warn "Frontend index.html not found"
        failures=$((failures + 1))
    fi

    # 5. Test API
    local login_response
    login_response=$(curl -s -X POST http://localhost:8080/api/v1/auth/login \
        -H 'Content-Type: application/json' \
        -d '{"username":"admin","password":"admin123"}' 2>/dev/null) || true

    if echo "$login_response" | grep -q '"code":0'; then
        ok "API login endpoint works"
    else
        warn "API login test failed"
        failures=$((failures + 1))
    fi

    if [ $failures -eq 0 ]; then
        ok "All post-deploy checks passed!"
    else
        warn "$failures post-deploy check(s) failed — review warnings"
    fi
}

cleanup_old_backups() {
    info "Cleaning up old backup binaries..."
    # Keep only the last 3 binary backups
    ls -t "$BIN_DIR"/idc-platform.bak* 2>/dev/null | tail -n +4 | while read -r old; do
        run rm -f "$old"
    done
}

# ── Main ───────────────────────────────────────────────────────────────────────

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage ;;
        -e|--env) ENV_FILE="$2"; shift 2 ;;
        -s|--source) SOURCE_DIR="$2"; shift 2 ;;
        -b|--backend-only)
            DEPLOY_FRONTEND=false
            RUN_MIGRATIONS=false
            shift ;;
        -f|--frontend-only)
            DEPLOY_BACKEND=false
            RUN_MIGRATIONS=false
            shift ;;
        -m|--migrate-only)
            DEPLOY_BACKEND=false
            DEPLOY_FRONTEND=false
            shift ;;
        --skip-backup) SKIP_BACKUP=true; shift ;;
        --dry-run) DRY_RUN=true; shift ;;
        *) err "Unknown option: $1"; usage ;;
    esac
done

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║       IDC Platform — Deployment Script                      ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

echo "  Source:      $SOURCE_DIR"
echo "  Env file:    $ENV_FILE"
echo "  Backend:     $([ "$DEPLOY_BACKEND" = true ] && echo 'yes' || echo 'no')"
echo "  Frontend:    $([ "$DEPLOY_FRONTEND" = true ] && echo 'yes' || echo 'no')"
echo "  Migrations:  $([ "$RUN_MIGRATIONS" = true ] && echo 'yes' || echo 'no')"
echo "  Pre-backup:  $([ "$SKIP_BACKUP" = false ] && echo 'yes' || echo 'no')"
echo "  Dry run:     $([ "$DRY_RUN" = true ] && echo 'yes' || echo 'no')"
echo ""

# Main execution
check_prerequisites

[ "$SKIP_BACKUP" = false ] && pre_deploy_backup

[ "$DEPLOY_BACKEND" = true ] && deploy_backend
[ "$DEPLOY_FRONTEND" = true ] && deploy_frontend
[ "$RUN_MIGRATIONS" = true ] && run_migrations

restart_services
post_deploy_verify
cleanup_old_backups

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║       Deployment Complete                                   ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
info "If you use .env.production for secrets, verify: $ENV_FILE"
info "Check service status: sudo systemctl status idc-platform"
info "View logs: sudo journalctl -u idc-platform -f"
