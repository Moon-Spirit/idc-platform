#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>
#include <functional>

namespace idc {

/// Billing cron job scheduler.
///
/// Registers a daily timer (every 86400 seconds) on the Drogon event loop.
/// On each tick, checks whether today is the billing day (1st of the month).
/// If so, triggers:
///   - BillingEngine::runMonthlyBilling() for monthly subscriptions
///   - BillingEngine::runYearlyBilling() for yearly subscriptions
///
/// Retry logic:
///   On failure, retries up to 3 times with a 5-minute backoff.
///
/// Usage: Call BillingCron::init() during application startup,
///        before drogon::app().run().
class BillingCron {
public:
    /// Register the daily billing check on the Drogon event loop.
    /// Safe to call once — subsequent calls are no-ops.
    static void init();

private:
    static bool initialized_;

    /// The daily check function invoked by the timer.
    static void onDailyTick();

    /// Run billing for a specific cycle with retry logic.
    /// @param billingCycle "monthly" or "yearly"
    /// @param maxRetries   Number of retry attempts (default: 3)
    /// @param retryDelayMs Delay between retries in ms (default: 300000 = 5min)
    static void runWithRetry(const std::string& billingCycle,
                              int maxRetries = 3,
                              int retryDelayMs = 300000);

    /// Check whether today is the 1st day of the month (billing day).
    static bool isBillingDay();
};

} // namespace idc
