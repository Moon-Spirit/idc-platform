#include "zjmf_sync_cron.h"
#include "services/zjmf_sync_service.h"
#include "utils/logger.h"

#include <drogon/HttpAppFramework.h>

namespace idc {

bool ZJMFSyncCron::initialized_ = false;

// ============================================================================
//  Initialization
// ============================================================================

void ZJMFSyncCron::init() {
    if (initialized_) {
        LOG_WARN << "[ZJMFSyncCron] Already initialized — ignoring";
        return;
    }
    initialized_ = true;

    auto loop = drogon::app().getLoop();

    // ── Product sync: every 300 seconds (5 min) ──────────────────────────────
    // First run after 30 seconds startup delay
    loop->runAfter(30, []() { onProductSync(); });
    loop->runEvery(300, []() { onProductSync(); });
    LOG_INFO << "[ZJMFSyncCron] Product sync registered (every 5 min)";

    // ── Inventory sync: every 60 seconds (1 min) ────────────────────────────
    // First run after 60 seconds startup delay
    loop->runAfter(60, []() { onInventorySync(); });
    loop->runEvery(60, []() { onInventorySync(); });
    LOG_INFO << "[ZJMFSyncCron] Inventory sync registered (every 1 min)";

    // ── Client sync: every 600 seconds (10 min) ─────────────────────────────
    // First run after 120 seconds startup delay
    loop->runAfter(120, []() { onClientSync(); });
    loop->runEvery(600, []() { onClientSync(); });
    LOG_INFO << "[ZJMFSyncCron] Client sync registered (every 10 min)";
}

// ============================================================================
//  Sync runners
// ============================================================================

void ZJMFSyncCron::onProductSync() {
    LOG_INFO << "[ZJMFSyncCron] Running product sync";
    try {
        ZJMFSyncService::runProductSync();
    } catch (const std::exception& e) {
        LOG_ERROR << "[ZJMFSyncCron] Product sync error: " << e.what();
    }
}

void ZJMFSyncCron::onInventorySync() {
    LOG_INFO << "[ZJMFSyncCron] Running inventory sync";
    try {
        ZJMFSyncService::runInventorySync();
    } catch (const std::exception& e) {
        LOG_ERROR << "[ZJMFSyncCron] Inventory sync error: " << e.what();
    }
}

void ZJMFSyncCron::onClientSync() {
    LOG_INFO << "[ZJMFSyncCron] Running client sync";
    try {
        ZJMFSyncService::runClientSync();
    } catch (const std::exception& e) {
        LOG_ERROR << "[ZJMFSyncCron] Client sync error: " << e.what();
    }
}

} // namespace idc
