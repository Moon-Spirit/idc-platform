# IDC 平台 — 运维指南

> **版本：** 1.0.0  
> **最后更新：** 2026-07-06

---

## 目录

1. [每日运维检查清单](#1-daily-operations-checklist)
2. [常见故障排除场景](#2-common-troubleshooting-scenarios)
3. [重启/升级流程](#3-restart--upgrade-procedures)
4. [紧急联系人与升级流程](#4-emergency-contacts--escalation)
5. [附录：关键系统路径](#5-appendix-key-system-paths)

---

## 1. 每日运维检查清单

### 1.1 早间检查（每日首次登录）

```bash
# 1. 验证所有服务是否正常运行
systemctl status idc-platform
systemctl status postgresql
systemctl status redis-server
systemctl status nginx
systemctl status certbot.timer

# 2. 检查磁盘使用情况
df -h / /var/lib/postgresql/16/main /var/log

# 3. 检查内存
free -h

# 4. 检查后端近期错误
journalctl -u idc-platform --since "24 hours ago" -p err

# 5. 检查 nginx 错误率
tail -100 /var/log/nginx/access.log | awk '{print $9}' | sort | uniq -c | sort -rn

# 6. 检查数据库连接
sudo -u postgres psql -c "SELECT count(*) FROM pg_stat_activity WHERE state = 'active';"

# 7. 确认备份已成功运行
ls -lh /backup/idc_platform_$(date +%Y-%m-%d)*.dump 2>/dev/null || echo "Backup file not found for today"
```

### 1.2 每周检查

```bash
# 1. 审查日志异常
journalctl -u idc-platform --since "7 days ago" -p warning

# 2. 检查备份完整性
pg_restore -l /backup/idc_platform_latest.dump | head -20

# 3. 检查 SSL 证书有效期
sudo openssl x509 -enddate -noout -in /etc/letsencrypt/live/idc-platform.example.com/fullchain.pem

# 4. 检查系统更新
apt list --upgradable

# 5. 检查 Redis 内存使用情况
redis-cli -a "$REDIS_PASSWORD" INFO memory | grep used_memory_human

# 6. 验证 TimescaleDB 压缩
sudo -u postgres psql -d idc_platform -c "
SELECT hypertable_name, chunk_name, range_start, range_end, is_compressed
FROM timescaledb_information.chunks
WHERE hypertable_name = 'bandwidth_samples'
ORDER BY range_start DESC LIMIT 5;"
```

### 1.3 每月检查

```bash
# 1. 审查计费运行情况
curl -s http://localhost:8080/api/v1/billing/status \
  -H "Authorization: Bearer $(get_admin_token)" | jq .

# 2. 验证 WAL 归档是否正常运行
ls -lh /var/lib/postgresql/16/wal_archive/ | wc -l

# 3. 执行测试恢复以验证备份
sudo -u postgres pg_restore -d idc_platform_test /backup/idc_platform_latest.dump

# 4. 检查慢查询
sudo -u postgres psql -d idc_platform -c "
SELECT query, calls, total_exec_time, mean_exec_time
FROM pg_stat_statements
ORDER BY mean_exec_time DESC LIMIT 10;"

# 5. 审计用户访问
sudo -u postgres psql -d idc_platform -c "
SELECT u.username, r.name as role, u.last_login_at
FROM users u JOIN roles r ON u.role_id = r.id
WHERE u.status = 1
ORDER BY u.last_login_at DESC NULLS LAST;"

# 6. 审查失败的登录尝试
sudo -u postgres psql -d idc_platform -c "
SELECT username, COUNT(*) as attempts
FROM operation_logs
WHERE action = 'login_failed'
  AND created_at > NOW() - INTERVAL '30 days'
GROUP BY username
ORDER BY attempts DESC;"
```

---

## 2. 常见故障排除场景

### 2.1 后端无法启动

**症状：** `systemctl start idc-platform` 启动失败或立即退出。

**检查清单：**

```bash
# 1. 检查 journal 中的错误消息
journalctl -u idc-platform -n 50 --no-pager

# 2. 确认 config.json 是有效的 JSON
python3 -m json.tool /opt/idc-platform/config/config.json

# 3. 检查数据库是否可访问
sudo -u postgres psql -d idc_platform -c "SELECT 1;"

# 4. 检查 Redis 是否可访问
redis-cli -a "$REDIS_PASSWORD" ping

# 5. 确认二进制文件存在且可执行
ls -la /opt/idc-platform/bin/idc-platform
file /opt/idc-platform/bin/idc-platform

# 6. 检查端口 8080 是否已被占用
ss -tlnp | grep 8080
```

### 2.2 数据库连接错误

**症状：** 后端日志显示 `connection refused` 或 `FATAL: password authentication failed`。

```bash
# 1. 确认 PostgreSQL 是否正在运行
systemctl status postgresql

# 2. 检查 pg_hba.conf 是否允许本地连接
grep -v '^#' /etc/postgresql/16/main/pg_hba.conf | grep -v '^$'

# 3. 手动测试连接
PGPASSWORD="$DB_PASSWORD" psql -h localhost -U idcapp -d idc_platform -c "SELECT 1;"

# 4. 检查连接池限制
sudo -u postgres psql -c "SHOW max_connections;"
sudo -u postgres psql -c "SELECT count(*) FROM pg_stat_activity;"
```

### 2.3 Redis 连接错误

```bash
# 1. 确认 Redis 是否正在运行
systemctl status redis-server

# 2. 使用密码测试连接
redis-cli -a "$REDIS_PASSWORD" ping

# 3. 检查 Redis 是否绑定到 localhost
grep '^bind' /etc/redis/redis.conf

# 4. 确认密码设置正确
grep '^requirepass' /etc/redis/redis.conf
```

### 2.4 内存/CPU 使用率过高

```bash
# 1. 定位进程
top -b -n1 | head -20

# 2. 检查后端线程数
ps -eLf | grep idc-platform | wc -l

# 3. 审查慢查询
sudo -u postgres psql -d idc_platform -c "
SELECT pid, now() - pg_stat_activity.query_start AS duration, query
FROM pg_stat_activity
WHERE state = 'active'
  AND query NOT LIKE '%pg_stat%'
ORDER BY duration DESC;"

# 4. 检查 nginx 连接数
nginx -V 2>&1 | grep -o 'worker_connections=\d\+'

# 5. 检查日志文件是否增长过大
du -sh /var/log/idc-platform/
```

### 2.5 磁盘空间不足

```bash
# 1. 查找大目录
du -sh /*/* 2>/dev/null | sort -rh | head -20

# 2. 检查 PostgreSQL 数据大小
sudo -u postgres psql -d idc_platform -c "
SELECT pg_size_pretty(pg_database_size('idc_platform'));"

# 3. 检查 WAL 归档大小
du -sh /var/lib/postgresql/16/wal_archive/

# 4. 删除旧的 WAL 归档（如果已备份）
find /var/lib/postgresql/16/wal_archive/ -name "*.gz" -mtime +7 -delete

# 5. 清理旧备份
find /backup/ -name "*.dump" -mtime +30 -delete

# 6. 截断过大的日志（如果 logrotate 失败）
sudo truncate -s 0 /var/log/idc-platform/idc*.log
```

### 2.6 SSL 证书问题

```bash
# 1. 检查证书有效期
sudo openssl x509 -enddate -noout -in /etc/letsencrypt/live/idc-platform.example.com/fullchain.pem

# 2. 强制续期
sudo certbot renew --force-renewal

# 3. 续期后验证 nginx 配置
sudo nginx -t && sudo systemctl reload nginx
```

### 2.7 API 返回 502 Bad Gateway

```bash
# 1. 检查后端是否正在运行
systemctl status idc-platform

# 2. 检查后端端口
ss -tlnp | grep 8080

# 3. 检查 nginx 错误日志
tail -50 /var/log/nginx/error.log | grep 'connect\|upstream'

# 4. 确认 nginx 配置中的 proxy_pass 地址与后端匹配
grep proxy_pass /etc/nginx/sites-available/idc-platform

# 5. 检查防火墙
sudo ufw status
sudo iptables -L -n | grep ':80\|:443'
```

### 2.8 用户无法登录

```bash
# 1. 尝试直接 API 调用
curl -s -X POST http://localhost:8080/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin123"}' | jq .

# 2. 检查用户是否被禁用
sudo -u postgres psql -d idc_platform -c "
SELECT username, status, last_login_at
FROM users WHERE username = 'admin';"

# 3. 检查操作日志中的失败尝试
sudo -u postgres psql -d idc_platform -c "
SELECT * FROM operation_logs
WHERE action = 'login_failed'
ORDER BY created_at DESC LIMIT 10;"
```

### 2.9 计费未运行

```bash
# 1. 检查计费状态
curl -s http://localhost:8080/api/v1/billing/status \
  -H "Authorization: Bearer $(get_admin_token)" | jq .

# 2. 检查 cron 日志
journalctl -u idc-platform --since "24 hours ago" | grep -i billing

# 3. 手动触发计费
curl -s -X POST http://localhost:8080/api/v1/billing/run \
  -H "Authorization: Bearer $(get_admin_token)" \
  -H 'Content-Type: application/json' \
  -d '{"cycle":"monthly"}' | jq .
```

### 2.10 账单生成失败

```bash
# 1. 检查 Gotenberg（PDF）服务
curl -s http://localhost:3000/health 2>/dev/null || echo "Gotenberg not accessible"

# 2. 检查后端日志中的 PDF 错误
journalctl -u idc-platform --since "1 hour ago" | grep -i "pdf\|gotenberg\|invoice"

# 3. 手动触发账单生成
curl -s -X POST http://localhost:8080/api/v1/invoices/generate \
  -H "Authorization: Bearer $(get_admin_token)" \
  -H 'Content-Type: application/json' \
  -d '{"period_start":"2026-07-01","period_end":"2026-07-31"}' | jq .
```

---

## 3. 重启/升级流程

### 3.1 优雅重启（无停机）

Drogon 后端支持对部分配置变更进行优雅重载：

```bash
# 优雅重载（发送 SIGHUP 信号）
sudo systemctl reload idc-platform

# 如果不支持重载，则使用带健康检查循环的重启：
sudo systemctl restart idc-platform && \
  sleep 2 && \
  curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/api/v1/auth/me \
    -H "Authorization: Bearer $(get_admin_token)" | grep -q 200 || \
  echo "Restart may have failed, check logs"
```

### 3.2 完整升级流程

> ⚠️ 在维护窗口期间安排升级。提前通知用户。

**步骤 1：准备新二进制文件**

```bash
# 在构建机器或 CI 上
cd /opt/idc-platform

# 拉取最新代码
git pull origin main

# 构建后端
mkdir -p build && cd build
cmake ../backend -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel "$(nproc)"

# 构建前端
cd ../frontend
npm ci && npm run build
```

**步骤 2：部署新二进制文件**

```bash
# 停止服务
sudo systemctl stop idc-platform

# 备份当前二进制文件
sudo cp /opt/idc-platform/bin/idc-platform /opt/idc-platform/bin/idc-platform.bak

# 复制新二进制文件
sudo cp /opt/idc-platform/build/idc-platform /opt/idc-platform/bin/

# 复制前端文件
sudo cp -r /opt/idc-platform/frontend/dist/* /opt/idc-platform/frontend/

# 应用所有新的数据库迁移
for f in /opt/idc-platform/backend/migrations/*.sql; do
  echo "Applying migration: $f"
  sudo -u postgres psql -d idc_platform -f "$f"
done

# 启动服务
sudo systemctl start idc-platform

# 验证健康状态
sleep 2
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/api/v1/auth/me \
  -H "Authorization: Bearer $(get_admin_token)" | grep -q 200 && \
  echo "Upgrade successful" || echo "Upgrade may have failed"
```

**步骤 3：回滚（如有需要）**

```bash
sudo systemctl stop idc-platform
sudo cp /opt/idc-platform/bin/idc-platform.bak /opt/idc-platform/bin/idc-platform
sudo systemctl start idc-platform
```

### 3.3 数据库升级

```bash
# 1. 迁移前进行完整备份
pg_dump -Fc -U postgres idc_platform > /backup/pre_migration_$(date +%Y%m%d_%H%M%S).dump

# 2. 应用迁移
sudo -u postgres psql -d idc_platform -f /opt/idc-platform/backend/migrations/NNN_description.sql

# 3. 验证
sudo -u postgres psql -d idc_platform -c "\dt"
```

### 3.4 操作系统升级

```bash
# 1. 检查更新
apt list --upgradable

# 2. 安排维护窗口

# 3. 操作系统升级前进行完整备份
sudo -u postgres pg_dumpall -f /backup/pre_os_upgrade.sql

# 4. 应用更新
sudo apt update
sudo apt upgrade -y

# 5. 如果内核已更新则重启
sudo reboot

# 6. 重启后验证所有服务
systemctl status idc-platform postgresql redis-server nginx
```

---

## 4. 紧急联系人与升级流程

### 4.1 事件严重级别

| 级别 | 定义 | 响应时间 | 示例 |
|-------|------------|---------------|---------|
| **P0** | 服务宕机 — 所有用户受影响 | 15 分钟 | 后端崩溃，数据库宕机 |
| **P1** | 主要功能不可用 | 1 小时 | 登录故障，计费失败 |
| **P2** | 部分功能降级 | 4 小时 | 响应缓慢，间歇性错误 |
| **P3** | 界面/次要问题 | 24 小时 | 界面错别字，非关键性缺陷 |

### 4.2 升级流程

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

### 4.3 联系人列表

| 角色 | 联系人 | 备注 |
|------|---------|-------|
| 值班运维人员 | （填写） | 第一线，7×24 小时可用 |
| 系统管理员 | （填写） | 服务器访问，配置变更 |
| 后端开发人员 | （填写） | Drogon/C++ 专家 |
| 前端开发人员 | （填写） | Vue.js 专家 |
| 数据库管理员 | （填写） | PostgreSQL/TimescaleDB |
| 安全负责人 | （填写） | 安全事件 |

### 4.4 事件报告模板

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

## 5. 附录：关键系统路径

| 项目 | 路径 |
|------|------|
| 后端二进制文件 | `/opt/idc-platform/bin/idc-platform` |
| 后端配置 | `/opt/idc-platform/config/config.json` |
| 后端日志 | `/var/log/idc-platform/` |
| 前端（静态文件） | `/opt/idc-platform/frontend/` |
| systemd 服务 | `/etc/systemd/system/idc-platform.service` |
| Nginx 配置 | `/etc/nginx/sites-available/idc-platform` |
| Nginx 日志 | `/var/log/nginx/` |
| 数据库数据目录 | `/var/lib/postgresql/16/main` |
| WAL 归档 | `/var/lib/postgresql/16/wal_archive` |
| Redis 数据 | `/var/lib/redis/` |
| 备份 | `/backup/` |
| SSL 证书 | `/etc/letsencrypt/live/idc-platform.example.com/` |
| 环境变量文件 | `/opt/idc-platform/.env.production` |
| 迁移脚本 | `/opt/idc-platform/backend/migrations/` |
