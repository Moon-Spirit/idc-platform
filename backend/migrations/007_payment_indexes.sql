-- ============================================================================
-- IDC Platform — Payment System Indexes & Enhancements Migration
-- Migration ID: 007_payment_indexes
-- Description: Adds additional indexes for payment query performance,
--              idempotency dedup, and refund tracking.
-- ============================================================================

-- --------------------------------------------------------------------------
-- B.7.2  Payment status + method index for admin dashboard filtering
-- --------------------------------------------------------------------------
-- Covers queries like:
--   SELECT * FROM payments WHERE status = 'pending' AND method = 'alipay'
--   SELECT COUNT(*) FROM payments WHERE paid_at >= NOW() - INTERVAL '30 days'
CREATE INDEX IF NOT EXISTS idx_payments_status_method
    ON payments(status, method);

CREATE INDEX IF NOT EXISTS idx_payments_paid_at
    ON payments(paid_at DESC);

-- --------------------------------------------------------------------------
-- B.7.3  Idempotency dedup index
-- --------------------------------------------------------------------------
-- Covers the 7-day dedup window query:
--   SELECT COUNT(*) FROM payments
--   WHERE transaction_id = $1
--     AND status = 'completed'
--     AND created_at >= NOW() - INTERVAL '7 days'
-- The existing idx_payments_transaction index on (transaction_id) already
-- covers the transaction_id lookup. The status + created_at filter is covered
-- by the composite index below. Together these ensure fast dedup checks.
CREATE INDEX IF NOT EXISTS idx_payments_dedup
    ON payments(transaction_id, status, created_at DESC)
    WHERE transaction_id IS NOT NULL AND status = 'completed';

-- --------------------------------------------------------------------------
-- B.7.4  Invoice status + due_date for dunning/payment-reminder queries
-- --------------------------------------------------------------------------
-- Covers queries that find unpaid invoices for payment checks:
--   SELECT * FROM invoices WHERE status = 'unpaid' AND due_date < NOW()
CREATE INDEX IF NOT EXISTS idx_invoices_unpaid_due
    ON invoices(status, due_date)
    WHERE status = 'unpaid';

-- --------------------------------------------------------------------------
-- B.7.5  Payment method index on invoices (if we add method tracking later)
-- --------------------------------------------------------------------------
-- Distributor's monthly payment summary:
--   SELECT SUM(amount) FROM payments
--   WHERE distributor_id = $1 AND status = 'completed'
--     AND paid_at >= $2 AND paid_at < $3
CREATE INDEX IF NOT EXISTS idx_payments_distributor_paid
    ON payments(distributor_id, paid_at DESC)
    WHERE status = 'completed';

-- --------------------------------------------------------------------------
-- B.7.6  Payment number lookup (already unique, but useful for partial match)
-- --------------------------------------------------------------------------
-- The payment_no column already has a UNIQUE constraint; the existing
-- idx_payments_invoice and idx_payments_distributor indexes cover the
-- common query paths.
-- This index covers LIKE queries for payment number prefix searches.
CREATE INDEX IF NOT EXISTS idx_payments_no_prefix
    ON payments(payment_no text_pattern_ops);

-- ============================================================================
-- SECTION: Comments for documentation
-- ============================================================================

COMMENT ON INDEX idx_payments_status_method IS
    'Admin dashboard: filter payments by status + gateway method';
COMMENT ON INDEX idx_payments_paid_at IS
    'Revenue reports: payments completed in a time range';
COMMENT ON INDEX idx_payments_dedup IS
    'Oracle review fix: 7-day dedup window for payment callback idempotency';
COMMENT ON INDEX idx_invoices_unpaid_due IS
    'Dunning: find overdue unpaid invoices efficiently';
COMMENT ON INDEX idx_payments_distributor_paid IS
    'Distributor monthly payment summary';
COMMENT ON INDEX idx_payments_no_prefix IS
    'Payment number prefix search (text_pattern_ops)';
