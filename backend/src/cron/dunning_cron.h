#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>
#include <functional>

namespace idc {

/// Dunning (催缴) cron job scheduler.
///
/// Registers a daily timer (every 86400 seconds) on the Drogon event loop.
/// On each tick, triggers DunningService::runDailyDunning() to check
/// overdue invoices and apply reminders / suspensions / terminations.
///
/// Usage: Call DunningCron::init() during application startup,
///        before drogon::app().run().
class DunningCron {
public:
    /// Register the daily dunning check on the Drogon event loop.
    /// Safe to call once — subsequent calls are no-ops.
    static void init();

private:
    static bool initialized_;

    /// The daily check function invoked by the timer.
    static void onDailyTick();
};

} // namespace idc
