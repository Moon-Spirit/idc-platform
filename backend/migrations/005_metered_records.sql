-- ============================================================================
-- IDC Platform — Metered Billing & Addon Services Migration
-- Migration ID: 005_metered_records
-- Description: Adds metered_hours for tracking cloud server runtime (hourly
--              billing) and addon_services for addon subscription prorata
--              calculations (extra_ips, ddos_protection, backup_storage).
-- ============================================================================

-- --------------------------------------------------------------------------
-- B.10.1 按量计费 — 云服务器运行时记录
-- --------------------------------------------------------------------------
-- Tracks daily cloud server running hours for hourly billing.
-- Version column enables optimistic locking for concurrent updates.

CREATE TABLE metered_hours (
    id                BIGSERIAL PRIMARY KEY,
    subscription_id   BIGINT NOT NULL,
    date              DATE NOT NULL,
    hours_running     DECIMAL(6,2) NOT NULL DEFAULT 0.00,  -- 运行时长（小时）
    rate              DECIMAL(8,4),                          -- 当时有效的小时单价
    amount            DECIMAL(12,2) DEFAULT 0.00,            -- hours_running * rate
    source            VARCHAR(32) DEFAULT 'auto',            -- auto | admin | zjmf_sync
    remarks           TEXT,
    version           INT NOT NULL DEFAULT 1,                -- 乐观锁版本号
    created_at        TIMESTAMPTZ DEFAULT NOW(),
    updated_at        TIMESTAMPTZ DEFAULT NOW()
);

-- Each subscription has at most one metered record per day
ALTER TABLE metered_hours ADD CONSTRAINT uq_mh_sub_date
    UNIQUE (subscription_id, date);

CREATE INDEX idx_mh_subscription ON metered_hours(subscription_id);
CREATE INDEX idx_mh_date ON metered_hours(date);
CREATE INDEX idx_mh_period ON metered_hours(subscription_id, date);

ALTER TABLE metered_hours ADD CONSTRAINT fk_mh_subscription
    FOREIGN KEY (subscription_id) REFERENCES subscriptions(id) ON DELETE CASCADE;

-- --------------------------------------------------------------------------
-- B.10.2 附加服务订阅
-- --------------------------------------------------------------------------
-- Tracks addon services attached to a subscription for prorata billing.
-- Supports: extra_ips, ddos_protection, backup_storage

CREATE TABLE addon_services (
    id                BIGSERIAL PRIMARY KEY,
    subscription_id   BIGINT NOT NULL,
    addon_type        VARCHAR(32) NOT NULL,       -- extra_ips | ddos_protection | backup_storage
    product_id        BIGINT,                     -- nullable; references products(type='addon')
    specs             JSONB DEFAULT '{}',         -- 附加服务的规格快照（如 IP 数量、DDoS 清洗带宽、备份大小）
    monthly_price     DECIMAL(12,2) NOT NULL,     -- 附加服务的月单价（用于 prorata）
    start_date        DATE NOT NULL,              -- 附加服务生效日期
    end_date          DATE,                       -- NULL = 长期有效（随订阅一起计费/取消）
    status            VARCHAR(16) DEFAULT 'active',  -- active | cancelled | expired
    version           INT NOT NULL DEFAULT 1,     -- 乐观锁版本号
    created_at        TIMESTAMPTZ DEFAULT NOW(),
    updated_at        TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_addon_subscription ON addon_services(subscription_id);
CREATE INDEX idx_addon_type ON addon_services(addon_type);
CREATE INDEX idx_addon_status ON addon_services(status);

ALTER TABLE addon_services ADD CONSTRAINT fk_addon_subscription
    FOREIGN KEY (subscription_id) REFERENCES subscriptions(id) ON DELETE CASCADE;

ALTER TABLE addon_services ADD CONSTRAINT fk_addon_product
    FOREIGN KEY (product_id) REFERENCES products(id);

-- ============================================================================
-- Permission seeds for metered billing
-- ============================================================================
INSERT INTO permissions (code, name, module) VALUES
    ('metered:read', '查看按量计费', 'metered'),
    ('metered:write', '记录按量计费', 'metered'),
    ('addon:read', '查看附加服务', 'addon'),
    ('addon:write', '管理附加服务', 'addon')
ON CONFLICT (code) DO NOTHING;

-- Admin gets all new permissions
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id
FROM roles r, permissions p
WHERE r.name = 'admin'
  AND p.code IN ('metered:read', 'metered:write', 'addon:read', 'addon:write')
ON CONFLICT DO NOTHING;

-- Dealer gets read-only metered + addon permissions
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id
FROM roles r, permissions p
WHERE r.name = 'dealer'
  AND p.code IN ('metered:read', 'addon:read')
ON CONFLICT DO NOTHING;
