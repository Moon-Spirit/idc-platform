# 附录A：数据库表结构详细设计

> 说明：此为第一期核心表结构，使用 PostgreSQL 16 + TimescaleDB 扩展。

---

## A.1 认证与用户

### users 用户表
```sql
CREATE TABLE users (
    id              BIGSERIAL PRIMARY KEY,
    username        VARCHAR(64) NOT NULL UNIQUE,
    password_hash   VARCHAR(256) NOT NULL,
    email           VARCHAR(128),
    phone           VARCHAR(20),
    real_name       VARCHAR(64),          -- 真实姓名/企业名
    status          SMALLINT DEFAULT 1,   -- 1=active 0=disabled -1=deleted
    role_id         BIGINT NOT NULL REFERENCES roles(id),
    distributor_id  BIGINT REFERENCES distributors(id), -- 所属经销商
    last_login_at   TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_users_role ON users(role_id);
CREATE INDEX idx_users_distributor ON users(distributor_id);
```

### roles 角色表
```sql
CREATE TABLE roles (
    id          BIGSERIAL PRIMARY KEY,
    name        VARCHAR(64) NOT NULL UNIQUE,  -- admin | dealer | sub_dealer
    description VARCHAR(256),
    is_system   BOOLEAN DEFAULT FALSE,        -- 系统内置角色不可删除
    created_at  TIMESTAMPTZ DEFAULT NOW()
);
```

### permissions 权限表
```sql
CREATE TABLE permissions (
    id          BIGSERIAL PRIMARY KEY,
    code        VARCHAR(128) NOT NULL UNIQUE, -- e.g. "order:approve"
    name        VARCHAR(64) NOT NULL,         -- e.g. "审核订单"
    module      VARCHAR(64),                  -- e.g. "order"
    created_at  TIMESTAMPTZ DEFAULT NOW()
);
```

### role_permissions 角色权限关联
```sql
CREATE TABLE role_permissions (
    role_id       BIGINT NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    permission_id BIGINT NOT NULL REFERENCES permissions(id) ON DELETE CASCADE,
    PRIMARY KEY (role_id, permission_id)
);
```

### jwt_blacklist JWT黑名单（Redis优先）
```sql
-- Redis key: jwt:blacklist:{jti}
-- TTL: 等于token剩余有效期
-- 数据库持久化仅用于极端情况恢复（可选）
CREATE TABLE jwt_blacklist (
    jti         VARCHAR(128) PRIMARY KEY,
    user_id     BIGINT NOT NULL REFERENCES users(id),
    expires_at  TIMESTAMPTZ NOT NULL,
    created_at  TIMESTAMPTZ DEFAULT NOW()
);
-- 定期清理：DELETE FROM jwt_blacklist WHERE expires_at < NOW()
```

---

## A.2 经销商体系

### distributors 经销商表
```sql
CREATE TABLE distributors (
    id              BIGSERIAL PRIMARY KEY,
    name            VARCHAR(128) NOT NULL,
    level           SMALLINT DEFAULT 1,     -- 1=一级 2=二级 3=三级...
    parent_id       BIGINT REFERENCES distributors(id),
    price_template_id BIGINT REFERENCES price_templates(id),
    contact_person  VARCHAR(64),
    contact_phone   VARCHAR(20),
    contact_email   VARCHAR(128),
    balance         DECIMAL(12,2) DEFAULT 0.00,  -- 预充值余额
    credit_limit    DECIMAL(12,2) DEFAULT 0.00,  -- 信用额度
    status          SMALLINT DEFAULT 1,     -- 1=active 0=disabled -1=deleted
    remark          TEXT,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
-- 组织树查询（递归CTE）
CREATE INDEX idx_distributors_parent ON distributors(parent_id);
-- WITH RECURSIVE tree AS (
--   SELECT * FROM distributors WHERE id = ?
--   UNION ALL
--   SELECT d.* FROM distributors d JOIN tree t ON d.parent_id = t.id
-- ) SELECT * FROM tree;
```

### distributor_settlements 经销商结算记录
```sql
CREATE TABLE distributor_settlements (
    id              BIGSERIAL PRIMARY KEY,
    distributor_id  BIGINT NOT NULL REFERENCES distributors(id),
    period_start    DATE NOT NULL,
    period_end      DATE NOT NULL,
    total_commission DECIMAL(12,2) DEFAULT 0.00,  -- 佣金总额
    settled_amount  DECIMAL(12,2) DEFAULT 0.00,    -- 已结算
    status          SMALLINT DEFAULT 0,  -- 0=pending 1=settled 2=disputed
    settled_at      TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_settlements_distributor ON distributor_settlements(distributor_id);
```

---

## A.3 产品与定价

### products 产品表
```sql
CREATE TABLE products (
    id              BIGSERIAL PRIMARY KEY,
    name            VARCHAR(128) NOT NULL,
    type            VARCHAR(32) NOT NULL,  -- bare_metal | cloud | bandwidth | ip | addon | rack
    description     TEXT,
    specs           JSONB NOT NULL DEFAULT '{}',
    -- specs示例:
    -- bare_metal: {"cpu":"E5-2680v4","cores":14,"ram":"64GB","disk":"2x480GB SSD","gpu":null}
    -- cloud: {"cpu_range":[1,32],"ram_range":[1,128],"disk_range":[10,2000]}
    -- bandwidth: {"port_speed":"10G","commit":"1Gbps","burst":"2Gbps"}
    -- ip: {"type":"ipv4","block_size":"/29","count":5}
    status          SMALLINT DEFAULT 1,    -- 1=active 0=disabled
    sort_order      INT DEFAULT 0,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_products_type ON products(type);
CREATE INDEX idx_products_specs ON products USING gin(specs);
```

### price_templates 价格模板表
```sql
CREATE TABLE price_templates (
    id              BIGSERIAL PRIMARY KEY,
    name            VARCHAR(128) NOT NULL,
    distributor_id  BIGINT REFERENCES distributors(id),  -- NULL=全局默认模板
    parent_id       BIGINT REFERENCES price_templates(id), -- 继承父模板
    effective_date  DATE NOT NULL,
    expiry_date     DATE,          -- NULL=长期有效
    status          SMALLINT DEFAULT 1,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_price_templates_distributor ON price_templates(distributor_id);
```

### product_prices 产品定价表
```sql
CREATE TABLE product_prices (
    id              BIGSERIAL PRIMARY KEY,
    price_template_id BIGINT NOT NULL REFERENCES price_templates(id) ON DELETE CASCADE,
    product_id      BIGINT NOT NULL REFERENCES products(id),
    -- 价格字段（根据product.type不同字段有效）
    monthly_price   DECIMAL(12,2),            -- 月付价
    yearly_price    DECIMAL(12,2),            -- 年付价（月付*10）
    hourly_price    DECIMAL(8,4),             -- 按量价（元/小时）
    bandwidth_95_price DECIMAL(10,2),         -- 95带宽单价（元/Mbps）
    setup_fee       DECIMAL(10,2) DEFAULT 0.00, -- 一次性设置费
    min_cycle       SMALLINT DEFAULT 1,       -- 最小购买周期（月）
    UNIQUE (price_template_id, product_id)
);
CREATE INDEX idx_product_prices_template ON product_prices(price_template_id);
CREATE INDEX idx_product_prices_product ON product_prices(product_id);
```

---

## A.4 订单与订阅

### orders 订单表
```sql
CREATE TABLE orders (
    id              BIGSERIAL PRIMARY KEY,
    order_no        VARCHAR(64) NOT NULL UNIQUE,  -- 订单号：ORD+日期+序列
    distributor_id  BIGINT NOT NULL REFERENCES distributors(id),
    status          VARCHAR(32) NOT NULL DEFAULT 'pending',
    -- 状态机流转：pending → approved(审核通过) → provisioning(开通中) → active(运行中)
    --            pending → rejected(驳回)
    --            active → suspended(暂停) → terminated(终止)
    total_amount    DECIMAL(12,2) NOT NULL,
    discount_amount DECIMAL(12,2) DEFAULT 0.00,
    final_amount    DECIMAL(12,2) NOT NULL,
    billing_cycle   VARCHAR(16) NOT NULL,     -- monthly | yearly | hourly
    payment_term    SMALLINT DEFAULT 30,      -- 付款期限（天）
    remark          TEXT,
    approved_by     BIGINT REFERENCES users(id),
    approved_at     TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_orders_distributor ON orders(distributor_id);
CREATE INDEX idx_orders_status ON orders(status);
CREATE INDEX idx_orders_created ON orders(created_at);
```

### order_items 订单项表
```sql
CREATE TABLE order_items (
    id              BIGSERIAL PRIMARY KEY,
    order_id        BIGINT NOT NULL REFERENCES orders(id) ON DELETE CASCADE,
    product_id      BIGINT NOT NULL REFERENCES products(id),
    product_name    VARCHAR(128) NOT NULL,      -- 下单时的产品名（快照）
    product_specs   JSONB,                      -- 下单时的规格快照
    quantity        INT DEFAULT 1,
    unit_price      DECIMAL(12,2) NOT NULL,     -- 下单时的单价
    subtotal        DECIMAL(12,2) NOT NULL,     -- quantity * unit_price
    period_months   INT DEFAULT 1,              -- 购买时长（月）
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_order_items_order ON order_items(order_id);
```

### subscriptions 订阅/服务实例表
```sql
CREATE TABLE subscriptions (
    id              BIGSERIAL PRIMARY KEY,
    sub_no          VARCHAR(64) NOT NULL UNIQUE, -- SUB+日期+序列
    order_id        BIGINT NOT NULL REFERENCES orders(id),
    order_item_id   BIGINT REFERENCES order_items(id),
    distributor_id  BIGINT NOT NULL REFERENCES distributors(id),
    product_id      BIGINT NOT NULL REFERENCES products(id),
    product_specs   JSONB,                      -- 实际配置
    status          VARCHAR(32) NOT NULL DEFAULT 'pending',
    -- pending → active → suspended → terminated
    start_date      DATE,
    next_billing_date DATE,
    end_date        DATE,
    billing_cycle   VARCHAR(16) NOT NULL,
    auto_renew      BOOLEAN DEFAULT TRUE,
    -- 智简魔方映射
    remote_system   VARCHAR(32),    -- zjmf_v10 | zjmf_finance
    remote_resource_id VARCHAR(128), -- 智简魔方侧的资源ID
    provision_status VARCHAR(32),   -- pending | provisioning | done | failed
    provision_data  JSONB,          -- 开通请求/响应的完整记录
    suspend_reason  TEXT,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_subscriptions_distributor ON subscriptions(distributor_id);
CREATE INDEX idx_subscriptions_status ON subscriptions(status);
CREATE INDEX idx_subscriptions_next_billing ON subscriptions(next_billing_date);
CREATE INDEX idx_subscriptions_remote ON subscriptions(remote_system, remote_resource_id);
```

---

## A.5 计费引擎

### bandwidth_samples 带宽采样数据（TimescaleDB Hypertable）
```sql
-- 启用TimescaleDB扩展
CREATE EXTENSION IF NOT EXISTS timescaledb;

CREATE TABLE bandwidth_samples (
    time            TIMESTAMPTZ NOT NULL,
    subscription_id BIGINT NOT NULL REFERENCES subscriptions(id),
    port_id         VARCHAR(64),              -- 端口标识（多端口时）
    bytes_in        BIGINT NOT NULL,           -- 入流量 bytes（5分钟累计）
    bytes_out       BIGINT NOT NULL,           -- 出流量 bytes
    bytes_in_rate   DECIMAL(12,4),             -- 入方向平均速率 bps
    bytes_out_rate  DECIMAL(12,4)              -- 出方向平均速率 bps
);
-- 转换为超表（按时间和订阅ID分区）
SELECT create_hypertable('bandwidth_samples', 'time',
    chunk_time_interval => INTERVAL '1 day');

-- Oracle审查修正：增加覆盖索引确保95计费查询性能
CREATE INDEX idx_bw_samples_sub_time 
    ON bandwidth_samples(subscription_id, time DESC);
CREATE INDEX idx_bw_samples_billing 
    ON bandwidth_samples(subscription_id, time DESC, bytes_out_rate DESC);

-- Oracle审查修正：添加数据压缩策略（7天后压缩）
SELECT add_compression_policy('bandwidth_samples', INTERVAL '7 days');
-- Oracle审查修正：添加数据保留策略（保留6个月，足够处理账单争议）
SELECT add_retention_policy('bandwidth_samples', INTERVAL '6 months');
-- Oracle审查修正：添加唯一约束防止重复采样
ALTER TABLE bandwidth_samples ADD UNIQUE (time, subscription_id, port_id);
```

### 95分位带宽计费查询示例
```sql
-- 月度95计费计算
WITH sorted_samples AS (
    SELECT 
        bytes_out_rate,
        ROW_NUMBER() OVER (
            PARTITION BY subscription_id, port_id 
            ORDER BY bytes_out_rate DESC
        ) AS rank,
        COUNT(*) OVER (
            PARTITION BY subscription_id, port_id
        ) AS total_count
    FROM bandwidth_samples
    WHERE subscription_id = ?
      AND time >= ?
      AND time < ?
)
SELECT 
    subscription_id,
    port_id,
    bytes_out_rate AS billing_rate,
    bytes_out_rate * ? AS bill_amount
FROM sorted_samples
-- Oracle审查修正：原使用ROUND(total_count * 0.05)会系统性地多收费用
-- 正确公式：排除最高的5%，取下一个 = FLOOR(total_count * 0.05) + 1
-- 例：100个样本→排除5个（最高5%）→取第6个
WHERE rank = FLOOR(total_count * 0.05) + 1;
```

### billing_records 计费记录表
```sql
CREATE TABLE billing_records (
    id              BIGSERIAL PRIMARY KEY,
    subscription_id BIGINT NOT NULL REFERENCES subscriptions(id),
    type            VARCHAR(32) NOT NULL,  -- monthly | yearly | bandwidth_95 | hourly | addon
    period_start    DATE NOT NULL,
    period_end      DATE NOT NULL,
    amount          DECIMAL(12,2) NOT NULL,
    quantity        DECIMAL(12,4),          -- 计费数量（如带宽Mbps、小时数）
    unit_price      DECIMAL(12,4),
    details         JSONB,                  -- 计算明细（如95采样的原始数据快照）
    invoice_id      BIGINT REFERENCES invoices(id),
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_billing_records_subscription ON billing_records(subscription_id);
CREATE INDEX idx_billing_records_invoice ON billing_records(invoice_id);
CREATE INDEX idx_billing_records_period ON billing_records(period_start, period_end);
```

### invoices 账单表
```sql
CREATE TABLE invoices (
    id              BIGSERIAL PRIMARY KEY,
    invoice_no      VARCHAR(64) NOT NULL UNIQUE, -- INV+日期+序列
    distributor_id  BIGINT NOT NULL REFERENCES distributors(id),
    status          VARCHAR(16) NOT NULL DEFAULT 'unpaid',
    -- unpaid → paid | overdue → paid | void
    period_start    DATE NOT NULL,
    period_end      DATE NOT NULL,
    due_date        DATE NOT NULL,
    total_amount    DECIMAL(12,2) NOT NULL,
    paid_amount     DECIMAL(12,2) DEFAULT 0.00,
    discount_amount DECIMAL(12,2) DEFAULT 0.00,  -- 预留：优惠券折抵
    discount_id     BIGINT,                      -- 预留：优惠券ID
    bandwidth_usage_snapshot JSONB,              -- 95计费快照
    notes           TEXT,
    paid_at         TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_invoices_distributor ON invoices(distributor_id);
CREATE INDEX idx_invoices_status ON invoices(status);
CREATE INDEX idx_invoices_due_date ON invoices(due_date);
CREATE INDEX idx_invoices_period ON invoices(period_start, period_end);
```

### invoice_items 账单明细表
```sql
CREATE TABLE invoice_items (
    id              BIGSERIAL PRIMARY KEY,
    invoice_id      BIGINT NOT NULL REFERENCES invoices(id) ON DELETE CASCADE,
    type            VARCHAR(32) NOT NULL,  -- subscription | bandwidth_95 | hourly | addon | setup
    description     TEXT,
    subscription_id BIGINT REFERENCES subscriptions(id),
    billing_record_id BIGINT REFERENCES billing_records(id),
    amount          DECIMAL(12,2) NOT NULL,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_invoice_items_invoice ON invoice_items(invoice_id);
```

---

## A.6 支付

### payments 支付记录表
```sql
CREATE TABLE payments (
    id              BIGSERIAL PRIMARY KEY,
    payment_no      VARCHAR(64) NOT NULL UNIQUE,
    invoice_id      BIGINT REFERENCES invoices(id),
    distributor_id  BIGINT NOT NULL REFERENCES distributors(id),
    method          VARCHAR(32) NOT NULL,  -- alipay | wechat | balance | transfer
    amount          DECIMAL(12,2) NOT NULL,
    status          VARCHAR(16) NOT NULL DEFAULT 'pending',
    -- pending → success | failed | refunded
    transaction_id  VARCHAR(256),         -- 支付网关交易号
    gateway_response JSONB,               -- 网关返回的完整数据
    paid_at         TIMESTAMPTZ,
    refund_id       VARCHAR(256),
    refund_reason   TEXT,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_payments_invoice ON payments(invoice_id);
CREATE INDEX idx_payments_distributor ON payments(distributor_id);
CREATE INDEX idx_payments_transaction ON payments(transaction_id);
```

---

## A.7 智简魔方集成

### zjmf_connections 智简魔方连接配置
```sql
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
```

### zjmf_sync_logs 同步日志表
```sql
CREATE TABLE zjmf_sync_logs (
    id              BIGSERIAL PRIMARY KEY,
    connection_id   BIGINT NOT NULL REFERENCES zjmf_connections(id),
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
CREATE INDEX idx_zjmf_sync_connection ON zjmf_sync_logs(connection_id);
CREATE INDEX idx_zjmf_sync_status ON zjmf_sync_logs(status);
CREATE INDEX idx_zjmf_sync_created ON zjmf_sync_logs(created_at);
CREATE INDEX idx_zjmf_sync_entity ON zjmf_sync_logs(entity_type, entity_id);
```

### zjmf_entity_mappings 实体映射表
```sql
-- 维护本地实体ID和智简魔方远程实体ID的映射关系
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
CREATE INDEX idx_zjmf_mappings_local ON zjmf_entity_mappings(local_type, local_id);
CREATE INDEX idx_zjmf_mappings_remote ON zjmf_entity_mappings(remote_system, remote_id);
```

---

## A.8 系统/运维

### system_config 系统配置表
```sql
CREATE TABLE system_config (
    key             VARCHAR(128) PRIMARY KEY,
    value           TEXT NOT NULL,
    description     VARCHAR(256),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
-- 配置项示例:
-- billing.day_of_month: 5 (每月5号出账)
-- billing.payment_term_days: 30
-- billing.auto_suspend_days: 15
-- billing.auto_terminate_days: 30
-- dunning.reminder_days_before: 3
-- dunning.reminder_overdue_days: 1,3,7
```

### operation_logs 操作日志表
```sql
CREATE TABLE operation_logs (
    id              BIGSERIAL PRIMARY KEY,
    user_id         BIGINT REFERENCES users(id),
    action          VARCHAR(64) NOT NULL,     -- order.approve, invoice.pay
    entity_type     VARCHAR(32),
    entity_id       BIGINT,
    detail          JSONB,
    ip_address      INET,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_oplogs_user ON operation_logs(user_id);
CREATE INDEX idx_oplogs_action ON operation_logs(action);
CREATE INDEX idx_oplogs_created ON operation_logs(created_at);
-- 定期归档：按分区表或移动到历史表
```

---

## A.9 第一期ER图（关系概览）

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
