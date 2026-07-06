#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>

namespace idc {

/// ZJMF sync cron job scheduler.
///
/// Registers three periodic timers on the Drogon event loop:
///   - Product sync:  every 5 minutes  (300 seconds)
///   - Inventory sync: every 1 minute  (60 seconds)
///   - Client sync:   every 10 minutes (600 seconds)
///
/// Retry logic is handled by ZJMFSyncService (3 attempts, exp backoff).
/// Each sync runs asynchronously on a separate thread to avoid blocking
/// the event loop.
///
/// Usage: Call ZJMFSyncCron::init() during application startup,
///        before drogon::app().run().
class ZJMFSyncCron {
public:
    /// Register all ZJMF sync timers.
    /// Safe to call once — subsequent calls are no-ops.
    static void init();

private:
    static bool initialized_;

    /// ── Sync runners ─────────────────────────────────────────────────────────

    /// Run product sync for all v10 upstream connections.
    static void onProductSync();

    /// Run inventory sync for all v10 upstream connections.
    static void onInventorySync();

    /// Run client/invoice sync for all finance upstream connections.
    static void onClientSync();
};

} // namespace idc
