-- ============================================================================
-- IDC Platform — Database Initialization Migration
-- PostgreSQL 16 + TimescaleDB
-- Migration ID: 001_init
-- Description: Creates the idc_platform database with all tables, indexes,
--              TimescaleDB hypertable, and seed data.
-- ============================================================================

-- ============================================================================
-- SECTION 1: Extensions
-- ============================================================================

CREATE EXTENSION IF NOT EXISTS pgcrypto;
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- ============================================================================
-- SECTION 2: Tables (defined without FK constraints first; all FK constraints
--             are added in SECTION 3 to avoid circular dependency issues)
-- ============================================================================

-- --------------------------------------------------------------------------
-- A.1 认证与用户
-- --------------------------------------------------------------------------

CREATE TABLE roles (
    id          BIGSERIAL PRIMARY KEY,
    name        VARCHAR(64) NOT NULL UNIQUE,  -- admin | dealer | sub_dealer
    description VARCHAR(256),
    is_system   BOOLEAN DEFAULT FALSE,        -- 系统内置角色不可删除
    created_at  TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE permissions (
    id          BIGSERIAL PRIMARY KEY,
    code        VARCHAR(128) NOT NULL UNIQUE, -- e.g. "order:approve"
    name        VARCHAR(64) NOT NULL,         -- e.g. "审核订单"
    module      VARCHAR(64),                  -- e.g. "order"
    created_at  TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE role_permissions (
    role_id       BIGINT NOT NULL,
    permission_id BIGINT NOT NULL,
    PRIMARY KEY (role_id, permission_id)
);

CREATE TABLE users (
    id              BIGSERIAL PRIMARY KEY,
    username        VARCHAR(64) NOT NULL UNIQUE,
    password_hash   VARCHAR(256) NOT NULL,
    email           VARCHAR(128),
    phone           VARCHAR(20),
    real_name       VARCHAR(64),          -- 真实姓名/企业名
    status          SMALLINT DEFAULT 1,   -- 1=active 0=disabled -1=deleted
    role_id         BIGINT NOT NULL,
    distributor_id  BIGINT,               -- 所属经销商
    last_login_at   TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE jwt_blacklist (
    jti         VARCHAR(128) PRIMARY KEY,
    user_id     BIGINT NOT NULL,
    expires_at  TIMESTAMPTZ NOT NULL,
    created_at  TIMESTAMPTZ DEFAULT NOW()
);

-- --------------------------------------------------------------------------
-- A.2 经销商体系
-- --------------------------------------------------------------------------

CREATE TABLE distributors (
    id                BIGSERIAL PRIMARY KEY,
    name              VARCHAR(128) NOT NULL,
    level             SMALLINT DEFAULT 1,     -- 1=一级 2=二级 3=三级...
    parent_id         BIGINT,
    price_template_id BIGINT,
    contact_person    VARCHAR(64),
    contact_phone     VARCHAR(20),
    contact_email     VARCHAR(128),
    balance           DECIMAL(12,2) DEFAULT 0.00,  -- 预充值余额
    credit_limit      DECIMAL(12,2) DEFAULT 0.00,  -- 信用额度
    status            SMALLINT DEFAULT 1,     -- 1=active 0=disabled -1=deleted
    remark            TEXT,
    created_at        TIMESTAMPTZ DEFAULT NOW(),
    updated_at        TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE distributor_settlements (
    id                BIGSERIAL PRIMARY KEY,
    distributor_id    BIGINT NOT NULL,
    period_start      DATE NOT NULL,
    period_end        DATE NOT NULL,
    total_commission  DECIMAL(12,2) DEFAULT 0.00,  -- 佣金总额
    settled_amount    DECIMAL(12,2) DEFAULT 0.00,    -- 已结算
    status            SMALLINT DEFAULT 0,  -- 0=pending 1=settled 2=disputed
    settled_at        TIMESTAMPTZ,
    created_at        TIMESTAMPTZ DEFAULT NOW()
);

-- --------------------------------------------------------------------------
-- A.3 产品与定价
-- --------------------------------------------------------------------------

CREATE TABLE products (
    id              BIGSERIAL PRIMARY KEY,
    name            VARCHAR(128) NOT NULL,
    type            VARCHAR(32) NOT NULL,  -- bare_metal | cloud | bandwidth | ip | addon | rack
    description     TEXT,
    specs           JSONB NOT NULL DEFAULT '{}',
    status          SMALLINT DEFAULT 1,    -- 1=active 0=disabled
    sort_order      INT DEFAULT 0,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE price_templates (
    id              BIGSERIAL PRIMARY KEY,
    name            VARCHAR(128) NOT NULL,
    distributor_id  BIGINT,                 -- NULL=全局默认模板
    parent_id       BIGINT,                 -- 继承父模板
    effective_date  DATE NOT NULL,
    expiry_date     DATE,                   -- NULL=长期有效
    status          SMALLINT DEFAULT 1,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE product_prices (
    id                  BIGSERIAL PRIMARY KEY,
    price_template_id   BIGINT NOT NULL,
    product_id          BIGINT NOT NULL,
    monthly_price       DECIMAL(12,2),            -- 月付价
    yearly_price        DECIMAL(12,2),            -- 年付价（月付*10）
    hourly_price        DECIMAL(8,4),             -- 按量价（元/小时）
    bandwidth_95_price  DECIMAL(10,2),            -- 95带宽单价（元/Mbps）
    setup_fee           DECIMAL(10,2) DEFAULT 0.00, -- 一次性设置费
    min_cycle           SMALLINT DEFAULT 1,       -- 最小购买周期（月）
    UNIQUE (price_template_id, product_id)
);

-- --------------------------------------------------------------------------
-- A.4 订单与订阅
-- --------------------------------------------------------------------------

CREATE TABLE orders (
    id              BIGSERIAL PRIMARY KEY,
    order_no        VARCHAR(64) NOT NULL UNIQUE,  -- 订单号：ORD+日期+序列
    distributor_id  BIGINT NOT NULL,
    status          VARCHAR(32) NOT NULL DEFAULT 'pending',
    total_amount    DECIMAL(12,2) NOT NULL,
    discount_amount DECIMAL(12,2) DEFAULT 0.00,
    final_amount    DECIMAL(12,2) NOT NULL,
    billing_cycle   VARCHAR(16) NOT NULL,     -- monthly | yearly | hourly
    payment_term    SMALLINT DEFAULT 30,      -- 付款期限（天）
    remark          TEXT,
    approved_by     BIGINT,
    approved_at     TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE order_items (
    id              BIGSERIAL PRIMARY KEY,
    order_id        BIGINT NOT NULL,
    product_id      BIGINT NOT NULL,
    product_name    VARCHAR(128) NOT NULL,      -- 下单时的产品名（快照）
    product_specs   JSONB,                      -- 下单时的规格快照
    quantity        INT DEFAULT 1,
    unit_price      DECIMAL(12,2) NOT NULL,     -- 下单时的单价
    subtotal        DECIMAL(12,2) NOT NULL,     -- quantity * unit_price
    period_months   INT DEFAULT 1,              -- 购买时长（月）
    created_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE subscriptions (
    id                  BIGSERIAL PRIMARY KEY,
    sub_no              VARCHAR(64) NOT NULL UNIQUE, -- SUB+日期+序列
    order_id            BIGINT NOT NULL,
    order_item_id       BIGINT,
    distributor_id      BIGINT NOT NULL,
    product_id          BIGINT NOT NULL,
    product_specs       JSONB,                      -- 实际配置
    status              VARCHAR(32) NOT NULL DEFAULT 'pending',
    start_date          DATE,
    next_billing_date   DATE,
    end_date            DATE,
    billing_cycle       VARCHAR(16) NOT NULL,
    auto_renew          BOOLEAN DEFAULT TRUE,
    remote_system       VARCHAR(32),    -- zjmf_v10 | zjmf_finance
    remote_resource_id  VARCHAR(128),   -- 智简魔方侧的资源ID
    provision_status    VARCHAR(32),   -- pending | provisioning | done | failed
    provision_data      JSONB,         -- 开通请求/响应的完整记录
    suspend_reason      TEXT,
    created_at          TIMESTAMPTZ DEFAULT NOW(),
    updated_at          TIMESTAMPTZ DEFAULT NOW()
);

-- --------------------------------------------------------------------------
-- A.5 计费引擎
-- --------------------------------------------------------------------------

CREATE TABLE bandwidth_samples (
    time            TIMESTAMPTZ NOT NULL,
    subscription_id BIGINT NOT NULL,
    port_id         VARCHAR(64),              -- 端口标识（多端口时）
    bytes_in        BIGINT NOT NULL,           -- 入流量 bytes（5分钟累计）
    bytes_out       BIGINT NOT NULL,           -- 出流量 bytes
    bytes_in_rate   DECIMAL(12,4),             -- 入方向平均速率 bps
    bytes_out_rate  DECIMAL(12,4)              -- 出方向平均速率 bps
);

CREATE TABLE billing_records (
    id              BIGSERIAL PRIMARY KEY,
    subscription_id BIGINT NOT NULL,
    type            VARCHAR(32) NOT NULL,  -- monthly | yearly | bandwidth_95 | hourly | addon
    period_start    DATE NOT NULL,
    period_end      DATE NOT NULL,
    amount          DECIMAL(12,2) NOT NULL,
    quantity        DECIMAL(12,4),          -- 计费数量（如带宽Mbps、小时数）
    unit_price      DECIMAL(12,4),
    details         JSONB,                  -- 计算明细（如95采样的原始数据快照）
    invoice_id      BIGINT,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE invoices (
    id                        BIGSERIAL PRIMARY KEY,
    invoice_no                VARCHAR(64) NOT NULL UNIQUE, -- INV+日期+序列
    distributor_id            BIGINT NOT NULL,
    status                    VARCHAR(16) NOT NULL DEFAULT 'unpaid',
    period_start              DATE NOT NULL,
    period_end                DATE NOT NULL,
    due_date                  DATE NOT NULL,
    total_amount              DECIMAL(12,2) NOT NULL,
    paid_amount               DECIMAL(12,2) DEFAULT 0.00,
    discount_amount           DECIMAL(12,2) DEFAULT 0.00,
    discount_id               BIGINT,
    bandwidth_usage_snapshot  JSONB,              -- 95计费快照
    notes                     TEXT,
    paid_at                   TIMESTAMPTZ,
    created_at                TIMESTAMPTZ DEFAULT NOW(),
    updated_at                TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE invoice_items (
    id                BIGSERIAL PRIMARY KEY,
    invoice_id        BIGINT NOT NULL,
    type              VARCHAR(32) NOT NULL,  -- subscription | bandwidth_95 | hourly | addon | setup
    description       TEXT,
    subscription_id   BIGINT,
    billing_record_id BIGINT,
    amount            DECIMAL(12,2) NOT NULL,
    created_at        TIMESTAMPTZ DEFAULT NOW()
);

-- --------------------------------------------------------------------------
-- A.6 支付
-- --------------------------------------------------------------------------

CREATE TABLE payments (
    id                BIGSERIAL PRIMARY KEY,
    payment_no        VARCHAR(64) NOT NULL UNIQUE,
    invoice_id        BIGINT,
    distributor_id    BIGINT NOT NULL,
    method            VARCHAR(32) NOT NULL,  -- alipay | wechat | balance | transfer
    amount            DECIMAL(12,2) NOT NULL,
    status            VARCHAR(16) NOT NULL DEFAULT 'pending',
    transaction_id    VARCHAR(256),         -- 支付网关交易号
    gateway_response  JSONB,                -- 网关返回的完整数据
    paid_at           TIMESTAMPTZ,
    refund_id         VARCHAR(256),
    refund_reason     TEXT,
    created_at        TIMESTAMPTZ DEFAULT NOW(),
    updated_at        TIMESTAMPTZ DEFAULT NOW()
);

-- --------------------------------------------------------------------------
-- A.7 智简魔方集成
-- --------------------------------------------------------------------------

CREATE TABLE zjmf_connections (
    id              BIGSERIAL PRIMARY KEY,
    name            VARCHAR(128) NOT NULL,
    type            VARCHAR(32) NOT NULL,  -- v10 | finance
    direction       VARCHAR(16) NOT NULL,  -- upstream | downstream
    api_base_url    VARCHAR(256) NOT NULL,
    api_key         VARCHAR(256) NOT NULL,  -- 加密存储
    api_secret      VARCHAR(256) NOT NULL,  -- 加密存储
    sync_interval   INT DEFAULT 300,       -- 同步间隔（秒）
    status          SMALLINT DEFAULT 1,    -- 1=active 0=disabled
    last_sync_at    TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE zjmf_sync_logs (
    id              BIGSERIAL PRIMARY KEY,
    connection_id   BIGINT NOT NULL,
    direction       VARCHAR(16) NOT NULL,  -- inbound | outbound
    entity_type     VARCHAR(64) NOT NULL,  -- product | inventory | order | subscription
    entity_id       BIGINT,                -- 本地实体ID
    remote_id       VARCHAR(128),          -- 智简魔方端实体ID
    action          VARCHAR(32) NOT NULL,  -- sync | push | pull | callback
    request_data    JSONB,
    response_data   JSONB,
    status          VARCHAR(16) NOT NULL,  -- success | failed | pending
    error_message   TEXT,
    retry_count     INT DEFAULT 0,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE zjmf_entity_mappings (
    id              BIGSERIAL PRIMARY KEY,
    local_type      VARCHAR(32) NOT NULL,  -- product | subscription | order
    local_id        BIGINT NOT NULL,
    remote_system   VARCHAR(32) NOT NULL,  -- zjmf_v10 | zjmf_finance
    remote_id       VARCHAR(128) NOT NULL,
    remote_data     JSONB,                 -- 远程实体的关键字段快照
    last_synced_at  TIMESTAMPTZ DEFAULT NOW(),
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE (local_type, local_id, remote_system),
    UNIQUE (remote_system, remote_id)
);

-- --------------------------------------------------------------------------
-- A.8 系统/运维
-- --------------------------------------------------------------------------

CREATE TABLE system_config (
    key             VARCHAR(128) PRIMARY KEY,
    value           TEXT NOT NULL,
    description     VARCHAR(256),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE operation_logs (
    id              BIGSERIAL PRIMARY KEY,
    user_id         BIGINT,
    action          VARCHAR(64) NOT NULL,     -- order.approve, invoice.pay
    entity_type     VARCHAR(32),
    entity_id       BIGINT,
    detail          JSONB,
    ip_address      INET,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================================================
-- SECTION 3: Foreign Key Constraints
-- ============================================================================

-- role_permissions
ALTER TABLE role_permissions ADD CONSTRAINT fk_rp_role
    FOREIGN KEY (role_id) REFERENCES roles(id) ON DELETE CASCADE;
ALTER TABLE role_permissions ADD CONSTRAINT fk_rp_permission
    FOREIGN KEY (permission_id) REFERENCES permissions(id) ON DELETE CASCADE;

-- users
ALTER TABLE users ADD CONSTRAINT fk_users_role
    FOREIGN KEY (role_id) REFERENCES roles(id);
ALTER TABLE users ADD CONSTRAINT fk_users_distributor
    FOREIGN KEY (distributor_id) REFERENCES distributors(id);

-- jwt_blacklist
ALTER TABLE jwt_blacklist ADD CONSTRAINT fk_jwt_user
    FOREIGN KEY (user_id) REFERENCES users(id);

-- distributors (self-referencing + price_templates)
ALTER TABLE distributors ADD CONSTRAINT fk_distributors_parent
    FOREIGN KEY (parent_id) REFERENCES distributors(id);
ALTER TABLE distributors ADD CONSTRAINT fk_distributors_price_template
    FOREIGN KEY (price_template_id) REFERENCES price_templates(id);

-- distributor_settlements
ALTER TABLE distributor_settlements ADD CONSTRAINT fk_ds_distributor
    FOREIGN KEY (distributor_id) REFERENCES distributors(id);

-- price_templates (self-referencing + distributors — resolves circular FK)
ALTER TABLE price_templates ADD CONSTRAINT fk_pt_distributor
    FOREIGN KEY (distributor_id) REFERENCES distributors(id);
ALTER TABLE price_templates ADD CONSTRAINT fk_pt_parent
    FOREIGN KEY (parent_id) REFERENCES price_templates(id);

-- product_prices
ALTER TABLE product_prices ADD CONSTRAINT fk_pp_template
    FOREIGN KEY (price_template_id) REFERENCES price_templates(id) ON DELETE CASCADE;
ALTER TABLE product_prices ADD CONSTRAINT fk_pp_product
    FOREIGN KEY (product_id) REFERENCES products(id);

-- orders
ALTER TABLE orders ADD CONSTRAINT fk_orders_distributor
    FOREIGN KEY (distributor_id) REFERENCES distributors(id);
ALTER TABLE orders ADD CONSTRAINT fk_orders_approver
    FOREIGN KEY (approved_by) REFERENCES users(id);

-- order_items
ALTER TABLE order_items ADD CONSTRAINT fk_oi_order
    FOREIGN KEY (order_id) REFERENCES orders(id) ON DELETE CASCADE;
ALTER TABLE order_items ADD CONSTRAINT fk_oi_product
    FOREIGN KEY (product_id) REFERENCES products(id);

-- subscriptions
ALTER TABLE subscriptions ADD CONSTRAINT fk_subs_order
    FOREIGN KEY (order_id) REFERENCES orders(id);
ALTER TABLE subscriptions ADD CONSTRAINT fk_subs_order_item
    FOREIGN KEY (order_item_id) REFERENCES order_items(id);
ALTER TABLE subscriptions ADD CONSTRAINT fk_subs_distributor
    FOREIGN KEY (distributor_id) REFERENCES distributors(id);
ALTER TABLE subscriptions ADD CONSTRAINT fk_subs_product
    FOREIGN KEY (product_id) REFERENCES products(id);

-- bandwidth_samples
ALTER TABLE bandwidth_samples ADD CONSTRAINT fk_bw_subscription
    FOREIGN KEY (subscription_id) REFERENCES subscriptions(id);

-- invoices
ALTER TABLE invoices ADD CONSTRAINT fk_inv_distributor
    FOREIGN KEY (distributor_id) REFERENCES distributors(id);

-- billing_records
ALTER TABLE billing_records ADD CONSTRAINT fk_br_subscription
    FOREIGN KEY (subscription_id) REFERENCES subscriptions(id);
ALTER TABLE billing_records ADD CONSTRAINT fk_br_invoice
    FOREIGN KEY (invoice_id) REFERENCES invoices(id);

-- invoice_items
ALTER TABLE invoice_items ADD CONSTRAINT fk_ii_invoice
    FOREIGN KEY (invoice_id) REFERENCES invoices(id) ON DELETE CASCADE;
ALTER TABLE invoice_items ADD CONSTRAINT fk_ii_subscription
    FOREIGN KEY (subscription_id) REFERENCES subscriptions(id);
ALTER TABLE invoice_items ADD CONSTRAINT fk_ii_billing_record
    FOREIGN KEY (billing_record_id) REFERENCES billing_records(id);

-- payments
ALTER TABLE payments ADD CONSTRAINT fk_pay_invoice
    FOREIGN KEY (invoice_id) REFERENCES invoices(id);
ALTER TABLE payments ADD CONSTRAINT fk_pay_distributor
    FOREIGN KEY (distributor_id) REFERENCES distributors(id);

-- zjmf_sync_logs
ALTER TABLE zjmf_sync_logs ADD CONSTRAINT fk_zsl_connection
    FOREIGN KEY (connection_id) REFERENCES zjmf_connections(id);

-- operation_logs
ALTER TABLE operation_logs ADD CONSTRAINT fk_ol_user
    FOREIGN KEY (user_id) REFERENCES users(id);

-- ============================================================================
-- SECTION 4: TimescaleDB Hypertable Setup
-- ============================================================================

-- Convert bandwidth_samples to a hypertable, chunked by 1 day
SELECT create_hypertable('bandwidth_samples', 'time',
    chunk_time_interval => INTERVAL '1 day');

-- Oracle review fix: unique constraint on (time, subscription_id, port_id)
-- prevents duplicate samples from overlapping collection intervals
ALTER TABLE bandwidth_samples ADD UNIQUE (time, subscription_id, port_id);

-- Oracle review fix: compression after 7 days reduces storage footprint
SELECT add_compression_policy('bandwidth_samples', INTERVAL '7 days');

-- Oracle review fix: 6-month retention covers billing dispute windows
SELECT add_retention_policy('bandwidth_samples', INTERVAL '6 months');

-- ============================================================================
-- SECTION 5: Indexes
-- ============================================================================

-- users
CREATE INDEX idx_users_role ON users(role_id);
CREATE INDEX idx_users_distributor ON users(distributor_id);

-- distributors
CREATE INDEX idx_distributors_parent ON distributors(parent_id);
-- Note: recursive CTE for tree queries:
--   WITH RECURSIVE tree AS (
--     SELECT * FROM distributors WHERE id = ?
--     UNION ALL
--     SELECT d.* FROM distributors d JOIN tree t ON d.parent_id = t.id
--   ) SELECT * FROM tree;

-- distributor_settlements
CREATE INDEX idx_settlements_distributor ON distributor_settlements(distributor_id);

-- products
CREATE INDEX idx_products_type ON products(type);
CREATE INDEX idx_products_specs ON products USING gin(specs);

-- price_templates
CREATE INDEX idx_price_templates_distributor ON price_templates(distributor_id);

-- product_prices
CREATE INDEX idx_product_prices_template ON product_prices(price_template_id);
CREATE INDEX idx_product_prices_product ON product_prices(product_id);

-- orders
CREATE INDEX idx_orders_distributor ON orders(distributor_id);
CREATE INDEX idx_orders_status ON orders(status);
CREATE INDEX idx_orders_created ON orders(created_at);

-- order_items
CREATE INDEX idx_order_items_order ON order_items(order_id);

-- subscriptions
CREATE INDEX idx_subscriptions_distributor ON subscriptions(distributor_id);
CREATE INDEX idx_subscriptions_status ON subscriptions(status);
CREATE INDEX idx_subscriptions_next_billing ON subscriptions(next_billing_date);
CREATE INDEX idx_subscriptions_remote ON subscriptions(remote_system, remote_resource_id);

-- bandwidth_samples
-- Oracle review fix: covering index for 95th percentile billing query
--   WHERE subscription_id = ? AND time >= ? AND time < ?
--   PARTITION BY subscription_id ORDER BY bytes_out_rate DESC
CREATE INDEX idx_bw_samples_sub_time
    ON bandwidth_samples(subscription_id, time DESC);
CREATE INDEX idx_bw_samples_billing
    ON bandwidth_samples(subscription_id, time DESC, bytes_out_rate DESC);

-- billing_records
CREATE INDEX idx_billing_records_subscription ON billing_records(subscription_id);
CREATE INDEX idx_billing_records_invoice ON billing_records(invoice_id);
CREATE INDEX idx_billing_records_period ON billing_records(period_start, period_end);

-- invoices
CREATE INDEX idx_invoices_distributor ON invoices(distributor_id);
CREATE INDEX idx_invoices_status ON invoices(status);
CREATE INDEX idx_invoices_due_date ON invoices(due_date);
CREATE INDEX idx_invoices_period ON invoices(period_start, period_end);

-- invoice_items
CREATE INDEX idx_invoice_items_invoice ON invoice_items(invoice_id);

-- payments
CREATE INDEX idx_payments_invoice ON payments(invoice_id);
CREATE INDEX idx_payments_distributor ON payments(distributor_id);
CREATE INDEX idx_payments_transaction ON payments(transaction_id);

-- zjmf_sync_logs
CREATE INDEX idx_zjmf_sync_connection ON zjmf_sync_logs(connection_id);
CREATE INDEX idx_zjmf_sync_status ON zjmf_sync_logs(status);
CREATE INDEX idx_zjmf_sync_created ON zjmf_sync_logs(created_at);
CREATE INDEX idx_zjmf_sync_entity ON zjmf_sync_logs(entity_type, entity_id);

-- zjmf_entity_mappings
CREATE INDEX idx_zjmf_mappings_local ON zjmf_entity_mappings(local_type, local_id);
CREATE INDEX idx_zjmf_mappings_remote ON zjmf_entity_mappings(remote_system, remote_id);

-- operation_logs
CREATE INDEX idx_oplogs_user ON operation_logs(user_id);
CREATE INDEX idx_oplogs_action ON operation_logs(action);
CREATE INDEX idx_oplogs_created ON operation_logs(created_at);

-- ============================================================================
-- SECTION 6: Seed Data
-- ============================================================================

-- --------------------------------------------------------------------------
-- 6.1 Roles
-- --------------------------------------------------------------------------
INSERT INTO roles (name, description, is_system) VALUES
    ('admin', '系统管理员 — 全平台权限', TRUE),
    ('dealer', '经销商 — 基础业务权限', FALSE);

-- --------------------------------------------------------------------------
-- 6.2 Permissions
-- --------------------------------------------------------------------------
INSERT INTO permissions (code, name, module) VALUES
    -- User management
    ('user:create', '创建用户', 'user'),
    ('user:read', '查看用户', 'user'),
    ('user:update', '编辑用户', 'user'),
    ('user:delete', '删除用户', 'user'),
    ('user:list', '用户列表', 'user'),
    -- Role management
    ('role:create', '创建角色', 'role'),
    ('role:read', '查看角色', 'role'),
    ('role:update', '编辑角色', 'role'),
    ('role:delete', '删除角色', 'role'),
    ('role:list', '角色列表', 'role'),
    -- Permission management
    ('permission:read', '查看权限', 'permission'),
    ('permission:list', '权限列表', 'permission'),
    -- Distributor management
    ('distributor:create', '创建经销商', 'distributor'),
    ('distributor:read', '查看经销商', 'distributor'),
    ('distributor:update', '编辑经销商', 'distributor'),
    ('distributor:delete', '删除经销商', 'distributor'),
    ('distributor:list', '经销商列表', 'distributor'),
    ('distributor:settlement', '经销商结算', 'distributor'),
    -- Product management
    ('product:create', '创建产品', 'product'),
    ('product:read', '查看产品', 'product'),
    ('product:update', '编辑产品', 'product'),
    ('product:delete', '删除产品', 'product'),
    ('product:list', '产品列表', 'product'),
    -- Price management
    ('price:create', '创建定价', 'price'),
    ('price:read', '查看定价', 'price'),
    ('price:update', '编辑定价', 'price'),
    ('price:delete', '删除定价', 'price'),
    ('price:list', '定价列表', 'price'),
    -- Order management
    ('order:create', '创建订单', 'order'),
    ('order:read', '查看订单', 'order'),
    ('order:update', '编辑订单', 'order'),
    ('order:delete', '删除订单', 'order'),
    ('order:list', '订单列表', 'order'),
    ('order:approve', '审核订单', 'order'),
    ('order:reject', '驳回订单', 'order'),
    -- Subscription management
    ('subscription:create', '创建订阅', 'subscription'),
    ('subscription:read', '查看订阅', 'subscription'),
    ('subscription:update', '编辑订阅', 'subscription'),
    ('subscription:delete', '删除订阅', 'subscription'),
    ('subscription:list', '订阅列表', 'subscription'),
    ('subscription:suspend', '暂停订阅', 'subscription'),
    ('subscription:terminate', '终止订阅', 'subscription'),
    ('subscription:provision', '开通订阅', 'subscription'),
    -- Invoice management
    ('invoice:create', '创建账单', 'invoice'),
    ('invoice:read', '查看账单', 'invoice'),
    ('invoice:update', '编辑账单', 'invoice'),
    ('invoice:delete', '删除账单', 'invoice'),
    ('invoice:list', '账单列表', 'invoice'),
    ('invoice:pay', '支付账单', 'invoice'),
    ('invoice:void', '作废账单', 'invoice'),
    -- Payment management
    ('payment:read', '查看支付', 'payment'),
    ('payment:list', '支付列表', 'payment'),
    ('payment:refund', '退款处理', 'payment'),
    -- Report
    ('report:read', '查看报表', 'report'),
    ('report:export', '导出报表', 'report'),
    -- System
    ('system:read', '查看系统配置', 'system'),
    ('system:update', '修改系统配置', 'system'),
    -- Operation log
    ('operation:read', '查看操作日志', 'operation'),
    ('operation:list', '操作日志列表', 'operation');

-- --------------------------------------------------------------------------
-- 6.3 Role-Permission Assignments
-- --------------------------------------------------------------------------

-- Admin: all permissions
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id
FROM roles r, permissions p
WHERE r.name = 'admin';

-- Dealer: basic business permissions only
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id
FROM roles r, permissions p
WHERE r.name = 'dealer'
  AND p.code IN (
        'user:read', 'user:list',
        'permission:read', 'permission:list',
        'distributor:read', 'distributor:list',
        'product:read', 'product:list',
        'price:read', 'price:list',
        'order:create', 'order:read', 'order:list',
        'subscription:read', 'subscription:list',
        'invoice:read', 'invoice:list', 'invoice:pay',
        'payment:read', 'payment:list',
        'report:read',
        'operation:read', 'operation:list'
      );

-- --------------------------------------------------------------------------
-- 6.4 Default Admin User
-- --------------------------------------------------------------------------
-- Password: admin123 (bcrypt hashed, 10 rounds)
-- The application uses bcrypt for password verification.
INSERT INTO users (username, password_hash, email, real_name, status, role_id)
VALUES (
    'admin',
    '$2a$10$bIHn7mjrtFpdkBmkHoCRxOBSuY4zgMa.SeMSWT2oBJHvHoW9xH97e',
    'admin@idcplatform.com',
    'System Admin',
    1,
    (SELECT id FROM roles WHERE name = 'admin')
);

-- --------------------------------------------------------------------------
-- 6.5 Default System Configuration
-- --------------------------------------------------------------------------
INSERT INTO system_config (key, value, description) VALUES
    ('billing.day_of_month', '5', '每月出账日'),
    ('billing.payment_term_days', '30', '账单付款期限（天）'),
    ('billing.auto_suspend_days', '15', '逾期暂停服务（天）'),
    ('billing.auto_terminate_days', '30', '逾期终止服务（天）'),
    ('dunning.reminder_days_before', '3', '账单到期前提醒（天）'),
    ('dunning.reminder_overdue_days', '1,3,7', '逾期提醒间隔（天）');
