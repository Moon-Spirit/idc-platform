#pragma once

#include "zjmf_adapter.h"
#include "utils/http_client.h"

#include <json/json.h>
#include <memory>
#include <string>

namespace idc {

/// ZJMF 魔方v10 (CBAP) API adapter.
///
/// Handles upstream product/inventory sync and downstream server provisioning.
///
/// API endpoints (per C.3):
///   GET  /api/v1/products     — list products
///   GET  /api/v1/inventory    — list inventory
///   GET  /api/servers         — list servers
///   GET  /api/ips             — list IP pool
///   POST /api/servers         — create server (provisioning)
///   GET  /api/servers/:id     — query server status
///   PUT  /api/servers/:id/suspend    — suspend
///   PUT  /api/servers/:id/activate   — activate
///   DELETE /api/servers/:id          — terminate
class ZJMFV10Adapter : public ZJMFAdapter {
public:
    /// Construct a v10 adapter for the given connection.
    /// @param connection  The ZJMF connection configuration
    /// @param apiKey      Decrypted API key
    /// @param apiSecret   Decrypted API secret
    /// @param mockMode    Enable mock responses
    ZJMFV10Adapter(const ZJMFConnection& connection,
                   std::string apiKey,
                   std::string apiSecret,
                   bool mockMode);

    ~ZJMFV10Adapter() override = default;

    // ── ZJMFAdapter interface ────────────────────────────────────────────────

    std::string type() const override { return "v10"; }
    const ZJMFConnection& connection() const override { return connection_; }

    // ── Sync ─────────────────────────────────────────────────────────────────

    SyncResult syncProducts() override;
    SyncResult syncInventory() override;

    // Clients/invoices are not applicable for v10
    SyncResult syncClients() override {
        SyncResult r; r.success = true; return r;
    }
    SyncResult syncInvoices() override {
        SyncResult r; r.success = true; return r;
    }

    // ── Provisioning ─────────────────────────────────────────────────────────

    SyncResult createServer(const Json::Value& orderData) override;
    Json::Value queryServerStatus(const std::string& remoteId) override;
    SyncResult suspendServer(const std::string& remoteId) override;
    SyncResult activateServer(const std::string& remoteId) override;
    SyncResult terminateServer(const std::string& remoteId) override;

    // ── Finance (not applicable) ─────────────────────────────────────────────

    SyncResult createInvoice(const Json::Value&) override {
        SyncResult r; r.success = true; return r;
    }
    Json::Value queryInvoice(const std::string&) override {
        return Json::nullValue;
    }
    SyncResult recordPayment(const Json::Value&) override {
        SyncResult r; r.success = true; return r;
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    /// Test connectivity by fetching a single product.
    HttpResult testConnection();

private:
    ZJMFConnection connection_;
    std::unique_ptr<HttpClient> http_;

    /// Upsert a product from ZJMF data into the local products table.
    /// Returns the local product ID.
    int64_t upsertProduct(const Json::Value& product);
};

} // namespace idc
