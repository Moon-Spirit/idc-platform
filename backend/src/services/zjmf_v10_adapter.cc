#include "zjmf_v10_adapter.h"
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

ZJMFV10Adapter::ZJMFV10Adapter(const ZJMFConnection& connection,
                               std::string apiKey,
                               std::string apiSecret,
                               bool mockMode)
    : connection_(connection) {
    http_ = std::make_unique<HttpClient>(
        connection.apiBaseUrl,
        std::move(apiKey),
        std::move(apiSecret),
        mockMode);

    // Configure mock responses if in mock mode
    if (mockMode) {
        // Mock product list
        Json::Value mockProducts(Json::arrayValue);
        for (int i = 1; i <= 3; ++i) {
            Json::Value p;
            p["id"]          = i;
            p["name"]        = "Mock Server E5-" + std::to_string(2620 + i * 10);
            p["type"]        = "bare_metal";
            p["description"] = "Mock product " + std::to_string(i);
            p["specs"] = Json::objectValue;
            p["specs"]["cpu"]   = "Intel Xeon E5-2680v4";
            p["specs"]["cores"] = 14;
            p["specs"]["ram"]   = "128GB DDR4";
            p["specs"]["disk"]  = "2x960GB SSD";
            p["updated_at"]     = "2026-07-01T00:00:00Z";
            mockProducts.append(p);
        }
        Json::Value prodResp;
        prodResp["data"]    = mockProducts;
        prodResp["total"]   = 3;
        prodResp["success"] = true;
        http_->setMockResponse("GET", "/api/v1/products", prodResp);

        // Mock inventory
        Json::Value mockInv(Json::arrayValue);
        for (int i = 1; i <= 5; ++i) {
            Json::Value inv;
            inv["id"]             = 100 + i;
            inv["product_id"]     = (i % 3) + 1;
            inv["total"]          = 10;
            inv["available"]      = 8 - i;
            inv["reserved"]       = 2;
            inv["datacenter"]     = "DC-01";
            mockInv.append(inv);
        }
        Json::Value invResp;
        invResp["data"]    = mockInv;
        invResp["success"] = true;
        http_->setMockResponse("GET", "/api/v1/inventory", invResp);

        // Mock server create
        Json::Value createResp;
        createResp["success"]  = true;
        createResp["data"]["id"] = "srv_mock_001";
        createResp["data"]["status"] = "pending";
        http_->setMockResponse("POST", "/api/servers", createResp, 201);

        // Mock server status
        Json::Value statusResp;
        statusResp["success"] = true;
        statusResp["data"]["id"]     = "srv_mock_001";
        statusResp["data"]["status"] = "active";
        http_->setMockResponse("GET", "/api/servers/srv_mock_001", statusResp);

        // Mock suspend
        Json::Value suspendResp;
        suspendResp["success"] = true;
        suspendResp["message"]  = "Server suspended";
        http_->setMockResponse("PUT", "/api/servers/srv_mock_001/suspend", suspendResp);

        LOG_INFO << "[ZJMFV10Adapter] Mock responses configured for connection "
                 << connection_.id;
    }
}

// ============================================================================
// ── testConnection ────────────────────────────────────────────────────────────
// ============================================================================

HttpResult ZJMFV10Adapter::testConnection() {
    return http_->get("/api/v1/products", {{"per_page", "1"}});
}

// ============================================================================
// ── syncProducts ──────────────────────────────────────────────────────────────
// ============================================================================

SyncResult ZJMFV10Adapter::syncProducts() {
    SyncResult result;

    // Rate limit
    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) {
        result.errorMessage = "Rate limit exceeded";
        LOG_WARN << "[ZJMFV10Adapter] Rate limit hit for connection "
                 << connection_.id;
        return result;
    }

    LOG_INFO << "[ZJMFV10Adapter] Syncing products from connection "
             << connection_.id << " (" << connection_.name << ")";

    auto httpResult = http_->get("/api/v1/products");

    if (!httpResult.ok) {
        result.errorMessage = httpResult.error;
        result.syncLogId = logSync(
            connection_.id, "inbound", "product", 0, "",
            "sync", Json::objectValue, httpResult.body,
            "failed", httpResult.error, 1);
        LOG_ERROR << "[ZJMFV10Adapter] syncProducts failed: " << httpResult.error;
        return result;
    }

    // Parse response — ZJMF typically wraps in { "data": [...], "success": true }
    Json::Value items;
    if (httpResult.body.isMember("data") && httpResult.body["data"].isArray()) {
        items = httpResult.body["data"];
    } else if (httpResult.body.isArray()) {
        items = httpResult.body;
    } else {
        result.errorMessage = "Unexpected response format";
        result.syncLogId = logSync(
            connection_.id, "inbound", "product", 0, "",
            "sync", Json::objectValue, httpResult.body,
            "failed", result.errorMessage, 1);
        return result;
    }

    // Process each product
    for (const auto& item : items) {
        try {
            int64_t localId = upsertProduct(item);

            // Record entity mapping
            std::string remoteId = std::to_string(item["id"].asInt64());
            auto db = DbClient::getClient();
            db->execSqlSync(
                "INSERT INTO zjmf_entity_mappings "
                "(local_type, local_id, remote_system, remote_id, remote_data) "
                "VALUES ('product', $1, 'zjmf_v10', $2, $3::jsonb) "
                "ON CONFLICT (local_type, local_id, remote_system) "
                "DO UPDATE SET remote_data = $3::jsonb, last_synced_at = NOW()",
                localId, remoteId,
                Json::FastWriter().write(item));

            result.itemsCreated++;
            result.itemsSynced++;
        } catch (const std::exception& e) {
            LOG_ERROR << "[ZJMFV10Adapter] Failed to upsert product: " << e.what();
            result.itemsError++;
        }
    }

    result.success = true;
    result.syncLogId = logSync(
        connection_.id, "inbound", "product", 0, "",
        "sync", httpResult.body, httpResult.body,
        "success", "", 1);

    // Update last_sync_at
    try {
        auto db = DbClient::getClient();
        db->execSqlSync(
            "UPDATE zjmf_connections SET last_sync_at = NOW() WHERE id = $1",
            connection_.id);
    } catch (...) {}

    LOG_INFO << "[ZJMFV10Adapter] syncProducts complete: "
             << result.itemsSynced << " synced, "
             << result.itemsCreated << " created, "
             << result.itemsError << " errors";
    return result;
}

// ============================================================================
// ── syncInventory ─────────────────────────────────────────────────────────────
// ============================================================================

SyncResult ZJMFV10Adapter::syncInventory() {
    SyncResult result;

    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) {
        result.errorMessage = "Rate limit exceeded";
        return result;
    }

    LOG_INFO << "[ZJMFV10Adapter] Syncing inventory from connection "
             << connection_.id;

    auto httpResult = http_->get("/api/v1/inventory");

    if (!httpResult.ok) {
        result.errorMessage = httpResult.error;
        result.syncLogId = logSync(
            connection_.id, "inbound", "inventory", 0, "",
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
        result.errorMessage = "Unexpected inventory response format";
        result.syncLogId = logSync(
            connection_.id, "inbound", "inventory", 0, "",
            "sync", Json::objectValue, httpResult.body,
            "failed", result.errorMessage, 1);
        return result;
    }

    for (const auto& item : items) {
        try {
            int64_t productId = item["product_id"].asInt64();
            int available     = item["available"].asInt();
            int total         = item["total"].asInt();
            int reserved      = item.get("reserved", 0).asInt();

            // Update the local products table's specs with inventory data
            auto db = DbClient::getClient();
            db->execSqlSync(
                "UPDATE products SET specs = jsonb_set("
                "  jsonb_set("
                "    jsonb_set(specs, '{inventory_total}', $1::jsonb),"
                "    '{inventory_available}', $2::jsonb),"
                "  '{inventory_reserved}', $3::jsonb"
                "), updated_at = NOW() WHERE id = $4",
                Json::Value(total).toStyledString(),
                Json::Value(available).toStyledString(),
                Json::Value(reserved).toStyledString(),
                productId);

            result.itemsUpdated++;
            result.itemsSynced++;
        } catch (const std::exception& e) {
            LOG_ERROR << "[ZJMFV10Adapter] Failed to update inventory: " << e.what();
            result.itemsError++;
        }
    }

    result.success = true;
    result.syncLogId = logSync(
        connection_.id, "inbound", "inventory", 0, "",
        "sync", httpResult.body, httpResult.body,
        "success", "", 1);

    return result;
}

// ============================================================================
// ── Provisioning ──────────────────────────────────────────────────────────────
// ============================================================================

SyncResult ZJMFV10Adapter::createServer(const Json::Value& orderData) {
    SyncResult result;

    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) {
        result.errorMessage = "Rate limit exceeded";
        return result;
    }

    LOG_INFO << "[ZJMFV10Adapter] Creating server for connection "
             << connection_.id;

    auto httpResult = http_->post("/api/servers", orderData);

    if (!httpResult.ok) {
        result.errorMessage = httpResult.error;
        result.syncLogId = logSync(
            connection_.id, "outbound", "subscription", 0, "",
            "createServer", orderData, httpResult.body,
            "failed", httpResult.error, 1);
        return result;
    }

    // Extract remote ID
    std::string remoteId;
    if (httpResult.body.isMember("data") &&
        httpResult.body["data"].isMember("id")) {
        remoteId = httpResult.body["data"]["id"].asString();
    } else if (httpResult.body.isMember("id")) {
        remoteId = httpResult.body["id"].asString();
    }

    if (!remoteId.empty()) {
        result.itemsCreated = 1;
    }

    result.success = true;
    result.syncLogId = logSync(
        connection_.id, "outbound", "subscription", 0,
        remoteId, "createServer", orderData, httpResult.body,
        "success", "", 1);

    return result;
}

Json::Value ZJMFV10Adapter::queryServerStatus(const std::string& remoteId) {
    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume(500)) {
        Json::Value err;
        err["error"] = "rate_limited";
        return err;
    }

    auto result = http_->get("/api/servers/" + remoteId);
    if (!result.ok) {
        Json::Value err;
        err["error"] = result.error;
        return err;
    }

    return result.body;
}

SyncResult ZJMFV10Adapter::suspendServer(const std::string& remoteId) {
    SyncResult sr;
    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) { sr.errorMessage = "rate_limited"; return sr; }

    Json::Value body(Json::objectValue);
    auto result = http_->put("/api/servers/" + remoteId + "/suspend", body);
    sr.success = result.ok;
    sr.syncLogId = logSync(
        connection_.id, "outbound", "subscription", 0,
        remoteId, "suspend", body, result.body,
        result.ok ? "success" : "failed",
        result.ok ? "" : result.error, 1);
    return sr;
}

SyncResult ZJMFV10Adapter::activateServer(const std::string& remoteId) {
    SyncResult sr;
    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) { sr.errorMessage = "rate_limited"; return sr; }

    Json::Value body(Json::objectValue);
    auto result = http_->put("/api/servers/" + remoteId + "/activate", body);
    sr.success = result.ok;
    sr.syncLogId = logSync(
        connection_.id, "outbound", "subscription", 0,
        remoteId, "activate", body, result.body,
        result.ok ? "success" : "failed",
        result.ok ? "" : result.error, 1);
    return sr;
}

SyncResult ZJMFV10Adapter::terminateServer(const std::string& remoteId) {
    SyncResult sr;
    auto& bucket = RateLimiter::instance().getBucket(connection_.id);
    if (!bucket.tryConsume()) { sr.errorMessage = "rate_limited"; return sr; }

    auto result = http_->del("/api/servers/" + remoteId);
    sr.success = result.ok;
    sr.syncLogId = logSync(
        connection_.id, "outbound", "subscription", 0,
        remoteId, "terminate", Json::objectValue, result.body,
        result.ok ? "success" : "failed",
        result.ok ? "" : result.error, 1);
    return sr;
}

// ============================================================================
// ── Private helpers ───────────────────────────────────────────────────────────
// ============================================================================

int64_t ZJMFV10Adapter::upsertProduct(const Json::Value& product) {
    auto db = DbClient::getClient();

    std::string name = product.get("name", "Unnamed").asString();
    std::string type = product.get("type", "bare_metal").asString();
    std::string description = product.get("description", "").asString();
    Json::Value specs = product.get("specs", Json::objectValue);

    // Add remote ID to specs for traceability
    specs["zjmf_remote_id"] = product["id"].asInt64();
    specs["zjmf_connection_id"] = static_cast<Json::Int64>(connection_.id);

    // Check if product already exists via entity mapping
    auto mappingResult = db->execSqlSync(
        "SELECT local_id FROM zjmf_entity_mappings "
        "WHERE remote_system = 'zjmf_v10' AND remote_id = $1",
        std::to_string(product["id"].asInt64()));

    if (!mappingResult.empty()) {
        // Update existing product
        int64_t localId = mappingResult[0]["local_id"].as<int64_t>();
        db->execSqlSync(
            "UPDATE products SET name=$1, type=$2, description=$3, "
            "specs=$4::jsonb, updated_at=NOW() WHERE id=$5",
            name, type, description,
            Json::FastWriter().write(specs),
            localId);
        return localId;
    }

    // Insert new product
    auto insertResult = db->execSqlSync(
        "INSERT INTO products (name, type, description, specs) "
        "VALUES ($1, $2, $3, $4::jsonb) RETURNING id",
        name, type, description,
        Json::FastWriter().write(specs));

    return insertResult[0]["id"].as<int64_t>();
}

} // namespace idc
