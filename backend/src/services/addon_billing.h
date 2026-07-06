#pragma once

#include <json/json.h>
#include <string>

namespace idc {

/// Addon service prorata billing calculation.
///
/// Addon services (extra_ips, ddos_protection, backup_storage) attached to
/// a subscription are billed at a monthly_price that is prorated when the
/// addon is added or removed mid-cycle.
///
/// Prorata formula:
///   daily_rate      = monthly_price / days_in_month
///   amount          = daily_rate × remaining_days
///
/// Supports optimistic locking via version column on addon_services.
class AddonBillingService {
public:
    // ── Prorata calculation ─────────────────────────────────────────────

    /// Calculate prorata amount for an addon service over a billing period.
    ///
    /// For addons that start mid-cycle, only the remaining days from the
    /// addon's start_date to the period_end are charged.  For addons that
    /// end mid-cycle, only days from period_start to end_date are charged.
    ///
    /// @param subId         Subscription ID
    /// @param addonItem     JSON object with keys:
    ///                         addon_type     — "extra_ips"|"ddos_protection"|"backup_storage"
    ///                         monthly_price  — decimal string or number
    ///                         start_date     — YYYY-MM-DD
    ///                         end_date       — YYYY-MM-DD or null
    /// @param periodStart   Billing period start (YYYY-MM-DD)
    /// @param periodEnd     Billing period end   (YYYY-MM-DD)
    /// @return { addon_type, monthly_price, days_in_month, days_used,
    ///           daily_rate, amount, details }
    /// @throws std::invalid_argument on invalid input
    static Json::Value calculateAddonProrata(
        int64_t subId,
        const Json::Value& addonItem,
        const std::string& periodStart,
        const std::string& periodEnd);

    // ── Addon lifecycle ─────────────────────────────────────────────────

    /// Create a new addon service for a subscription.
    ///
    /// @param subId         Subscription ID
    /// @param addonType     "extra_ips" | "ddos_protection" | "backup_storage"
    /// @param monthlyPrice  Monthly price in yuan
    /// @param startDate     Effective date (YYYY-MM-DD)
    /// @param specs         Optional JSON specs object
    /// @return Created addon_services row as JSON
    /// @throws std::invalid_argument if subscription not found
    static Json::Value createAddon(int64_t subId,
                                   const std::string& addonType,
                                   double monthlyPrice,
                                   const std::string& startDate,
                                   const Json::Value& specs = Json::objectValue);

    /// Cancel an addon service (sets end_date and status = 'cancelled').
    ///
    /// @param addonId       Addon service ID
    /// @param endDate       Cancellation date (YYYY-MM-DD)
    /// @return Updated addon_services row as JSON
    /// @throws std::invalid_argument if not found or not active
    static Json::Value cancelAddon(int64_t addonId,
                                   const std::string& endDate);

    // ── Queries ─────────────────────────────────────────────────────────

    /// List active addons for a subscription.
    static Json::Value listAddons(int64_t subId);

    /// Get a specific addon by ID.
    static Json::Value getAddonById(int64_t addonId);

private:
    // ── Helpers ─────────────────────────────────────────────────────────

    /// Calculate days in a month given a date string (YYYY-MM-DD).
    static int daysInMonth(const std::string& dateStr);

    /// Calculate number of days between two date strings (inclusive).
    static int daysBetween(const std::string& start, const std::string& end);

    /// Validate addon type.
    static bool isValidAddonType(const std::string& type);
};

} // namespace idc
