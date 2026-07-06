#include "zjmf_finance_adapter.h"
#include "zjmf_adapter.h"
#include "utils/db_client.h"
#include "utils/logger.h"
#include "utils/rate_limiter.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>

#include <json/json.h>

namespace idc {

// ============================================================================
// ── Constructor ───────────────────────────────────────────────────────────────
// ============================================================================

ZJMFFinanceAdapter::ZJMFFinanceAdapter(const ZJMFConnection& connection,
                                       std::string apiKey,
                                       std::string apiSecret,
                                       bool mockMode)
    : connection_(connection) {
    http_ = std::make_unique<HttpClient>(
        connection.apiBaseUrl,
        std::move(apiKey),
        std::move(apiSecret),
        mockMode);

    if (mockMode) {
        // Mock client list
        Json::Value mockClients(Json::arrayValue);
        for (int i = 1; i <= 2; ++i) {
            Json::Value c;
            c["id"]      = 1000 + i;
            c["name"]    = "Mock Distributor " + std::to_string(i);
            c["email"]   = "dist" + std::to_string(i) + "@example.com";
            c["phone"]   = "1380000" + std::to_string(1000 + i);
            c["status"]  = "active";
            c["balance"] = "5000.00";
            mockClients.append(c);
        }
        Json::Value cliResp;
        cliResp["data"]    = mockClients;
        cliResp["success"] = true;
        http_->setMockResponse("GET", "/api/clients", cliResp);

        // Mock invoice list
        Json::Value mockInvoices(Json::arrayValue);
        for (int i = 1; i <= 3; ++i) {
            Json::Value inv;
            inv["id"]         = 2000 + i;
            inv["client_id"]  = 1000 + (i % 2);
            inv["date"]       = "2026-07-01";
            inv["duedate"]    = "2026-07-31";
            inv["total"]      = "800.00";
            inv["status"]     = i == 1 ? "paid" : "unpaid";
            mockInvoices.append(inv);
        }
        Json::Value invResp;
        invResp["data"]    = mockInvoices;
        invResp["success"] = true;
        http_->setMockResponse("GET", "/api/invoices", invResp);

        // Mock invoice create
        Json::Value createInv;
        createInv["success"] = true;
        createInv["data"]["id"] = "inv_mock_001";
        http_->setMockResponse("POST", "/api/invoices", createInv, 201);

        // Mock payment record
        Json::Value payResp;
        payResp["success"] = true;
        payResp["data"]["id"] = "txn_mock_001";
        http_->setMockResponse("POST", "/api/transactions", payResp, 201);

        LOG_INFO << "[ZJMFFinanceAdapter] Mock responses configured";
    }
}

// ============================================================================
// ── testConnection ────────────────────────────────────────────────────────────
// ============================================================================

HttpResult ZJMFFinanceAdapter::testConnection() {
    return http_->get("/api/clients", {{"limit", "1"}});
}

// ============================================================================
// ── syncClients ───────────────────────────────────────────────────────────────
// ============================================================================

SyncResult ZJMFFinanceAdapter::syncClients() {
    SyncResult result;

    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) {
        result.errorMessage = "Rate limit exceeded";
        return result;
    }

    LOG_INFO << "[ZJMFFinanceAdapter] Syncing clients from connection "
             << connection_.id;

    auto httpResult = http_->get("/api/clients");

    if (!httpResult.ok) {
        result.errorMessage = httpResult.error;
        result.syncLogId = logSync(
            connection_.id, "inbound", "client", 0, "",
            "sync", Json::objectValue, httpResult.body,
            "failed", httpResult.error, 1);
        return result;
    }

    Json::Value items;
    if (httpResult.body.isMember("data") && httpResult.body["data"].isArray()) {
        items = httpResult.body["data"];
    } else if (httpResult.body.isArray()) {
        items = httpResult.body;
    } else {
        result.errorMessage = "Unexpected client response format";
        result.syncLogId = logSync(
            connection_.id, "inbound", "client", 0, "",
            "sync", Json::objectValue, httpResult.body,
            "failed", result.errorMessage, 1);
        return result;
    }

    for (const auto& item : items) {
        try {
            int64_t localId = upsertClient(item);

            std::string remoteId = std::to_string(item["id"].asInt64());
            auto db = DbClient::getClient();
            db->execSqlSync(
                "INSERT INTO zjmf_entity_mappings "
                "(local_type, local_id, remote_system, remote_id, remote_data) "
                "VALUES ('distributor', $1, 'zjmf_finance', $2, $3::jsonb) "
                "ON CONFLICT (local_type, local_id, remote_system) "
                "DO UPDATE SET remote_data = $3::jsonb, last_synced_at = NOW()",
                localId, remoteId,
                Json::FastWriter().write(item));

            result.itemsCreated++;
            result.itemsSynced++;
        } catch (const std::exception& e) {
            LOG_ERROR << "[ZJMFFinanceAdapter] Failed to upsert client: " << e.what();
            result.itemsError++;
        }
    }

    result.success = true;
    result.syncLogId = logSync(
        connection_.id, "inbound", "client", 0, "",
        "sync", httpResult.body, httpResult.body,
        "success", "", 1);

    try {
        auto db = DbClient::getClient();
        db->execSqlSync(
            "UPDATE zjmf_connections SET last_sync_at = NOW() WHERE id = $1",
            connection_.id);
    } catch (...) {}

    return result;
}

// ============================================================================
// ── syncInvoices ──────────────────────────────────────────────────────────────
// ============================================================================

SyncResult ZJMFFinanceAdapter::syncInvoices() {
    SyncResult result;

    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) {
        result.errorMessage = "Rate limit exceeded";
        return result;
    }

    LOG_INFO << "[ZJMFFinanceAdapter] Syncing invoices from connection "
             << connection_.id;

    auto httpResult = http_->get("/api/invoices");

    if (!httpResult.ok) {
        result.errorMessage = httpResult.error;
        result.syncLogId = logSync(
            connection_.id, "inbound", "invoice", 0, "",
            "sync", Json::objectValue, httpResult.body,
            "failed", httpResult.error, 1);
        return result;
    }

    Json::Value items;
    if (httpResult.body.isMember("data") && httpResult.body["data"].isArray()) {
        items = httpResult.body["data"];
    } else if (httpResult.body.isArray()) {
        items = httpResult.body;
    } else {
        result.errorMessage = "Unexpected invoice response format";
        result.syncLogId = logSync(
            connection_.id, "inbound", "invoice", 0, "",
            "sync", Json::objectValue, httpResult.body,
            "failed", result.errorMessage, 1);
        return result;
    }

    for (const auto& item : items) {
        try {
            int64_t localId = upsertInvoice(item);

            std::string remoteId = std::to_string(item["id"].asInt64());
            auto db = DbClient::getClient();
            db->execSqlSync(
                "INSERT INTO zjmf_entity_mappings "
                "(local_type, local_id, remote_system, remote_id, remote_data) "
                "VALUES ('invoice', $1, 'zjmf_finance', $2, $3::jsonb) "
                "ON CONFLICT (local_type, local_id, remote_system) "
                "DO UPDATE SET remote_data = $3::jsonb, last_synced_at = NOW()",
                localId, remoteId,
                Json::FastWriter().write(item));

            result.itemsSynced++;
        } catch (const std::exception& e) {
            LOG_ERROR << "[ZJMFFinanceAdapter] Failed to upsert invoice: " << e.what();
            result.itemsError++;
        }
    }

    result.success = true;
    result.syncLogId = logSync(
        connection_.id, "inbound", "invoice", 0, "",
        "sync", httpResult.body, httpResult.body,
        "success", "", 1);

    return result;
}

// ============================================================================
// ── createInvoice ─────────────────────────────────────────────────────────────
// ============================================================================

SyncResult ZJMFFinanceAdapter::createInvoice(const Json::Value& invoiceData) {
    SyncResult result;

    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) {
        result.errorMessage = "Rate limit exceeded";
        return result;
    }

    LOG_INFO << "[ZJMFFinanceAdapter] Creating invoice for connection "
             << connection_.id;

    auto httpResult = http_->post("/api/invoices", invoiceData);

    if (!httpResult.ok) {
        result.errorMessage = httpResult.error;
        result.syncLogId = logSync(
            connection_.id, "outbound", "invoice", 0, "",
            "createInvoice", invoiceData, httpResult.body,
            "failed", httpResult.error, 1);
        return result;
    }

    std::string remoteId;
    if (httpResult.body.isMember("data") &&
        httpResult.body["data"].isMember("id")) {
        remoteId = httpResult.body["data"]["id"].asString();
    } else if (httpResult.body.isMember("id")) {
        remoteId = httpResult.body["id"].asString();
    }

    result.success = true;
    result.itemsCreated = remoteId.empty() ? 0 : 1;
    result.syncLogId = logSync(
        connection_.id, "outbound", "invoice", 0,
        remoteId, "createInvoice", invoiceData, httpResult.body,
        "success", "", 1);

    return result;
}

// ============================================================================
// ── queryInvoice / recordPayment ──────────────────────────────────────────────
// ============================================================================

Json::Value ZJMFFinanceAdapter::queryInvoice(const std::string& remoteId) {
    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume(500)) {
        Json::Value err;
        err["error"] = "rate_limited";
        return err;
    }

    auto result = http_->get("/api/invoices/" + remoteId);
    if (!result.ok) {
        Json::Value err;
        err["error"] = result.error;
        return err;
    }
    return result.body;
}

SyncResult ZJMFFinanceAdapter::recordPayment(const Json::Value& transactionData) {
    SyncResult sr;
    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) { sr.errorMessage = "rate_limited"; return sr; }

    auto result = http_->post("/api/transactions", transactionData);
    sr.success = result.ok;
    sr.syncLogId = logSync(
        connection_.id, "outbound", "payment", 0, "",
        "recordPayment", transactionData, result.body,
        result.ok ? "success" : "failed",
        result.ok ? "" : result.error, 1);
    return sr;
}

// ============================================================================
// ── Private helpers ───────────────────────────────────────────────────────────
// ============================================================================

int64_t ZJMFFinanceAdapter::upsertClient(const Json::Value& client) {
    auto db = DbClient::getClient();

    std::string name  = client.get("name", "Unknown").asString();
    std::string email = client.get("email", "").asString();
    std::string phone = client.get("phone", "").asString();

    // Check if distributor exists via entity mapping
    auto mappingResult = db->execSqlSync(
        "SELECT local_id FROM zjmf_entity_mappings "
        "WHERE remote_system = 'zjmf_finance' AND local_type = 'distributor' "
        "AND remote_id = $1",
        std::to_string(client["id"].asInt64()));

    if (!mappingResult.empty()) {
        int64_t localId = mappingResult[0]["local_id"].as<int64_t>();
        db->execSqlSync(
            "UPDATE distributors SET name=$1, contact_email=$2, "
            "contact_phone=$3, updated_at=NOW() WHERE id=$4",
            name, email, phone, localId);
        return localId;
    }

    // Insert new distributor
    auto insertResult = db->execSqlSync(
        "INSERT INTO distributors (name, contact_email, contact_phone) "
        "VALUES ($1, $2, $3) RETURNING id",
        name, email, phone);

    return insertResult[0]["id"].as<int64_t>();
}

int64_t ZJMFFinanceAdapter::upsertInvoice(const Json::Value& invoice) {
    auto db = DbClient::getClient();

    // Check if invoice exists via entity mapping
    auto mappingResult = db->execSqlSync(
        "SELECT local_id FROM zjmf_entity_mappings "
        "WHERE remote_system = 'zjmf_finance' AND local_type = 'invoice' "
        "AND remote_id = $1",
        std::to_string(invoice["id"].asInt64()));

    if (!mappingResult.empty()) {
        int64_t localId = mappingResult[0]["local_id"].as<int64_t>();
        // Update status
        std::string status = invoice.get("status", "unpaid").asString();
        db->execSqlSync(
            "UPDATE invoices SET status=$1, updated_at=NOW() WHERE id=$2",
            status, localId);
        return localId;
    }

    // Insert new invoice record
    std::string date    = invoice.get("date", "").asString();
    std::string dueDate = invoice.get("duedate", "").asString();
    std::string total   = invoice.get("total", "0.00").asString();
    std::string status  = invoice.get("status", "unpaid").asString();

    auto insertResult = db->execSqlSync(
        "INSERT INTO invoices (invoice_no, distributor_id, status, "
        "total_amount, due_date) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING id",
        "ZJMF-" + std::to_string(invoice["id"].asInt64()),
        0,  // will be updated via mapping
        status, total, dueDate);

    return insertResult[0]["id"].as<int64_t>();
}

} // namespace idc
