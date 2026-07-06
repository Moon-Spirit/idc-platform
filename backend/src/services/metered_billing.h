#pragma once

#include <json/json.h>
#include <string>

namespace idc {

/// Hourly / metered billing calculation for cloud server subscriptions.
///
/// Cloud servers with `billing_cycle = 'hourly'` are charged per hour of
/// actual running time. Runtime is tracked daily in `metered_hours` and
/// consumed at the subscription's effective `hourly_price`.
///
/// Optimistic locking (version column on metered_hours) prevents
/// double-billing from concurrent auto-reporting and admin adjustments.
class MeteredBillingService {
public:
    // ── Hourly calculation ───────────────────────────────────────────────

    /// Calculate hourly billing for a subscription over a period.
    ///
    /// Aggregates metered_hours records in [period_start, period_end],
    /// multiplies total hours by the effective hourly_rate, and writes a
    /// billing_records entry with type='hourly' and full details in JSONB.
    ///
    /// @param subId         Subscription ID
    /// @param periodStart   Billing period start (inclusive, YYYY-MM-DD)
    /// @param periodEnd     Billing period end   (inclusive, YYYY-MM-DD)
    /// @return { subscription_id, period_start, period_end, total_hours,
    ///           hourly_rate, amount, billing_record_id, details }
    /// @throws std::invalid_argument if subscription not found or not hourly
    /// @throws std::runtime_error    on DB / optimistic-lock failure
    static Json::Value calculateHourly(int64_t subId,
                                       const std::string& periodStart,
                                       const std::string& periodEnd);

    // ── Runtime recording ────────────────────────────────────────────────

    /// Record (or update) daily running hours for a subscription.
    ///
    /// Uses PostgreSQL upsert (INSERT … ON CONFLICT) on the
    /// (subscription_id, date) unique constraint.  The version column acts
    /// as an optimistic lock – if a concurrent update changes the row
    /// between our read and write, the WHERE version = oldVersion clause
    /// causes the update to affect zero rows and we retry.
    ///
    /// @param subId         Subscription ID
    /// @param date          Date (YYYY-MM-DD)
    /// @param hours         Running hours for this date
    /// @param source        "auto" | "admin" | "zjmf_sync"
    /// @param remarks       Optional note
    /// @return Updated metered_hours row as JSON
    /// @throws std::invalid_argument if subscription does not exist
    /// @throws std::runtime_error    on DB / lock failure
    static Json::Value recordRuntime(int64_t subId,
                                     const std::string& date,
                                     double hours,
                                     const std::string& source = "auto",
                                     const std::string& remarks = "");

    // ── Query metered usage ─────────────────────────────────────────────

    /// Get metered usage records for a subscription over a date range.
    ///
    /// @param subId         Subscription ID
    /// @param periodStart   Start date (inclusive)
    /// @param periodEnd     End date   (inclusive)
    /// @return JSON array of metered_hours rows with summary:
    ///         { items: [...], total_hours, total_amount, hourly_rate }
    static Json::Value getMeteredUsage(int64_t subId,
                                       const std::string& periodStart,
                                       const std::string& periodEnd);

private:
    // ── Helpers ─────────────────────────────────────────────────────────

    /// Resolve the effective hourly_rate for a subscription.
    /// Reads hourly_price from product_prices (via pricing_service chain).
    static double resolveHourlyRate(int64_t subId, int64_t productId,
                                    int64_t distributorId);

    /// Build the details JSONB payload stored in billing_records.
    static Json::Value buildDetails(int64_t subId,
                                    const std::string& periodStart,
                                    const std::string& periodEnd,
                                    double totalHours,
                                    double hourlyRate,
                                    double amount);
};

} // namespace idc
