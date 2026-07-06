# IDC Platform — Database Migrations

## Prerequisites

- **PostgreSQL 16** with `pgcrypto` extension (auto-enabled by script)
- **TimescaleDB 2.x+** extension installed in the PostgreSQL instance
- `psql` client (or any PostgreSQL-compatible client)

Verify TimescaleDB is installed:

```bash
psql -U postgres -c "SELECT extname, extversion FROM pg_extension WHERE extname = 'timescaledb';"
```

If empty, install TimescaleDB via your OS package manager:

```bash
# Ubuntu/Debian
sudo apt install timescaledb-2-postgresql-16

# RHEL/CentOS
sudo dnf install timescaledb_16

# macOS (Homebrew)
brew install timescaledb
```

## Quick Start

### Create the database and apply the migration

```bash
# As postgres superuser
psql -U postgres -c "CREATE DATABASE idc_platform;"
psql -d idc_platform -f 001_init.sql
```

### What the migration does

| Component | Details |
|-----------|---------|
| **Extensions** | Enables `pgcrypto` (password hashing) and `timescaledb` (time-series) |
| **Tables** | 23 tables across 8 domains (auth, distributor, product, order, billing, payment, ZJMF integration, system) |
| **Foreign Keys** | All referential constraints with `ON DELETE CASCADE` where appropriate |
| **TimescaleDB** | `bandwidth_samples` converted to hypertable (1-day chunks), 7-day compression, 6-month retention |
| **Indexes** | 39 indexes including GIN on JSONB columns and composite indexes for 95th percentile billing queries |
| **Seed Data** | Admin/dealer roles, 54 permissions, role assignments, default admin user (`admin`/`admin123`), system config |

### Verify the migration

```sql
-- Check all tables exist
\dt

-- Check hypertable
SELECT * FROM timescaledb_information.hypertables
WHERE hypertable_name = 'bandwidth_samples';

-- Check compression policy
SELECT * FROM timescaledb_information.jobs
WHERE proc_name = 'policy_compression';

-- Check retention policy
SELECT * FROM timescaledb_information.jobs
WHERE proc_name = 'policy_retention';

-- Verify seed admin user
SELECT username, email, status FROM users;

-- Verify roles
SELECT name, is_system FROM roles;

-- Count permissions
SELECT COUNT(*) AS permission_count FROM permissions;
```

## Schema Overview

```
users ──── role ──── role_permissions ──── permissions
  │
  └─── distributors ──── parent (自引用)
         │
         ├─── price_templates ──── product_prices ──── products
         │
         ├─── orders ──── order_items
         │
         ├─── subscriptions ──── bandwidth_samples (TimescaleDB)
         │
         ├─── invoices ──── invoice_items ──── billing_records
         │
         ├─── payments
         │
         └─── distributor_settlements

zjmf_connections ──── zjmf_sync_logs
                 └─── zjmf_entity_mappings
```

## Table Inventory

| # | Table | Domain | Hypertable | Key Indexes |
|---|-------|--------|------------|-------------|
| 1 | `roles` | Auth | — | name (UNIQUE) |
| 2 | `permissions` | Auth | — | code (UNIQUE) |
| 3 | `role_permissions` | Auth | — | (role_id, permission_id) PK |
| 4 | `users` | Auth | — | role_id, distributor_id |
| 5 | `jwt_blacklist` | Auth | — | user_id |
| 6 | `distributors` | Distributor | — | parent_id, price_template_id |
| 7 | `distributor_settlements` | Distributor | — | distributor_id |
| 8 | `products` | Product | — | type (btree), specs (GIN) |
| 9 | `price_templates` | Product | — | distributor_id |
| 10 | `product_prices` | Product | — | template_id, product_id |
| 11 | `orders` | Order | — | distributor_id, status, created_at |
| 12 | `order_items` | Order | — | order_id |
| 13 | `subscriptions` | Order | — | distributor_id, status, next_billing, remote |
| 14 | `bandwidth_samples` | Billing | **Yes** | (sub_id, time DESC), (sub_id, time DESC, rate DESC) |
| 15 | `billing_records` | Billing | — | subscription_id, invoice_id, period |
| 16 | `invoices` | Billing | — | distributor_id, status, due_date, period |
| 17 | `invoice_items` | Billing | — | invoice_id |
| 18 | `payments` | Payment | — | invoice_id, distributor_id, transaction_id |
| 19 | `zjmf_connections` | Integration | — | — |
| 20 | `zjmf_sync_logs` | Integration | — | connection_id, status, entity |
| 21 | `zjmf_entity_mappings` | Integration | — | (local_type, local_id), (remote_system, remote_id) |
| 22 | `system_config` | System | — | key PK |
| 23 | `operation_logs` | System | — | user_id, action, created_at |

## File Naming Convention

Migrations follow the pattern `NNN_description.sql`:

```
001_init.sql         — Initial schema + seed data
002_add_coupons.sql  — Future: coupon/discount system
003_add_dns.sql      — Future: DNS management
...
```

## Rollback

There is no rollback script for `001_init`. To reset:

```bash
psql -U postgres -c "DROP DATABASE IF EXISTS idc_platform;"
psql -U postgres -c "CREATE DATABASE idc_platform;"
psql -d idc_platform -f 001_init.sql
```

> ⚠️ This destroys ALL data. Never run in production without a recent backup.

## Connection Configuration

The application (Drogon C++) connects via `backend/config.json`:

```json
{
  "db": {
    "host": "localhost",
    "port": 5432,
    "database": "idc_platform",
    "user": "idc_app",
    "password": "${DB_PASSWORD}"
  }
}
```

For local development, create a dedicated user:

```sql
CREATE USER idc_app WITH PASSWORD 'dev_password';
GRANT CONNECT ON DATABASE idc_platform TO idc_app;
GRANT USAGE ON SCHEMA public TO idc_app;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO idc_app;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO idc_app;
```
