# IDC Platform — Production Deployment Guide

> **Target:** Ubuntu 22.04 LTS  
> **Stack:** Drogon C++17 (backend) + Vue 3 (frontend) + PostgreSQL 16 + Redis 7  
> **Version:** 1.0.0

---

## Table of Contents

1. [Environment Requirements](#1-environment-requirements)
2. [Configuration Reference](#2-configuration-reference)
3. [Step-by-Step Deployment](#3-step-by-step-deployment)
   - [3.1 System Preparation](#31-system-preparation)
   - [3.2 Database Setup](#32-database-setup)
   - [3.3 Backend Build & Install](#33-backend-build--install)
   - [3.4 Frontend Build](#34-frontend-build)
   - [3.5 Database Migration](#35-database-migration)
   - [3.6 Nginx Configuration](#36-nginx-configuration)
   - [3.7 systemd Service](#37-systemd-service)
   - [3.8 Health Check](#38-health-check)
4. [Log Management](#4-log-management)
5. [Backup Strategy](#5-backup-strategy)
6. [Monitoring Recommendations](#6-monitoring-recommendations)

---

## 1. Environment Requirements

### Hardware Minimums

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| CPU | 2 cores | 4+ cores |
| RAM | 4 GB | 8+ GB |
| Disk | 40 GB SSD | 100 GB+ SSD |
| Network | 100 Mbps | 1 Gbps |

### Software Dependencies

```bash
# Ubuntu 22.04 LTS
OS: Ubuntu 22.04.5 LTS (Jammy Jellyfish)

# Database
PostgreSQL 16 + TimescaleDB 2.x
Redis 7.x

# Build Toolchain
cmake >= 3.20
g++ >= 11 (with C++17 support)

# Runtime
Node.js 22.x (for frontend build only)
Nginx 1.24+
```

### Required System Packages

```bash
# Core dependencies
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libssl-dev \
    libpq-dev \
    libhiredis-dev \
    uuid-dev \
    zlib1g-dev \
    nginx \
    postgresql-16 \
    postgresql-16-timescaledb \
    redis-server \
    certbot \
    python3-certbot-nginx

# For frontend build (on deploy machine or CI)
curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
sudo apt install -y nodejs
```

---

## 2. Configuration Reference

### 2.1 Backend: `config.json`

Located at `/opt/idc-platform/config.json`. Full reference:

```json
{
    "app": {
        "number_of_threads": 4,
        "log": {
            "log_path": "/var/log/idc-platform",
            "logfile_base_name": "idc",
            "log_level": "info"
        },
        "client_max_body_size": "16M",
        "client_max_memory_body_size": "64K",
        "keep_alive_timeout": 120
    },
    "db_clients": [
        {
            "name": "idc_db",
            "rdbms": "postgresql",
            "host": "localhost",
            "port": 5432,
            "dbname": "idc_platform",
            "user": "idcapp",
            "password": "${DB_PASSWORD}",
            "connection_number": 10,
            "timeout": 5.0
        }
    ],
    "jwt": {
        "secret": "${JWT_SECRET}",
        "expires_in": 86400
    },
    "redis_clients": [
        {
            "name": "idc_redis",
            "rdbms": "redis",
            "host": "localhost",
            "port": 6379,
            "db": 0,
            "password": "${REDIS_PASSWORD}",
            "connection_number": 4,
            "timeout": 3.0
        }
    ],
    "zjmf": {
        "mock": false,
        "rate_limit": {
            "requests_per_minute": 30,
            "burst": 30
        },
        "http_client": {
            "connect_timeout_sec": 5,
            "read_timeout_sec": 30,
            "max_retries": 3
        }
    }
}
```

> ⚠️ Always replace `${DB_PASSWORD}`, `${JWT_SECRET}`, and `${REDIS_PASSWORD}` with real secrets in production. Use `openssl rand -base64 32` to generate secrets.

### 2.2 Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `DB_PASSWORD` | PostgreSQL password | _(required)_ |
| `JWT_SECRET` | JWT signing secret | _(required)_ |
| `REDIS_PASSWORD` | Redis password | _(required)_ |
| `APP_THREADS` | Number of worker threads | `4` |
| `LOG_LEVEL` | Log level (trace/debug/info/warn/error) | `info` |
| `ZJMF_MOCK` | Enable ZJMF mock mode for testing | `false` |

### 2.3 Frontend: `frontend/.env.production`

```env
VITE_API_BASE_URL=/api/v1
VITE_APP_TITLE=IDC 分销平台
```

### 2.4 Ports

| Service | Port | Protocol |
|---------|------|----------|
| Backend API | 8080 | TCP (internal) |
| PostgreSQL | 5432 | TCP (internal) |
| Redis | 6379 | TCP (internal) |
| Nginx HTTP | 80 | TCP (public) |
| Nginx HTTPS | 443 | TCP (public) |

---

## 3. Step-by-Step Deployment

### 3.1 System Preparation

```bash
# Create application user
sudo useradd -r -s /bin/bash -m -d /opt/idc-platform idcapp

# Create directory structure
sudo mkdir -p /opt/idc-platform/{bin,config,logs,frontend}
sudo mkdir -p /var/log/idc-platform
sudo chown -R idcapp:idcapp /opt/idc-platform /var/log/idc-platform

# Tune system limits for production
cat << 'EOF' | sudo tee /etc/security/limits.d/90-idc-platform.conf
idcapp   soft    nofile    65536
idcapp   hard    nofile    65536
idcapp   soft    nproc     4096
idcapp   hard    nproc     4096
EOF

# Enable PostgreSQL and Redis on boot
sudo systemctl enable postgresql
sudo systemctl enable redis-server

# Configure Redis with password
sudo sed -i 's/# requirepass foobared/requirepass '"$(openssl rand -base64 32)"'/' /etc/redis/redis.conf
sudo systemctl restart redis-server
```

### 3.2 Database Setup

```bash
# Connect as postgres superuser
sudo -u postgres psql

-- Create the database
CREATE DATABASE idc_platform;

-- Create application user with strong password
CREATE USER idcapp WITH PASSWORD 'change_this_to_a_strong_password';

-- Grant privileges
GRANT CONNECT ON DATABASE idc_platform TO idcapp;
\c idc_platform
GRANT USAGE ON SCHEMA public TO idcapp;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO idcapp;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO idcapp;

-- Enable TimescaleDB in the database (requires extension installation)
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- Exit psql
\q
```

### 3.3 Backend Build & Install

```bash
# Clone or copy source
cd /opt/idc-platform
# (scp or git clone your source here)

# Build as idcapp user
sudo -u idcapp bash

# Build backend
cd /opt/idc-platform
mkdir -p build && cd build
cmake ../backend -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel "$(nproc)"

# Copy binary
cp idc-platform /opt/idc-platform/bin/

# Copy production config
cp /opt/idc-platform/backend/config.json /opt/idc-platform/config/
# Edit config.json with production values (see section 2.1)
exit
```

### 3.4 Frontend Build

```bash
# Build frontend (can be done on CI or deploy machine)
cd /opt/idc-platform/frontend
npm ci
npm run build

# Ensure dist is in the correct location
ls frontend/dist/  # Should contain index.html, assets/, etc.

# Copy to nginx serve directory
sudo cp -r frontend/dist/* /opt/idc-platform/frontend/
```

### 3.5 Database Migration

```bash
# Apply migrations in order
cd /opt/idc-platform/backend/migrations

# Migration 001: Initial schema + seed data
sudo -u postgres psql -d idc_platform -f 001_init.sql

# Apply subsequent migrations
for f in *.sql; do
    echo "Applying migration: $f"
    sudo -u postgres psql -d idc_platform -f "$f"
done
```

### 3.6 Nginx Configuration

Create `/etc/nginx/sites-available/idc-platform`:

```nginx
# ═══════════════════════════════════════════════════════════════════════════════
# IDC Platform — Production Nginx Configuration
# ═══════════════════════════════════════════════════════════════════════════════

# Redirect HTTP → HTTPS
server {
    listen 80;
    server_name idc-platform.example.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl http2;
    server_name idc-platform.example.com;

    # SSL — Let's Encrypt (see section 3.6.1)
    ssl_certificate     /etc/letsencrypt/live/idc-platform.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/idc-platform.example.com/privkey.pem;
    ssl_protocols       TLSv1.2 TLSv1.3;
    ssl_ciphers         HIGH:!aNULL:!MD5;
    ssl_prefer_server_ciphers on;

    # Security headers
    add_header X-Frame-Options DENY;
    add_header X-Content-Type-Options nosniff;
    add_header X-XSS-Protection "1; mode=block";
    add_header Strict-Transport-Security "max-age=31536000; includeSubDomains" always;
    add_header Referrer-Policy strict-origin-when-cross-origin;

    # Root directory — Vue.js SPA built output
    root /opt/idc-platform/frontend;
    index index.html;

    # Gzip
    gzip on;
    gzip_types text/plain text/css application/json application/javascript
               text/xml application/xml text/javascript image/svg+xml;
    gzip_min_length 256;
    gzip_vary on;

    # ── SPA fallback (Vue Router history mode) ──────────────────────────────
    location / {
        try_files $uri $uri/ /index.html;
    }

    # ── API reverse proxy ───────────────────────────────────────────────────
    location /api/ {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host                   $host;
        proxy_set_header X-Real-IP              $remote_addr;
        proxy_set_header X-Forwarded-For        $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto      $scheme;

        # Timeouts
        proxy_connect_timeout 10s;
        proxy_read_timeout 120s;
        proxy_send_timeout 120s;

        # Request body size (must match backend config)
        client_max_body_size 16M;
    }

    # ── WebSocket proxy ─────────────────────────────────────────────────────
    location /ws/ {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Upgrade                 $http_upgrade;
        proxy_set_header Connection              "upgrade";
        proxy_set_header Host                    $host;
        proxy_set_header X-Real-IP               $remote_addr;
        proxy_set_header X-Forwarded-For         $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto       $scheme;

        # Long-lived WebSocket connections (24h)
        proxy_read_timeout  86400s;
        proxy_send_timeout  86400s;
    }

    # ── Static assets with long-term caching ────────────────────────────────
    location /assets/ {
        expires    1y;
        add_header Cache-Control "public, immutable";
        access_log off;
    }

    # ── Health check endpoint (passed to backend) ──────────────────────────
    location /health {
        proxy_pass http://127.0.0.1:8080/api/v1/auth/me;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        access_log off;
    }

    # ── Deny access to hidden files ─────────────────────────────────────────
    location ~ /\. {
        deny all;
        access_log off;
        log_not_found off;
    }
}
```

#### 3.6.1 SSL with Let's Encrypt

```bash
# Obtain certificate
sudo certbot --nginx -d idc-platform.example.com --non-interactive --agree-tos -m admin@example.com

# Auto-renewal (certbot adds systemd timer automatically)
sudo certbot renew --dry-run

# Verify the timer
sudo systemctl status certbot.timer
```

#### 3.6.2 Enable the site

```bash
sudo ln -sf /etc/nginx/sites-available/idc-platform /etc/nginx/sites-enabled/
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t
sudo systemctl reload nginx
```

### 3.7 systemd Service

Create `/etc/systemd/system/idc-platform.service`:

```ini
[Unit]
Description=IDC Platform Backend API Server
Documentation=https://github.com/your-org/idc-platform
After=network.target postgresql.service redis-server.service
Wants=postgresql.service redis-server.service

[Service]
Type=simple
User=idcapp
Group=idcapp

# Working directory
WorkingDirectory=/opt/idc-platform

# Binary and config
ExecStart=/opt/idc-platform/bin/idc-platform
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=5

# File descriptors
LimitNOFILE=65536
LimitNPROC=4096

# Security hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=full
ReadWritePaths=/opt/idc-platform /var/log/idc-platform

# Logging (stdout → journald)
StandardOutput=journal
StandardError=journal

# Environment
Environment=DB_PASSWORD=your_db_password
Environment=JWT_SECRET=your_jwt_secret
Environment=REDIS_PASSWORD=your_redis_password

[Install]
WantedBy=multi-user.target
```

> ⚠️ Never hardcode secrets in the service file in production. Use a systemd EnvironmentFile or a secrets manager instead.

**Alternative: Use EnvironmentFile** (recommended):

```ini
[Service]
EnvironmentFile=/opt/idc-platform/.env.production
```

Then populate `/opt/idc-platform/.env.production`:

```env
DB_PASSWORD=your_actual_db_password
JWT_SECRET=your_actual_jwt_secret
REDIS_PASSWORD=your_actual_redis_password
LOG_LEVEL=info
APP_THREADS=4
```

**Enable and start:**

```bash
sudo systemctl daemon-reload
sudo systemctl enable idc-platform
sudo systemctl start idc-platform
sudo systemctl status idc-platform
```

---

### 3.8 Health Check Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Nginx-level health (proxies to backend auth) |
| `/api/v1/auth/me` | GET | Backend live check (requires valid JWT) |
| `GET /api/v1/billing/status` | GET | Checks DB connectivity |

**Health check command:**

```bash
# Basic health (HTTP 200 = alive)
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/api/v1/auth/me \
  -H "Authorization: Bearer $(curl -s -X POST http://localhost:8080/api/v1/auth/login \
    -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"admin123"}' | jq -r '.data.token')"
```

**Load balancer health check configuration:**

```nginx
# In your load balancer (HAProxy / AWS ALB / Nginx Plus)
# Check: GET /health
# Interval: 10s
# Fall: 3
# Rise: 2
```

---

## 4. Log Management

### Log Locations

| Component | Path | Format |
|-----------|------|--------|
| Backend API | `/var/log/idc-platform/idc*.log` | Text (Drogon) |
| Nginx access | `/var/log/nginx/access.log` | Combined |
| Nginx error | `/var/log/nginx/error.log` | Error |
| PostgreSQL | `/var/log/postgresql/postgresql-16-main.log` | CSVLOG |
| Redis | `/var/log/redis/redis-server.log` | Text |
| systemd (backend) | `journalctl -u idc-platform` | Journal |

### Log Rotation

Backend logs are managed by Drogon's built-in rotation (see config.json).  
System logs use `logrotate`. Create `/etc/logrotate.d/idc-platform`:

```
/var/log/idc-platform/*.log {
    daily
    rotate 30
    compress
    delaycompress
    missingok
    notifempty
    copytruncate
    dateext
    dateformat -%Y%m%d
}

/var/log/nginx/*.log {
    daily
    rotate 30
    compress
    delaycompress
    missingok
    notifempty
    sharedscripts
    postrotate
        [ -f /var/run/nginx.pid ] && kill -USR1 `cat /var/run/nginx.pid`
    endscript
}
```

---

## 5. Backup Strategy

### 5.1 Daily pg_dump

See [scripts/backup.sh](../scripts/backup.sh) for automated backup script.

Backup schedule via cron:

```bash
# Daily at 3:00 AM
0 3 * * * /opt/idc-platform/scripts/backup.sh
```

### 5.2 WAL Archiving (Point-in-Time Recovery)

Configure PostgreSQL WAL archiving in `/etc/postgresql/16/main/postgresql.conf`:

```ini
wal_level = replica
archive_mode = on
archive_command = 'cp %p /var/lib/postgresql/16/wal_archive/%f && gzip -f /var/lib/postgresql/16/wal_archive/%f'
archive_timeout = 60
```

Create the archive directory:

```bash
sudo mkdir -p /var/lib/postgresql/16/wal_archive
sudo chown -R postgres:postgres /var/lib/postgresql/16/wal_archive
sudo systemctl restart postgresql
```

### 5.3 Retention Policy

| Backup Type | Retention | Storage |
|-------------|-----------|---------|
| Daily full dump | 30 days | Local + S3 |
| WAL archives | 7 days | Local only |
| Monthly snapshot | 12 months | S3/remote |

### 5.4 Restoration Test

```bash
# Restore a backup to verify integrity
pg_restore -d idc_platform_test /backup/idc_platform_2026-07-06.dump
# Check that data is intact
psql -d idc_platform_test -c "SELECT count(*) FROM users;"
```

---

## 6. Monitoring Recommendations

### 6.1 Essential Metrics

| Category | Metrics | Tool |
|----------|---------|------|
| System | CPU, RAM, disk, network | Prometheus Node Exporter |
| Backend | Request rate, latency (p50/p95/p99), error rate | Prometheus + Grafana |
| Database | Connections, query time, cache hit ratio | pg_stat_statements |
| Redis | Memory, hit rate, connected clients | Redis INFO |
| Nginx | Requests/sec, 4xx/5xx rates | Nginx stub_status |
| SSL | Certificate expiry | certbot renew --dry-run |

### 6.2 Prometheus + Grafana (Quick Setup)

```bash
# Install Prometheus Node Exporter
sudo apt install -y prometheus-node-exporter

# Install Prometheus
sudo apt install -y prometheus

# Install Grafana
sudo apt install -y software-properties-common
sudo add-apt-repository "deb https://packages.grafana.com/oss/deb stable main"
wget -q -O- https://packages.grafana.com/gpg.key | sudo apt-key add -
sudo apt update && sudo apt install -y grafana
sudo systemctl enable grafana-server
sudo systemctl start grafana-server
```

### 6.3 Alerting Rules (Prometheus)

```yaml
groups:
  - name: idc-platform
    rules:
      - alert: BackendDown
        expr: up{job="idc-platform"} == 0
        for: 1m
        annotations:
          summary: "Backend API is down"

      - alert: HighErrorRate
        expr: rate(http_requests_total{status=~"5.."}[5m]) > 0.05
        for: 5m
        annotations:
          summary: "Error rate > 5% over 5 minutes"

      - alert: DBDiskLow
        expr: (node_filesystem_free_bytes{mountpoint="/var/lib/postgresql"} /
               node_filesystem_size_bytes{mountpoint="/var/lib/postgresql"}) < 0.2
        for: 5m
        annotations:
          summary: "PostgreSQL disk space below 20%"

      - alert: SSLCertExpiring
        expr: (cert_expiry_days < 30)
        annotations:
          summary: "SSL certificate expires in {{ $value }} days"
```

### 6.4 Uptime Monitoring

- [UptimeRobot](https://uptimerobot.com) — Free tier monitors `/health` every 5 minutes
- [Checkly](https://checklyhq.com) — Advanced browser + API check monitoring

---

## Appendix A: Quick Reference Commands

```bash
# Service management
sudo systemctl start idc-platform
sudo systemctl stop idc-platform
sudo systemctl restart idc-platform
sudo systemctl status idc-platform
sudo journalctl -u idc-platform -f

# Nginx
sudo nginx -t
sudo systemctl reload nginx

# Database
sudo -u postgres psql -d idc_platform
sudo systemctl restart postgresql

# Redis
redis-cli -a "$REDIS_PASSWORD" ping
sudo systemctl restart redis-server

# Logs
tail -f /var/log/idc-platform/idc*.log
tail -f /var/log/nginx/access.log
```

## Appendix B: Configuration Checklist

- [ ] PostgreSQL password changed from default
- [ ] Redis password set
- [ ] JWT secret generated (`openssl rand -base64 32`)
- [ ] config.json host changed from `postgres` → `localhost` and `redis` → `localhost`
- [ ] config.json `mock` set to `false` (unless testing)
- [ ] Nginx SSL certificate obtained and configured
- [ ] systemd service file installed and enabled
- [ ] Log rotation configured
- [ ] Backup script installed and cron job added
- [ ] Firewall ports restricted (only 80/443 public)
- [ ] Fail2ban installed for SSH protection
- [ ] Automatic security updates enabled
