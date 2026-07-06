-- ============================================================================
-- IDC Platform — Provisioning Tracking Migration
-- PostgreSQL 16
-- Migration ID: 008_provisioning
-- Description: Adds provisioning tracking fields to subscriptions,
--              provisioning_failed order state support,
--              provisioning permission entries, and provisioning polling cron config.
-- ============================================================================

-- ============================================================================
-- SECTION 1: Additional provisioning tracking fields on subscriptions
-- ============================================================================

ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS provisioning_attempts   INT DEFAULT 0;
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS last_provision_attempt  TIMESTAMPTZ;
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS provisioning_error      TEXT;
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS provisioning_started_at TIMESTAMPTZ;
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS poll_count              INT DEFAULT 0;
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS poll_until              TIMESTAMPTZ;
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS provision_webhook_data  JSONB;

-- ============================================================================
-- SECTION 2: Index for provisioning polling queries
-- ============================================================================
CREATE INDEX IF NOT EXISTS idx_subs_polling
    ON subscriptions(provision_status, poll_until)
    WHERE provision_status = 'provisioning'
      AND remote_resource_id IS NOT NULL;

-- ============================================================================
-- SECTION 3: Order provisioning_failed state support
-- ============================================================================
-- provisioning_failed is a terminal state on the order level
-- that signals manual retry is required.

-- ============================================================================
-- SECTION 4: Provisioning management permissions
-- ============================================================================
INSERT INTO permissions (code, name, module) VALUES
    ('provisioning:read',  '查看开通状态', 'provisioning'),
    ('provisioning:retry', '重试开通',     'provisioning')
ON CONFLICT (code) DO NOTHING;

-- Grant provisioning permissions to admin role
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id
FROM roles r, permissions p
WHERE r.name = 'admin'
  AND p.code IN ('provisioning:read', 'provisioning:retry')
ON CONFLICT DO NOTHING;

-- Grant provisioning:read to dealer role
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id
FROM roles r, permissions p
WHERE r.name = 'dealer'
  AND p.code = 'provisioning:read'
ON CONFLICT DO NOTHING;

-- ============================================================================
-- SECTION 5: Provisioning system config defaults
-- ============================================================================
INSERT INTO system_config (key, value, description) VALUES
    ('provisioning.poll_interval', '30',  '开通轮询间隔（秒）'),
    ('provisioning.poll_timeout',  '1800', '开通轮询超时（秒），默认30分钟'),
    ('provisioning.poll_max_count','60',  '最大轮询次数')
ON CONFLICT (key) DO NOTHING;
