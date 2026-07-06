#include "zjmf_sync_service.h"
#include "zjmf_adapter.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>

#include <chrono>
#include <thread>

namespace idc {

// ============================================================================
//  Static members
// ============================================================================

std::mutex ZJMFSyncService::syncFlagsMutex_;
std::unordered_map<std::string, bool> ZJMFSyncService::syncInProgress_;

constexpr int ZJMFSyncService::kBackoffMs[];

// ============================================================================
//  Periodic syncs
// ============================================================================

void ZJMFSyncService::runProductSync() {
    LOG_INFO << "[ZJMFSyncService] Starting periodic product sync";

    auto connections = ZJMFConnectionManager::getActiveConnections();
    for (const auto& conn : connections) {
        if (conn.type != "v10" || conn.direction != "upstream") continue;

        auto adapter = ZJMFConnectionManager::getAdapter(conn.id);
        if (!adapter) continue;

        // Incremental sync: check if there's been a recent sync
        std::string lastSync = getLastSyncTimestamp(conn.id, "product");
        if (!lastSync.empty()) {
            LOG_DEBUG << "[ZJMFSyncService] Last product sync for connection "
                      << conn.id << ": " << lastSync;
            // ZJMF v10 API supports ?updated_since parameter for incremental sync
            // The adapter handles this internally
        }

        executeWithRetry(adapter, "product",
            [](ZJMFAdapter& a) { return a.syncProducts(); });
    }

    LOG_INFO << "[ZJMFSyncService] Periodic product sync complete";
}

void ZJMFSyncService::runInventorySync() {
    LOG_INFO << "[ZJMFSyncService] Starting periodic inventory sync";

    auto connections = ZJMFConnectionManager::getActiveConnections();
    for (const auto& conn : connections) {
        if (conn.type != "v10" || conn.direction != "upstream") continue;

        auto adapter = ZJMFConnectionManager::getAdapter(conn.id);
        if (!adapter) continue;

        executeWithRetry(adapter, "inventory",
            [](ZJMFAdapter& a) { return a.syncInventory(); });
    }

    LOG_INFO << "[ZJMFSyncService] Periodic inventory sync complete";
}

void ZJMFSyncService::runClientSync() {
    LOG_INFO << "[ZJMFSyncService] Starting periodic client sync";

    auto connections = ZJMFConnectionManager::getActiveConnections();
    for (const auto& conn : connections) {
        if (conn.type != "finance" || conn.direction != "upstream") continue;

        auto adapter = ZJMFConnectionManager::getAdapter(conn.id);
        if (!adapter) continue;

        executeWithRetry(adapter, "client",
            [](ZJMFAdapter& a) { return a.syncClients(); });

        // Also sync invoices from finance
        executeWithRetry(adapter, "invoice",
            [](ZJMFAdapter& a) { return a.syncInvoices(); });
    }

    LOG_INFO << "[ZJMFSyncService] Periodic client sync complete";
}

// ============================================================================
//  Manual trigger
// ============================================================================

SyncResult ZJMFSyncService::triggerSync(int64_t connectionId,
                                        const std::string& entityType) {
    auto adapter = ZJMFConnectionManager::getAdapter(connectionId);
    if (!adapter) {
        SyncResult r;
        r.success = false;
        r.errorMessage = "Connection not found or inactive: " +
                         std::to_string(connectionId);
        return r;
    }

    // Check if a sync is already in progress
    if (isSyncInProgress(connectionId, entityType)) {
        SyncResult r;
        r.success = false;
        r.errorMessage = "Sync already in progress for connection " +
                         std::to_string(connectionId) + "/" + entityType;
        return r;
    }

    LOG_INFO << "[ZJMFSyncService] Manual sync triggered: connection="
             << connectionId << " entity=" << entityType;

    if (entityType == "product") {
        return executeWithRetry(adapter, entityType,
            [](ZJMFAdapter& a) { return a.syncProducts(); });
    } else if (entityType == "inventory") {
        return executeWithRetry(adapter, entityType,
            [](ZJMFAdapter& a) { return a.syncInventory(); });
    } else if (entityType == "client") {
        return executeWithRetry(adapter, entityType,
            [](ZJMFAdapter& a) { return a.syncClients(); });
    } else if (entityType == "invoice") {
        return executeWithRetry(adapter, entityType,
            [](ZJMFAdapter& a) { return a.syncInvoices(); });
    } else {
        SyncResult r;
        r.success = false;
        r.errorMessage = "Unknown entity type: " + entityType;
        return r;
    }
}

// ============================================================================
//  Version tracking
// ============================================================================

int64_t ZJMFSyncService::getSyncVersion(int64_t connectionId,
                                         const std::string& entityType) {
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "SELECT COALESCE(MAX(sync_version), 0) FROM zjmf_sync_logs "
            "WHERE connection_id = $1 AND entity_type = $2 AND status = 'success'",
            connectionId, entityType);
        if (!result.empty()) {
            return result[0][0].as<int64_t>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[ZJMFSyncService] getSyncVersion error: " << e.what();
    }
    return 0;
}

int64_t ZJMFSyncService::incrementSyncVersion(int64_t connectionId,
                                               const std::string& entityType) {
    // We use the sync log ID as the version marker
    return getSyncVersion(connectionId, entityType) + 1;
}

bool ZJMFSyncService::isSyncInProgress(int64_t connectionId,
                                        const std::string& entityType) {
    std::lock_guard<std::mutex> lock(syncFlagsMutex_);
    auto key = syncKey(connectionId, entityType);
    return syncInProgress_[key];
}

std::string ZJMFSyncService::getLastSyncTimestamp(
    int64_t connectionId, const std::string& entityType) {
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "SELECT MAX(created_at) FROM zjmf_sync_logs "
            "WHERE connection_id = $1 AND entity_type = $2 AND status = 'success'",
            connectionId, entityType);
        if (!result.empty() && !result[0][0].isNull()) {
            return result[0][0].as<std::string>();
        }
    } catch (...) {}
    return "";
}

// ============================================================================
//  Private helpers
// ============================================================================

SyncResult ZJMFSyncService::executeWithRetry(
    std::shared_ptr<ZJMFAdapter> adapter,
    const std::string& entityType,
    std::function<SyncResult(ZJMFAdapter&)> syncFn) {

    int64_t connId = adapter->connection().id;
    std::string key = syncKey(connId, entityType);

    // Mark sync as in progress
    {
        std::lock_guard<std::mutex> lock(syncFlagsMutex_);
        syncInProgress_[key] = true;
    }

    SyncResult result;
    for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
        LOG_INFO << "[ZJMFSyncService] " << entityType << " sync for connection "
                 << connId << " (attempt " << attempt << "/" << kMaxRetries << ")";

        try {
            result = syncFn(*adapter);
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = e.what();
        }

        if (result.success) {
            LOG_INFO << "[ZJMFSyncService] " << entityType << " sync succeeded "
                     << "for connection " << connId;
            break;
        }

        LOG_WARN << "[ZJMFSyncService] " << entityType << " sync attempt "
                 << attempt << " failed: " << result.errorMessage;

        if (attempt >= kMaxRetries) {
            LOG_ERROR << "[ZJMFSyncService] " << entityType << " sync failed "
                      << "after " << kMaxRetries << " attempts for connection "
                      << connId;
            break;
        }

        // Exponential backoff
        int delayMs = kBackoffMs[attempt - 1];
        LOG_INFO << "[ZJMFSyncService] Retrying in " << delayMs << "ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    // Mark sync as complete
    {
        std::lock_guard<std::mutex> lock(syncFlagsMutex_);
        syncInProgress_[key] = false;
    }

    return result;
}

std::string ZJMFSyncService::syncKey(int64_t connectionId,
                                      const std::string& entityType) {
    return std::to_string(connectionId) + ":" + entityType;
}

} // namespace idc
