-- ============================================================================
-- IDC Platform — ZJMF Integration Migration
-- PostgreSQL 16
-- Migration ID: 007_zjmf_integration
-- Description: Adds ZJMF sync schema updates for race condition prevention,
--              permission entries for sync management, and webhook config.
-- ============================================================================

-- ============================================================================
-- SECTION 1: Add sync_version column to zjmf_sync_logs
-- ============================================================================
-- Oracle review fix: sync_version tracking prevents race conditions
-- by ensuring only one sync operation writes per version.

ALTER TABLE zjmf_sync_logs ADD COLUMN IF NOT EXISTS sync_version BIGINT DEFAULT 0;

CREATE INDEX IF NOT EXISTS idx_zjmf_sync_version
    ON zjmf_sync_logs(connection_id, entity_type, sync_version DESC);

-- ============================================================================
-- SECTION 2: Add webhook_secret to zjmf_connections (per-connection auth)
-- ============================================================================
ALTER TABLE zjmf_connections ADD COLUMN IF NOT EXISTS webhook_secret VARCHAR(256);
ALTER TABLE zjmf_connections ADD COLUMN IF NOT EXISTS webhook_url VARCHAR(512);
ALTER TABLE zjmf_connections ADD COLUMN IF NOT EXISTS rate_limit SMALLINT DEFAULT 30;
ALTER TABLE zjmf_connections ADD COLUMN IF NOT EXISTS mock_response_path VARCHAR(512);

-- ============================================================================
-- SECTION 3: Sync management permissions
-- ============================================================================
INSERT INTO permissions (code, name, module) VALUES
    ('sync:read', '查看同步状态', 'sync'),
    ('sync:trigger', '触发手动同步', 'sync'),
    ('sync:config', '配置同步连接', 'sync')
ON CONFLICT (code) DO NOTHING;

-- Grant sync permissions to admin role
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id
FROM roles r, permissions p
WHERE r.name = 'admin'
  AND p.code IN ('sync:read', 'sync:trigger', 'sync:config')
ON CONFLICT DO NOTHING;

-- Grant sync:read to dealer role
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id
FROM roles r, permissions p
WHERE r.name = 'dealer'
  AND p.code = 'sync:read'
ON CONFLICT DO NOTHING;
