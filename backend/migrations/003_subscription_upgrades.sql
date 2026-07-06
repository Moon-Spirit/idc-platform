-- ============================================================================
-- IDC Platform — Subscription Timeline & Upgrades Migration
-- Migration ID: 003_subscription_upgrades
-- Description: Adds subscription_timeline for state transition audit trail,
--              and subscription_upgrades for tracking upgrade/downgrade
--              spec change requests.
-- ============================================================================

-- --------------------------------------------------------------------------
-- A.9.1 订阅状态流转历史
-- --------------------------------------------------------------------------

CREATE TABLE subscription_timeline (
    id              BIGSERIAL PRIMARY KEY,
    subscription_id BIGINT NOT NULL,
    from_status     VARCHAR(32),               -- NULL for initial creation
    to_status       VARCHAR(32) NOT NULL,       -- target state
    operator_id     BIGINT,                     -- user who performed the action
    operator_name   VARCHAR(64),                -- snapshot of operator username
    remark          TEXT,                       -- optional note / reason
    created_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_st_subscription ON subscription_timeline(subscription_id);
CREATE INDEX idx_st_created ON subscription_timeline(created_at);

ALTER TABLE subscription_timeline ADD CONSTRAINT fk_st_subscription
    FOREIGN KEY (subscription_id) REFERENCES subscriptions(id) ON DELETE CASCADE;

ALTER TABLE subscription_timeline ADD CONSTRAINT fk_st_operator
    FOREIGN KEY (operator_id) REFERENCES users(id);

-- --------------------------------------------------------------------------
-- A.9.2 升降配变更请求
-- --------------------------------------------------------------------------

CREATE TABLE subscription_upgrades (
    id                BIGSERIAL PRIMARY KEY,
    subscription_id   BIGINT NOT NULL,
    from_specs        JSONB NOT NULL,           -- current specs before change
    to_specs          JSONB NOT NULL,           -- requested specs
    status            VARCHAR(16) NOT NULL DEFAULT 'pending',  -- pending | approved | rejected | completed
    effective_date    VARCHAR(16) NOT NULL DEFAULT 'immediate',  -- immediate | next_billing
    requested_by      BIGINT,                   -- user who requested
    approved_by       BIGINT,                   -- admin who approved
    remark            TEXT,
    created_at        TIMESTAMPTZ DEFAULT NOW(),
    updated_at        TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_su_subscription ON subscription_upgrades(subscription_id);
CREATE INDEX idx_su_status ON subscription_upgrades(status);

ALTER TABLE subscription_upgrades ADD CONSTRAINT fk_su_subscription
    FOREIGN KEY (subscription_id) REFERENCES subscriptions(id) ON DELETE CASCADE;

ALTER TABLE subscription_upgrades ADD CONSTRAINT fk_su_requested_by
    FOREIGN KEY (requested_by) REFERENCES users(id);

ALTER TABLE subscription_upgrades ADD CONSTRAINT fk_su_approved_by
    FOREIGN KEY (approved_by) REFERENCES users(id);
