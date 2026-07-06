#pragma once

#include "zjmf_adapter.h"

#include <atomic>
#include <functional>
#include <json/json.h>
#include <mutex>
#include <string>
#include <unordered_map>

namespace idc {

/// Sync orchestration service for ZJMF integrations.
///
/// Responsibilities:
///   - Run periodic syncs for all active connections
///   - Retry logic: 3 attempts with exponential backoff (1s, 2s, 4s)
///   - Incremental sync: compare updated_at timestamps
///   - sync_version tracking for race condition prevention
///   - Webhook-first, poll-as-fallback strategy
///   - Sync logging to zjmf_sync_logs
class ZJMFSyncService {
public:
    /// ── Configuration ────────────────────────────────────────────────────────

    static constexpr int kMaxRetries       = 3;
    static constexpr int kBackoffMs[] = { 1000, 2000, 4000 };

    // ── Periodic syncs ───────────────────────────────────────────────────────

    /// Run product sync for all active upstream v10 connections.
    /// Called by cron every 5 minutes.
    static void runProductSync();

    /// Run inventory sync for all active upstream v10 connections.
    /// Called by cron every 1 minute.
    static void runInventorySync();

    /// Run client sync for all active upstream finance connections.
    /// Called by cron every 10 minutes.
    static void runClientSync();

    // ── Manual triggers ──────────────────────────────────────────────────────

    /// Trigger a manual sync for a specific connection and entity type.
    /// @param connectionId Connection to sync
    /// @param entityType   "product" | "inventory" | "client" | "invoice"
    /// @return SyncResult
    static SyncResult triggerSync(int64_t connectionId,
                                  const std::string& entityType);

    // ── Version tracking ─────────────────────────────────────────────────────

    /// Get the current sync version for a connection+entity combination.
    /// Returns 0 if no sync has been performed.
    static int64_t getSyncVersion(int64_t connectionId,
                                  const std::string& entityType);

    /// Increment the sync version.
    static int64_t incrementSyncVersion(int64_t connectionId,
                                        const std::string& entityType);

    /// Check if a sync is already in progress for this connection+entity.
    /// Uses an atomic flag to prevent concurrent syncs of the same type.
    static bool isSyncInProgress(int64_t connectionId,
                                 const std::string& entityType);

    /// Get the last sync timestamp for incremental sync.
    static std::string getLastSyncTimestamp(int64_t connectionId,
                                            const std::string& entityType);

private:
    // ── Sync flags (prevent concurrent runs for same connection+entity) ─────
    // Key: "connectionId:entityType" → in_progress flag
    static std::mutex syncFlagsMutex_;
    static std::unordered_map<std::string, bool> syncInProgress_;

    // ── Helpers ──────────────────────────────────────────────────────────────

    /// Execute a sync with retry logic and version tracking.
    /// @param adapter     The adapter to use
    /// @param entityType  Entity type for logging
    /// @param syncFn      The actual sync function to call
    static SyncResult executeWithRetry(
        std::shared_ptr<ZJMFAdapter> adapter,
        const std::string& entityType,
        std::function<SyncResult(ZJMFAdapter&)> syncFn);

    /// Build the sync flag key.
    static std::string syncKey(int64_t connectionId,
                               const std::string& entityType);
};

} // namespace idc
