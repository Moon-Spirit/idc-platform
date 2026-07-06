-- ============================================================================
-- IDC Platform — Billing Engine Migration
-- Migration ID: 004_billing_records
-- Description: Adds additional indexes for billing performance, billing:run
--              permission, and system config for last billing run tracking.
-- ============================================================================

-- --------------------------------------------------------------------------
-- A.5.1  Additional billing_records indexes for query performance
-- --------------------------------------------------------------------------

-- Composite index for idempotency check: find existing billing records
-- by subscription + type + period quickly.
CREATE INDEX IF NOT EXISTS idx_billing_records_type_period
    ON billing_records(subscription_id, type, period_start, period_end);

-- Index for finding all billing records in a given month (for billing day)
CREATE INDEX IF NOT EXISTS idx_billing_records_period_start
    ON billing_records(period_start);

-- --------------------------------------------------------------------------
-- A.8.1  Add billing:run permission
-- --------------------------------------------------------------------------

INSERT INTO permissions (code, name, module)
SELECT 'billing:run', '手动触发计费', 'billing'
WHERE NOT EXISTS (
    SELECT 1 FROM permissions WHERE code = 'billing:run'
);

INSERT INTO permissions (code, name, module)
SELECT 'billing:read', '查看计费状态', 'billing'
WHERE NOT EXISTS (
    SELECT 1 FROM permissions WHERE code = 'billing:read'
);

-- Grant billing permissions to admin role
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id
FROM roles r, permissions p
WHERE r.name = 'admin'
  AND p.code IN ('billing:run', 'billing:read')
  AND NOT EXISTS (
    SELECT 1 FROM role_permissions rp
    WHERE rp.role_id = r.id AND rp.permission_id = p.id
);

-- --------------------------------------------------------------------------
-- A.8.2  System config for billing run tracking
-- --------------------------------------------------------------------------

INSERT INTO system_config (key, value, description)
SELECT 'billing.last_run_at', '', '上次计费执行时间（ISO8601）'
WHERE NOT EXISTS (
    SELECT 1 FROM system_config WHERE key = 'billing.last_run_at'
);

INSERT INTO system_config (key, value, description)
SELECT 'billing.last_run_summary', '{}', '上次计费执行摘要JSON'
WHERE NOT EXISTS (
    SELECT 1 FROM system_config WHERE key = 'billing.last_run_summary'
);

INSERT INTO system_config (key, value, description)
SELECT 'billing.run_monthly', 'true', '是否启用月度自动计费'
WHERE NOT EXISTS (
    SELECT 1 FROM system_config WHERE key = 'billing.run_monthly'
);

INSERT INTO system_config (key, value, description)
SELECT 'billing.run_yearly', 'true', '是否启用年度自动计费'
WHERE NOT EXISTS (
    SELECT 1 FROM system_config WHERE key = 'billing.run_yearly'
);
