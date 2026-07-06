#include "zjmf_adapter.h"
#include "zjmf_v10_adapter.h"
#include "zjmf_finance_adapter.h"
#include "zjmf_sync_service.h"
#include "utils/db_client.h"
#include "utils/crypto.h"
#include "utils/http_client.h"
#include "utils/logger.h"
#include "utils/rate_limiter.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <json/json.h>

namespace idc {

// ============================================================================
//  Static members
// ============================================================================

bool ZJMFConnectionManager::initialized_ = false;
std::unordered_map<int64_t, std::shared_ptr<ZJMFAdapter>>
    ZJMFConnectionManager::adapters_;
std::mutex ZJMFConnectionManager::mutex_;

// ============================================================================
//  ZJMFAdapter — base sync logger
// ============================================================================

int64_t ZJMFAdapter::logSync(
    int64_t connectionId,
    const std::string& direction,
    const std::string& entityType,
    int64_t entityId,
    const std::string& remoteId,
    const std::string& action,
    const Json::Value& requestData,
    const Json::Value& responseData,
    const std::string& status,
    const std::string& errorMessage,
    int retryCount) {

    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "INSERT INTO zjmf_sync_logs "
            "(connection_id, direction, entity_type, entity_id, remote_id, "
            " action, request_data, response_data, status, error_message, retry_count) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb, $8::jsonb, $9, $10, $11) "
            "RETURNING id",
            connectionId, direction, entityType,
            entityId, remoteId, action,
            Json::FastWriter().write(requestData),
            Json::FastWriter().write(responseData),
            status, errorMessage, retryCount);

        if (!result.empty()) {
            return result[0]["id"].as<int64_t>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[ZJMFAdapter] Failed to log sync: " << e.what();
    }
    return 0;
}

// ============================================================================
//  ZJMFConnectionManager
// ============================================================================

void ZJMFConnectionManager::init() {
    if (initialized_) {
        LOG_WARN << "[ZJMFConnectionManager] Already initialized";
        return;
    }
    initialized_ = true;

    if (isMockMode()) {
        LOG_INFO << "[ZJMFConnectionManager] Running in MOCK mode";
    }

    LOG_INFO << "[ZJMFConnectionManager] Initialized";
}

std::shared_ptr<ZJMFAdapter> ZJMFConnectionManager::getAdapter(
    int64_t connectionId) {
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = adapters_.find(connectionId);
        if (it != adapters_.end()) {
            return it->second;
        }
    }

    // Load connection from DB
    auto conn = loadConnection(connectionId);
    if (!conn.has_value()) {
        LOG_ERROR << "[ZJMFConnectionManager] Connection not found: id="
                  << connectionId;
        return nullptr;
    }

    if (conn->status != 1) {
        LOG_WARN << "[ZJMFConnectionManager] Connection " << connectionId
                 << " is disabled (status=" << conn->status << ")";
        return nullptr;
    }

    // Decrypt API credentials
    auto decryptedKey = CryptoUtil::aesDecrypt(conn->apiKey);
    if (!decryptedKey.has_value()) {
        LOG_ERROR << "[ZJMFConnectionManager] Failed to decrypt API key for "
                  << "connection " << connectionId;
        return nullptr;
    }

    std::string decryptedSecret;
    if (!conn->apiSecret.empty()) {
        auto ds = CryptoUtil::aesDecrypt(conn->apiSecret);
        if (ds.has_value()) decryptedSecret = *ds;
    }

    // Create the appropriate adapter
    std::shared_ptr<ZJMFAdapter> adapter;
    bool mockMode = isMockMode();

    if (conn->type == "v10") {
        adapter = std::make_shared<ZJMFV10Adapter>(
            *conn, *decryptedKey, decryptedSecret, mockMode);
    } else if (conn->type == "finance") {
        adapter = std::make_shared<ZJMFFinanceAdapter>(
            *conn, *decryptedKey, decryptedSecret, mockMode);
    } else {
        LOG_ERROR << "[ZJMFConnectionManager] Unknown type: " << conn->type;
        return nullptr;
    }

    // Configure rate limiter
    RateLimiter::instance().setRate(
        connectionId,
        RateLimiter::kDefaultRate,
        RateLimiter::kDefaultBurst);

    // Cache and return
    {
        std::lock_guard<std::mutex> lock(mutex_);
        adapters_[connectionId] = adapter;
    }

    LOG_INFO << "[ZJMFConnectionManager] Created adapter for connection "
             << connectionId << " (" << conn->type << "/" << conn->direction << ")";
    return adapter;
}

std::vector<ZJMFConnection> ZJMFConnectionManager::getActiveConnections() {
    std::vector<ZJMFConnection> connections;
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "SELECT id, name, type, direction, api_base_url, "
            "api_key, api_secret, sync_interval, status, "
            "last_sync_at, created_at, updated_at "
            "FROM zjmf_connections WHERE status = 1 "
            "ORDER BY id");

        for (const auto& row : result) {
            ZJMFConnection conn;
            conn.id           = row["id"].as<int64_t>();
            conn.name         = row["name"].as<std::string>();
            conn.type         = row["type"].as<std::string>();
            conn.direction    = row["direction"].as<std::string>();
            conn.apiBaseUrl   = row["api_base_url"].as<std::string>();
            conn.apiKey       = row["api_key"].as<std::string>();
            conn.apiSecret    = row["api_secret"].as<std::string>();
            conn.syncInterval = row["sync_interval"].as<int>();
            conn.status       = row["status"].as<int16_t>();

            auto lastSync = row["last_sync_at"];
            conn.lastSyncAt = lastSync.isNull() ? "" : lastSync.as<std::string>();
            conn.createdAt  = row["created_at"].as<std::string>();
            conn.updatedAt  = row["updated_at"].as<std::string>();
            connections.push_back(std::move(conn));
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[ZJMFConnectionManager] Failed to load connections: "
                  << e.what();
    }
    return connections;
}

std::optional<ZJMFConnection> ZJMFConnectionManager::getConnection(
    int64_t connectionId) {
    return loadConnection(connectionId);
}

bool ZJMFConnectionManager::testConnection(int64_t connectionId) {
    auto adapter = getAdapter(connectionId);
    if (!adapter) return false;

    // For v10: try GET /api/v1/products?per_page=1
    // For finance: try GET /api/clients?limit=1
    try {
        if (adapter->type() == "v10") {
            auto v10 = std::dynamic_pointer_cast<ZJMFV10Adapter>(adapter);
            if (v10) {
                // Use the HTTP client directly
                auto result = v10->testConnection();
                return result.ok;
            }
        } else {
            auto fin = std::dynamic_pointer_cast<ZJMFFinanceAdapter>(adapter);
            if (fin) {
                auto result = fin->testConnection();
                return result.ok;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[ZJMFConnectionManager] testConnection failed for "
                  << connectionId << ": " << e.what();
    }
    return false;
}

std::vector<SyncStatus> ZJMFConnectionManager::getSyncStatus() {
    std::vector<SyncStatus> statuses;
    auto connections = getActiveConnections();

    for (const auto& conn : connections) {
        SyncStatus st;
        st.connectionId    = conn.id;
        st.connectionName  = conn.name;
        st.type            = conn.type;
        st.direction       = conn.direction;
        st.lastSyncAt      = conn.lastSyncAt;
        st.status          = "idle";
        st.lastError       = "";
        st.pendingItems    = 0;

        // Check last sync log for errors
        try {
            auto db = DbClient::getClient();
            auto logResult = db->execSqlSync(
                "SELECT status, error_message FROM zjmf_sync_logs "
                "WHERE connection_id = $1 ORDER BY id DESC LIMIT 1",
                conn.id);

            if (!logResult.empty()) {
                auto row = logResult[0];
                std::string logStatus = row["status"].as<std::string>();
                if (logStatus == "failed") {
                    st.status = "error";
                    st.lastError = row["error_message"].as<std::string>();
                }
            }
        } catch (...) {
            // Ignore DB errors in status collection
        }

        statuses.push_back(std::move(st));
    }
    return statuses;
}

SyncResult ZJMFConnectionManager::triggerSync(
    int64_t connectionId, const std::string& entityType) {
    return ZJMFSyncService::triggerSync(connectionId, entityType);
}

bool ZJMFConnectionManager::isMockMode() {
    try {
        auto& config = drogon::app().getCustomConfig();
        if (config.isMember("zjmf") && config["zjmf"].isMember("mock")) {
            return config["zjmf"]["mock"].asBool();
        }
    } catch (...) {}
    return false;
}

void ZJMFConnectionManager::clearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    adapters_.clear();
    LOG_INFO << "[ZJMFConnectionManager] Adapter cache cleared";
}

std::optional<ZJMFConnection> ZJMFConnectionManager::loadConnection(
    int64_t id) {
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "SELECT id, name, type, direction, api_base_url, "
            "api_key, api_secret, sync_interval, status, "
            "last_sync_at, created_at, updated_at "
            "FROM zjmf_connections WHERE id = $1", id);

        if (result.empty()) return std::nullopt;

        auto row = result[0];
        ZJMFConnection conn;
        conn.id           = row["id"].as<int64_t>();
        conn.name         = row["name"].as<std::string>();
        conn.type         = row["type"].as<std::string>();
        conn.direction    = row["direction"].as<std::string>();
        conn.apiBaseUrl   = row["api_base_url"].as<std::string>();
        conn.apiKey       = row["api_key"].as<std::string>();
        conn.apiSecret    = row["api_secret"].as<std::string>();
        conn.syncInterval = row["sync_interval"].as<int>();
        conn.status       = row["status"].as<int16_t>();

        auto lastSync = row["last_sync_at"];
        conn.lastSyncAt = lastSync.isNull() ? "" : lastSync.as<std::string>();
        conn.createdAt  = row["created_at"].as<std::string>();
        conn.updatedAt  = row["updated_at"].as<std::string>();
        return conn;
    } catch (const std::exception& e) {
        LOG_ERROR << "[ZJMFConnectionManager] loadConnection error: " << e.what();
        return std::nullopt;
    }
}

} // namespace idc
