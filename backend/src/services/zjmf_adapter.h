#pragma once

#include <json/json.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace idc {

// ── Forward declarations ─────────────────────────────────────────────────────

class HttpClient;
class ZJMFV10Adapter;
class ZJMFFinanceAdapter;

/// ─── Connection info struct ──────────────────────────────────────────────────
/// Loaded from the zjmf_connections table.
struct ZJMFConnection {
    int64_t  id           = 0;
    std::string name;
    std::string type;             // "v10" | "finance"
    std::string direction;        // "upstream" | "downstream"
    std::string apiBaseUrl;
    std::string apiKey;           // Already decrypted by ConnectionManager
    std::string apiSecret;        // Already decrypted by ConnectionManager
    int     syncInterval   = 300; // seconds
    int     status         = 1;   // 1=active
    std::string lastSyncAt;
    std::string createdAt;
    std::string updatedAt;
};

/// ─── Sync result ─────────────────────────────────────────────────────────────
struct SyncResult {
    bool    success      = false;
    int     itemsSynced  = 0;
    int     itemsCreated = 0;
    int     itemsUpdated = 0;
    int     itemsError   = 0;
    std::string errorMessage;
    int64_t syncLogId    = 0;     // ID in zjmf_sync_logs
};

/// ─── Sync status for dashboard ───────────────────────────────────────────────
struct SyncStatus {
    int64_t  connectionId;
    std::string connectionName;
    std::string type;
    std::string direction;
    std::string lastSyncAt;
    std::string status;            // "idle" | "syncing" | "error"
    std::string lastError;
    int     pendingItems = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
//  ZJMFAdapter — Abstract base class
// ═══════════════════════════════════════════════════════════════════════════

class ZJMFAdapter {
public:
    virtual ~ZJMFAdapter() = default;

    /// Return the adapter type ("v10" or "finance").
    virtual std::string type() const = 0;

    /// Return the connection info this adapter was initialized with.
    virtual const ZJMFConnection& connection() const = 0;

    /// ── Sync methods ────────────────────────────────────────────────────────

    /// Sync products from upstream. Only applicable for v10 upstream.
    /// For finance adapters, this is a no-op.
    virtual SyncResult syncProducts() = 0;

    /// Sync inventory from upstream. Only applicable for v10 upstream.
    virtual SyncResult syncInventory() = 0;

    /// Sync clients from upstream. Only applicable for finance upstream.
    virtual SyncResult syncClients() = 0;

    /// Sync invoices from upstream. Only applicable for finance upstream.
    virtual SyncResult syncInvoices() = 0;

    /// ── Provisioning methods (v10) ──────────────────────────────────────────

    /// Create a server (provisioning request).
    /// @param orderData JSON with client_id, product_id, hostname, etc.
    /// @return SyncResult with remote_id in itemsCreated if successful.
    virtual SyncResult createServer(const Json::Value& orderData) = 0;

    /// Query the provisioning status of a server.
    virtual Json::Value queryServerStatus(const std::string& remoteId) = 0;

    /// Suspend a server.
    virtual SyncResult suspendServer(const std::string& remoteId) = 0;

    /// Activate a suspended server.
    virtual SyncResult activateServer(const std::string& remoteId) = 0;

    /// Terminate (delete) a server.
    virtual SyncResult terminateServer(const std::string& remoteId) = 0;

    /// ── Finance methods ─────────────────────────────────────────────────────

    /// Create an invoice in the downstream finance system.
    virtual SyncResult createInvoice(const Json::Value& invoiceData) = 0;

    /// Query invoice status.
    virtual Json::Value queryInvoice(const std::string& remoteId) = 0;

    /// Record a payment transaction.
    virtual SyncResult recordPayment(const Json::Value& transactionData) = 0;

protected:
    /// Log a sync action to zjmf_sync_logs.
    /// @return The sync_log_id.
    static int64_t logSync(
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
        int retryCount);
};

// ═══════════════════════════════════════════════════════════════════════════
//  ZJMFConnectionManager — Factory + connection lifecycle
// ═══════════════════════════════════════════════════════════════════════════

class ZJMFConnectionManager {
public:
    /// Initialize the connection manager. Loads mock config.
    static void init();

    /// Get an adapter for the given connection ID.
    /// Decrypts API keys, creates the appropriate adapter type.
    /// Returns nullptr if connection not found or inactive.
    /// Caches adapters for reuse.
    static std::shared_ptr<ZJMFAdapter> getAdapter(int64_t connectionId);

    /// Get all active connections.
    static std::vector<ZJMFConnection> getActiveConnections();

    /// Get a connection by ID.
    static std::optional<ZJMFConnection> getConnection(int64_t connectionId);

    /// Test a connection by making a simple API call.
    static bool testConnection(int64_t connectionId);

    /// Get sync status for all connections (for dashboard).
    static std::vector<SyncStatus> getSyncStatus();

    /// Trigger a manual sync for a connection and entity type.
    /// @param connectionId Connection to sync
    /// @param entityType   "product" | "inventory" | "client" | "invoice"
    static SyncResult triggerSync(int64_t connectionId,
                                  const std::string& entityType);

    /// Check whether mock mode is enabled.
    static bool isMockMode();

    /// Clear cached adapters (e.g. after connection config update).
    static void clearCache();

private:
    static bool initialized_;

    // Cached adapters: connection_id -> adapter
    static std::unordered_map<int64_t, std::shared_ptr<ZJMFAdapter>> adapters_;
    static std::mutex mutex_;

    /// Load a connection row from DB.
    static std::optional<ZJMFConnection> loadConnection(int64_t id);
};

} // namespace idc
