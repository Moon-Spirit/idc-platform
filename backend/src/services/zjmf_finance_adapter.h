#pragma once

#include "zjmf_adapter.h"
#include "utils/http_client.h"

#include <json/json.h>
#include <memory>
#include <string>

namespace idc {

/// ZJMF 魔方财务 (Finance) API adapter.
///
/// Handles upstream client/invoice sync and downstream invoice/payment push.
///
/// API endpoints (per C.4):
///   GET  /api/clients         — list clients
///   GET  /api/invoices        — list invoices
///   GET  /api/transactions    — list transactions
///   POST /api/clients         — create client
///   POST /api/invoices        — create invoice
///   GET  /api/invoices/:id    — query invoice
///   POST /api/transactions    — record payment
class ZJMFFinanceAdapter : public ZJMFAdapter {
public:
    ZJMFFinanceAdapter(const ZJMFConnection& connection,
                       std::string apiKey,
                       std::string apiSecret,
                       bool mockMode);

    ~ZJMFFinanceAdapter() override = default;

    // ── ZJMFAdapter interface ────────────────────────────────────────────────

    std::string type() const override { return "finance"; }
    const ZJMFConnection& connection() const override { return connection_; }

    // ── Sync ─────────────────────────────────────────────────────────────────

    SyncResult syncProducts() override {
        SyncResult r; r.success = true; return r;
    }
    SyncResult syncInventory() override {
        SyncResult r; r.success = true; return r;
    }
    SyncResult syncClients() override;
    SyncResult syncInvoices() override;

    // ── Provisioning (not applicable for finance) ────────────────────────────

    SyncResult createServer(const Json::Value&) override {
        SyncResult r; r.success = true; return r;
    }
    Json::Value queryServerStatus(const std::string&) override {
        return Json::nullValue;
    }
    SyncResult suspendServer(const std::string&) override {
        SyncResult r; r.success = true; return r;
    }
    SyncResult activateServer(const std::string&) override {
        SyncResult r; r.success = true; return r;
    }
    SyncResult terminateServer(const std::string&) override {
        SyncResult r; r.success = true; return r;
    }

    // ── Finance ──────────────────────────────────────────────────────────────

    SyncResult createInvoice(const Json::Value& invoiceData) override;
    Json::Value queryInvoice(const std::string& remoteId) override;
    SyncResult recordPayment(const Json::Value& transactionData) override;

    // ── Helpers ──────────────────────────────────────────────────────────────

    /// Test connectivity by fetching a single client.
    HttpResult testConnection();

private:
    ZJMFConnection connection_;
    std::unique_ptr<HttpClient> http_;

    /// Upsert a client (distributor) from finance data into local distributors table.
    static int64_t upsertClient(const Json::Value& client);

    /// Upsert an invoice record locally.
    static int64_t upsertInvoice(const Json::Value& invoice);
};

} // namespace idc
