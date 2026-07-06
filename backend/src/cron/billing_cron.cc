#include "billing_cron.h"
#include "services/billing_engine.h"
#include "utils/logger.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>

#include <ctime>
#include <string>
#include <thread>

namespace idc {

bool BillingCron::initialized_ = false;

// ═══════════════════════════════════════════════════════════════════════════
//  Initialization
// ═══════════════════════════════════════════════════════════════════════════

void BillingCron::init() {
    if (initialized_) {
        LOG_WARN << "[BillingCron] Already initialized — ignoring duplicate call";
        return;
    }
    initialized_ = true;

    LOG_INFO << "[BillingCron] Registering daily billing check (86400s interval)";

    // Schedule the first check 1 hour after startup (gives DB time to warm up),
    // then every 86400 seconds (24 hours) thereafter.
    auto loop = drogon::app().getLoop();
    loop->runAfter(3600, []() {
        onDailyTick();
    });

    loop->runEvery(86400, []() {
        onDailyTick();
    });

    LOG_INFO << "[BillingCron] Daily billing cron registered";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Daily Tick
// ═══════════════════════════════════════════════════════════════════════════

void BillingCron::onDailyTick() {
    if (!isBillingDay()) {
        LOG_DEBUG << "[BillingCron] Today is not billing day — skipping";
        return;
    }

    LOG_INFO << "[BillingCron] Billing day detected — starting billing runs";

    // Run monthly billing with retry
    runWithRetry("monthly");

    // Run yearly billing with retry
    runWithRetry("yearly");

    LOG_INFO << "[BillingCron] Billing day complete";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Run with Retry
// ═══════════════════════════════════════════════════════════════════════════

void BillingCron::runWithRetry(const std::string& billingCycle,
                                int maxRetries,
                                int retryDelayMs) {
    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        LOG_INFO << "[BillingCron] Running " << billingCycle
                 << " billing (attempt " << attempt << "/" << maxRetries << ")";

        Json::Value result;
        if (billingCycle == "monthly") {
            result = BillingEngine::runMonthlyBilling();
        } else {
            result = BillingEngine::runYearlyBilling();
        }

        int errorCount = result["errors"].size();
        if (errorCount == 0) {
            LOG_INFO << "[BillingCron] " << billingCycle
                     << " billing succeeded: "
                     << result["subscriptions_billed"].asInt()
                     << " subscriptions billed, total="
                     << result["total_amount"].asString();
            return; // success
        }

        LOG_WARN << "[BillingCron] " << billingCycle
                 << " billing attempt " << attempt
                 << " had " << errorCount << " error(s)";

        // If this was the last attempt, log failure and return
        if (attempt >= maxRetries) {
            LOG_ERROR << "[BillingCron] " << billingCycle
                      << " billing failed after " << maxRetries << " attempts";
            return;
        }

        // Wait before retrying
        LOG_INFO << "[BillingCron] Retrying " << billingCycle
                 << " billing in " << (retryDelayMs / 1000) << " seconds...";
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Billing Day Check
// ═══════════════════════════════════════════════════════════════════════════

bool BillingCron::isBillingDay() {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::gmtime(&t);

    // Check if today is the 1st of the month
    return (tm->tm_mday == 1);
}

} // namespace idc
