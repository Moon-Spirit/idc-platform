-- ============================================================================
-- IDC Platform — Dealer Registration Migration
-- Migration ID: 009_register
-- Description: Documents the users.status=2 (pending) convention used by
--              the self-registration endpoint. Also adds an index on
--              users.status to support admin review queries.
-- ============================================================================

-- --------------------------------------------------------------------------
-- Extend users.status values:
--   2  = pending  — new dealer registration awaiting admin approval
--   1  = active   — normal active account (existing, unchanged)
--   0  = disabled — account disabled by admin (existing, unchanged)
--  -1  = deleted  — soft-deleted account (existing, unchanged)
-- --------------------------------------------------------------------------

COMMENT ON COLUMN users.status IS
    'Account status: 2=pending (awaiting approval), 1=active, 0=disabled, -1=deleted';

-- Index to support admin queries for pending registrations
CREATE INDEX IF NOT EXISTS idx_users_status ON users(status);
