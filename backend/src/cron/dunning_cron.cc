#include "dunning_cron.h"
#include "services/dunning_service.h"
#include "utils/logger.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>

#include <ctime>
#include <string>

namespace idc {

bool DunningCron::initialized_ = false;

// ═══════════════════════════════════════════════════════════════════════════
//  Initialization
// ═══════════════════════════════════════════════════════════════════════════

void DunningCron::init() {
    if (initialized_) {
        LOG_WARN << "[DunningCron] Already initialized — ignoring duplicate call";
        return;
    }
    initialized_ = true;

    LOG_INFO << "[DunningCron] Registering daily dunning check (86400s interval)";

    // Schedule the first check 2 hours after startup (gives DB time to warm up),
    // then every 86400 seconds (24 hours) thereafter.
    auto loop = drogon::app().getLoop();
    loop->runAfter(7200, []() {
        onDailyTick();
    });

    loop->runEvery(86400, []() {
        onDailyTick();
    });

    LOG_INFO << "[DunningCron] Daily dunning cron registered";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Daily Tick
// ═══════════════════════════════════════════════════════════════════════════

void DunningCron::onDailyTick() {
    LOG_INFO << "[DunningCron] Running daily dunning check";

    auto result = DunningService::runDailyDunning();

    LOG_INFO << "[DunningCron] Daily dunning complete: "
             << "upcoming_reminders=" << result["upcoming_reminders"].asInt()
             << " overdue_marked=" << result["overdue_marked"].asInt()
             << " overdue_reminders=" << result["overdue_reminders"].asInt()
             << " auto_suspended=" << result["auto_suspended"].asInt()
             << " auto_terminated=" << result["auto_terminated"].asInt();
}

} // namespace idc
