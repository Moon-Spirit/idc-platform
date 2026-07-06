#pragma once

#include <json/json.h>
#include <string>

namespace idc {

/// Core recurring billing engine for monthly and yearly subscriptions.
///
/// Responsibilities:
///   - Monthly billing: generate billing_records for active monthly subscriptions
///   - Yearly billing: generate billing_records for active yearly subscriptions
///   - Prorata: partial-month calculation for mid-cycle starts
///   - PostgreSQL advisory lock (pg_advisory_xact_lock) to prevent concurrent
///     billing runs
///   - REPEATABLE READ isolation level for consistent billing snapshot
///   - Skip subscriptions already billed for the period (idempotent)
///
/// Thread-safety:
///   All public methods are safe to call concurrently. The advisory lock
///   ensures only one billing run executes at a time.
class BillingEngine {
public:
    // ── Billing run types ─────────────────────────────────────────────────

    /// Run monthly billing for all eligible active monthly subscriptions.
    ///
    /// Finds subscriptions where:
    ///   - status = 'active'
    ///   - billing_cycle = 'monthly'
    ///   - next_billing_date <= billing_date (end of current billing period)
    ///
    /// For each subscription:
    ///   1. Check if already billed for the period (idempotent)
    ///   2. Calculate amount (with prorata if mid-cycle)
    ///   3. Insert billing_record with details JSONB
    ///   4. Update next_billing_date
    ///
    /// Returns summary JSON:
    ///   { subscriptions_billed, total_amount, details: [...], errors: [...] }
    static Json::Value runMonthlyBilling();

    /// Run yearly billing for all eligible active yearly subscriptions.
    ///
    /// Same logic as runMonthlyBilling but for billing_cycle='yearly'
    /// and uses yearly_price from product_prices.
    ///
    /// Returns summary JSON (same shape as runMonthlyBilling).
    static Json::Value runYearlyBilling();

    // ── Status / diagnostics ─────────────────────────────────────────────

    /// Get the last billing run status from system_config.
    ///
    /// Returns JSON:
    ///   { last_run_at: "ISO8601", last_run_summary: { ... } }
    /// Returns null object if no billing run has been recorded.
    static Json::Value getLastRunStatus();

private:
    // ── Internal helpers ──────────────────────────────────────────────────

    /// Core billing logic shared by monthly and yearly billing.
    ///
    /// @param billingCycle  "monthly" or "yearly"
    /// @return Summary JSON
    static Json::Value runBillingForCycle(const std::string& billingCycle);

    /// Acquire a PostgreSQL advisory lock to serialize billing runs.
    ///
    /// Uses pg_advisory_xact_lock with a deterministic lock ID derived from
    /// the billing cycle name hash. The lock is held for the duration of the
    /// transaction and released automatically on commit/rollback.
    ///
    /// @return true if lock acquired, false if another billing run is in progress
    static bool tryAdvisoryLock(const std::string& billingCycle);

    /// Calculate the prorated amount for a subscription.
    ///
    /// Prorata applies when the subscription's start_date falls within the
    /// billing period (i.e., it started mid-cycle).
    ///
    /// Formula:
    ///   days_remaining = period_end - max(start_date, period_start) + 1
    ///   days_in_period = period_end - period_start + 1
    ///   amount = full_price * (days_remaining / days_in_period)
    ///
    /// @param fullPrice     The full monthly/yearly price
    /// @param startDate     Subscription start_date (YYYY-MM-DD)
    /// @param periodStart   Billing period start (YYYY-MM-DD)
    /// @param periodEnd     Billing period end (YYYY-MM-DD)
    /// @return Calculated amount, or fullPrice if no prorata needed
    static double calculateProrata(double fullPrice,
                                    const std::string& startDate,
                                    const std::string& periodStart,
                                    const std::string& periodEnd);

    /// Check whether a subscription has already been billed for the given
    /// period and type.
    static bool isAlreadyBilled(const std::string& billingCycle,
                                 int64_t subscriptionId,
                                 const std::string& periodStart,
                                 const std::string& periodEnd);

    /// Log the billing run result to operation_logs.
    static void logBillingRun(const Json::Value& summary,
                               const std::string& billingCycle);

    /// Persist the last run status to system_config.
    static void persistRunStatus(const Json::Value& summary,
                                  const std::string& billingCycle,
                                  const std::string& runAt);
};

} // namespace idc
