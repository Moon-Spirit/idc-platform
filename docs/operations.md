# IDC Platform — Operations Guide

> **Version:** 1.0.0  
> **Last Updated:** 2026-07-06

---

## Table of Contents

1. [Daily Operations Checklist](#1-daily-operations-checklist)
2. [Common Troubleshooting Scenarios](#2-common-troubleshooting-scenarios)
3. [Restart / Upgrade Procedures](#3-restart--upgrade-procedures)
4. [Emergency Contacts & Escalation](#4-emergency-contacts--escalation)
5. [Appendix: Key System Paths](#5-appendix-key-system-paths)

---

## 1. Daily Operations Checklist

### 1.1 Morning Check (First login of the day)

```bash
# 1. Verify all services are running
systemctl status idc-platform
systemctl status postgresql
systemctl status redis-server
systemctl status nginx
systemctl status certbot.timer

# 2. Check disk usage
df -h / /var/lib/postgresql/16/main /var/log

# 3. Check memory
free -h

# 4. Check backend recent errors
journalctl -u idc-platform --since "24 hours ago" -p err

# 5. Check nginx error rate
tail -100 /var/log/nginx/access.log | awk '{print $9}' | sort | uniq -c | sort -rn

# 6. Check database connections
sudo -u postgres psql -c "SELECT count(*) FROM pg_stat_activity WHERE state = 'active';"

# 7. Verify backup ran successfully
ls -lh /backup/idc_platform_$(date +%Y-%m-%d)*.dump 2>/dev/null || echo "Backup file not found for today"
```

### 1.2 Weekly Checks

```bash
# 1. Review logs for anomalies
journalctl -u idc-platform --since "7 days ago" -p warning

# 2. Check backup integrity
pg_restore -l /backup/idc_platform_latest.dump | head -20

# 3. Check SSL certificate expiry
sudo openssl x509 -enddate -noout -in /etc/letsencrypt/live/idc-platform.example.com/fullchain.pem

# 4. Review system updates
apt list --upgradable

# 5. Check Redis memory usage
redis-cli -a "$REDIS_PASSWORD" INFO memory | grep used_memory_human

# 6. Verify TimescaleDB compression
sudo -u postgres psql -d idc_platform -c "
SELECT hypertable_name, chunk_name, range_start, range_end, is_compressed
FROM timescaledb_information.chunks
WHERE hypertable_name = 'bandwidth_samples'
ORDER BY range_start DESC LIMIT 5;"
```

### 1.3 Monthly Checks

```bash
# 1. Review billing runs
curl -s http://localhost:8080/api/v1/billing/status \
  -H "Authorization: Bearer $(get_admin_token)" | jq .

# 2. Verify WAL archiving is working
ls -lh /var/lib/postgresql/16/wal_archive/ | wc -l

# 3. Perform a test restore to verify backups
sudo -u postgres pg_restore -d idc_platform_test /backup/idc_platform_latest.dump

# 4. Check for slow queries
sudo -u postgres psql -d idc_platform -c "
SELECT query, calls, total_exec_time, mean_exec_time
FROM pg_stat_statements
ORDER BY mean_exec_time DESC LIMIT 10;"

# 5. Audit user access
sudo -u postgres psql -d idc_platform -c "
SELECT u.username, r.name as role, u.last_login_at
FROM users u JOIN roles r ON u.role_id = r.id
WHERE u.status = 1
ORDER BY u.last_login_at DESC NULLS LAST;"

# 6. Review failed login attempts
sudo -u postgres psql -d idc_platform -c "
SELECT username, COUNT(*) as attempts
FROM operation_logs
WHERE action = 'login_failed'
  AND created_at > NOW() - INTERVAL '30 days'
GROUP BY username
ORDER BY attempts DESC;"
```

---

## 2. Common Troubleshooting Scenarios

### 2.1 Backend Won't Start

**Symptoms:** `systemctl start idc-platform` fails or immediately exits.

**Checklist:**

```bash
# 1. Check journal for error messages
journalctl -u idc-platform -n 50 --no-pager

# 2. Verify config.json is valid JSON
python3 -m json.tool /opt/idc-platform/config/config.json

# 3. Check database is accessible
sudo -u postgres psql -d idc_platform -c "SELECT 1;"

# 4. Check Redis is accessible
redis-cli -a "$REDIS_PASSWORD" ping

# 5. Verify binary exists and is executable
ls -la /opt/idc-platform/bin/idc-platform
file /opt/idc-platform/bin/idc-platform

# 6. Check port 8080 is not already in use
ss -tlnp | grep 8080
```

### 2.2 Database Connection Errors

**Symptoms:** Backend logs show `connection refused` or `FATAL: password authentication failed`.

```bash
# 1. Verify PostgreSQL is running
systemctl status postgresql

# 2. Check pg_hba.conf allows local connections
grep -v '^#' /etc/postgresql/16/main/pg_hba.conf | grep -v '^$'

# 3. Test connection manually
PGPASSWORD="$DB_PASSWORD" psql -h localhost -U idcapp -d idc_platform -c "SELECT 1;"

# 4. Check connection pool limits
sudo -u postgres psql -c "SHOW max_connections;"
sudo -u postgres psql -c "SELECT count(*) FROM pg_stat_activity;"
```

### 2.3 Redis Connection Errors

```bash
# 1. Verify Redis is running
systemctl status redis-server

# 2. Test connection with password
redis-cli -a "$REDIS_PASSWORD" ping

# 3. Check Redis binds to localhost
grep '^bind' /etc/redis/redis.conf

# 4. Verify password is set correctly
grep '^requirepass' /etc/redis/redis.conf
```

### 2.4 High Memory / CPU Usage

```bash
# 1. Identify the process
top -b -n1 | head -20

# 2. Check backend threads
ps -eLf | grep idc-platform | wc -l

# 3. Review slow queries
sudo -u postgres psql -d idc_platform -c "
SELECT pid, now() - pg_stat_activity.query_start AS duration, query
FROM pg_stat_activity
WHERE state = 'active'
  AND query NOT LIKE '%pg_stat%'
ORDER BY duration DESC;"

# 4. Check nginx connections
nginx -V 2>&1 | grep -o 'worker_connections=\d\+'

# 5. Check if log files are growing too large
du -sh /var/log/idc-platform/
```

### 2.5 Disk Space Low

```bash
# 1. Find large directories
du -sh /*/* 2>/dev/null | sort -rh | head -20

# 2. Check PostgreSQL data size
sudo -u postgres psql -d idc_platform -c "
SELECT pg_size_pretty(pg_database_size('idc_platform'));"

# 3. Check WAL archive size
du -sh /var/lib/postgresql/16/wal_archive/

# 4. Remove old WAL archives (if backed up)
find /var/lib/postgresql/16/wal_archive/ -name "*.gz" -mtime +7 -delete

# 5. Clean old backups
find /backup/ -name "*.dump" -mtime +30 -delete

# 6. Truncate oversized logs (if logrotate failed)
sudo truncate -s 0 /var/log/idc-platform/idc*.log
```

### 2.6 SSL Certificate Issues

```bash
# 1. Check certificate expiry
sudo openssl x509 -enddate -noout -in /etc/letsencrypt/live/idc-platform.example.com/fullchain.pem

# 2. Force renewal
sudo certbot renew --force-renewal

# 3. Verify nginx config after renewal
sudo nginx -t && sudo systemctl reload nginx
```

### 2.7 API Returns 502 Bad Gateway

```bash
# 1. Check if backend is running
systemctl status idc-platform

# 2. Check backend port
ss -tlnp | grep 8080

# 3. Check nginx error log
tail -50 /var/log/nginx/error.log | grep 'connect\|upstream'

# 4. Verify proxy_pass address in nginx config matches backend
grep proxy_pass /etc/nginx/sites-available/idc-platform

# 5. Check firewall
sudo ufw status
sudo iptables -L -n | grep ':80\|:443'
```

### 2.8 Users Cannot Login

```bash
# 1. Try direct API call
curl -s -X POST http://localhost:8080/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin123"}' | jq .

# 2. Check if user is disabled
sudo -u postgres psql -d idc_platform -c "
SELECT username, status, last_login_at
FROM users WHERE username = 'admin';"

# 3. Check operation logs for failed attempts
sudo -u postgres psql -d idc_platform -c "
SELECT * FROM operation_logs
WHERE action = 'login_failed'
ORDER BY created_at DESC LIMIT 10;"
```

### 2.9 Billing Not Running

```bash
# 1. Check billing status
curl -s http://localhost:8080/api/v1/billing/status \
  -H "Authorization: Bearer $(get_admin_token)" | jq .

# 2. Check cron logs
journalctl -u idc-platform --since "24 hours ago" | grep -i billing

# 3. Manually trigger billing
curl -s -X POST http://localhost:8080/api/v1/billing/run \
  -H "Authorization: Bearer $(get_admin_token)" \
  -H 'Content-Type: application/json' \
  -d '{"cycle":"monthly"}' | jq .
```

### 2.10 Invoice Generation Failed

```bash
# 1. Check Gotenberg (PDF) service
curl -s http://localhost:3000/health 2>/dev/null || echo "Gotenberg not accessible"

# 2. Check backend logs for PDF errors
journalctl -u idc-platform --since "1 hour ago" | grep -i "pdf\|gotenberg\|invoice"

# 3. Manually trigger invoice generation
curl -s -X POST http://localhost:8080/api/v1/invoices/generate \
  -H "Authorization: Bearer $(get_admin_token)" \
  -H 'Content-Type: application/json' \
  -d '{"period_start":"2026-07-01","period_end":"2026-07-31"}' | jq .
```

---

## 3. Restart / Upgrade Procedures

### 3.1 Graceful Restart (No Downtime)

The Drogon backend supports graceful reload for some config changes:

```bash
# Graceful reload (sends SIGHUP)
sudo systemctl reload idc-platform

# If reload not supported, use restart with health check loop:
sudo systemctl restart idc-platform && \
  sleep 2 && \
  curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/api/v1/auth/me \
    -H "Authorization: Bearer $(get_admin_token)" | grep -q 200 || \
  echo "Restart may have failed, check logs"
```

### 3.2 Full Upgrade Procedure

> ⚠️ Schedule upgrades during maintenance window. Notify users in advance.

**Step 1: Prepare new binary**

```bash
# On build machine or CI
cd /opt/idc-platform

# Pull latest code
git pull origin main

# Build backend
mkdir -p build && cd build
cmake ../backend -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel "$(nproc)"

# Build frontend
cd ../frontend
npm ci && npm run build
```

**Step 2: Deploy new binary**

```bash
# Stop service
sudo systemctl stop idc-platform

# Backup current binary
sudo cp /opt/idc-platform/bin/idc-platform /opt/idc-platform/bin/idc-platform.bak

# Copy new binary
sudo cp /opt/idc-platform/build/idc-platform /opt/idc-platform/bin/

# Copy frontend
sudo cp -r /opt/idc-platform/frontend/dist/* /opt/idc-platform/frontend/

# Apply any new database migrations
for f in /opt/idc-platform/backend/migrations/*.sql; do
  echo "Applying migration: $f"
  sudo -u postgres psql -d idc_platform -f "$f"
done

# Start service
sudo systemctl start idc-platform

# Verify health
sleep 2
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/api/v1/auth/me \
  -H "Authorization: Bearer $(get_admin_token)" | grep -q 200 && \
  echo "Upgrade successful" || echo "Upgrade may have failed"
```

**Step 3: Rollback (if needed)**

```bash
sudo systemctl stop idc-platform
sudo cp /opt/idc-platform/bin/idc-platform.bak /opt/idc-platform/bin/idc-platform
sudo systemctl start idc-platform
```

### 3.3 Database Upgrade

```bash
# 1. Take a full backup before migration
pg_dump -Fc -U postgres idc_platform > /backup/pre_migration_$(date +%Y%m%d_%H%M%S).dump

# 2. Apply migration
sudo -u postgres psql -d idc_platform -f /opt/idc-platform/backend/migrations/NNN_description.sql

# 3. Verify
sudo -u postgres psql -d idc_platform -c "\dt"
```

### 3.4 Operating System Upgrade

```bash
# 1. Check for updates
apt list --upgradable

# 2. Schedule maintenance window

# 3. Take full backup before OS upgrade
sudo -u postgres pg_dumpall -f /backup/pre_os_upgrade.sql

# 4. Apply updates
sudo apt update
sudo apt upgrade -y

# 5. Reboot if kernel updated
sudo reboot

# 6. Verify all services after reboot
systemctl status idc-platform postgresql redis-server nginx
```

---

## 4. Emergency Contacts & Escalation

### 4.1 Incident Severity Levels

| Level | Definition | Response Time | Example |
|-------|------------|---------------|---------|
| **P0** | Service down — all users affected | 15 minutes | Backend crash, DB down |
| **P1** | Major feature unavailable | 1 hour | Login broken, billing failed |
| **P2** | Partial functionality degraded | 4 hours | Slow response, intermittent errors |
| **P3** | Cosmetic / minor issue | 24 hours | UI typo, non-critical bug |

### 4.2 Escalation Flow

```
User reports issue
       │
       ▼
Level 1: On-call Operator
  • Verify the issue
  • Check runbook (this document)
  • Attempt common fixes
  • Escalate if unresolved after 30 min
       │
       ▼
Level 2: System Administrator
  • Access server
  • Review logs and metrics
  • Apply hotfix if identified
  • Escalate if infrastructure issue
       │
       ▼
Level 3: Development Team
  • Root cause analysis
  • Code fix / configuration change
  • Deploy fix through normal pipeline
```

### 4.3 Contact List

| Role | Contact | Notes |
|------|---------|-------|
| On-call Operator | _(fill in)_ | First line, available 24/7 |
| System Admin | _(fill in)_ | Server access, config changes |
| Backend Developer | _(fill in)_ | Drogon/C++ expertise |
| Frontend Developer | _(fill in)_ | Vue.js expertise |
| DBA | _(fill in)_ | PostgreSQL/TimescaleDB |
| Security Officer | _(fill in)_ | Security incidents |

### 4.4 Incident Reporting Template

```
────────────────────────────────────────
INCIDENT REPORT
────────────────────────────────────────
Date:        YYYY-MM-DD
Time:        HH:MM UTC
Severity:    P0/P1/P2/P3
Reported by: (name)

DESCRIPTION:
[What happened]

IMPACT:
[Users affected, features impacted]

TIMELINE:
- HH:MM — Issue detected
- HH:MM — Investigation started
- HH:MM — Root cause identified
- HH:MM — Mitigation applied
- HH:MM — Service restored

ROOT CAUSE:
[Why it happened]

RESOLUTION:
[What was done to fix it]

PREVENTION:
[What will prevent recurrence]

────────────────────────────────────────
```

---

## 5. Appendix: Key System Paths

| Item | Path |
|------|------|
| Backend binary | `/opt/idc-platform/bin/idc-platform` |
| Backend config | `/opt/idc-platform/config/config.json` |
| Backend logs | `/var/log/idc-platform/` |
| Frontend (static) | `/opt/idc-platform/frontend/` |
| systemd service | `/etc/systemd/system/idc-platform.service` |
| Nginx config | `/etc/nginx/sites-available/idc-platform` |
| Nginx logs | `/var/log/nginx/` |
| DB data dir | `/var/lib/postgresql/16/main` |
| WAL archive | `/var/lib/postgresql/16/wal_archive` |
| Redis data | `/var/lib/redis/` |
| Backups | `/backup/` |
| SSL certs | `/etc/letsencrypt/live/idc-platform.example.com/` |
| Environment file | `/opt/idc-platform/.env.production` |
| Migration scripts | `/opt/idc-platform/backend/migrations/` |
