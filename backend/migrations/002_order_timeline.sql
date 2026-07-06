-- ============================================================================
-- IDC Platform — Order Timeline Migration
-- Migration ID: 002_order_timeline
-- Description: Adds order_timeline table for recording order state transitions.
--              Each state change (pending→approved, approved→provisioning, etc.)
--              is recorded with timestamp and operator info for audit trail.
-- ============================================================================

-- --------------------------------------------------------------------------
-- A.9 订单状态流转历史
-- --------------------------------------------------------------------------

CREATE TABLE order_timeline (
    id              BIGSERIAL PRIMARY KEY,
    order_id        BIGINT NOT NULL,
    from_status     VARCHAR(32),               -- NULL for initial creation
    to_status       VARCHAR(32) NOT NULL,       -- target state
    operator_id     BIGINT,                     -- user who performed the action
    operator_name   VARCHAR(64),                -- snapshot of operator username
    remark          TEXT,                       -- optional note / reason
    created_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_ot_order ON order_timeline(order_id);
CREATE INDEX idx_ot_created ON order_timeline(created_at);

ALTER TABLE order_timeline ADD CONSTRAINT fk_ot_order
    FOREIGN KEY (order_id) REFERENCES orders(id) ON DELETE CASCADE;

ALTER TABLE order_timeline ADD CONSTRAINT fk_ot_operator
    FOREIGN KEY (operator_id) REFERENCES users(id);
