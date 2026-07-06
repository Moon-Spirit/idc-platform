#include "zjmf_controller.h"
#include "services/zjmf_adapter.h"
#include "services/zjmf_sync_service.h"
#include "services/zjmf_provisioning_service.h"
#include "utils/crypto.h"
#include "utils/db_client.h"
#include "utils/logger.h"
#include "utils/response.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <json/json.h>

#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/sync/zjmf/status
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFController::getSyncStatus(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto connections = ZJMFConnectionManager::getActiveConnections();
        auto syncStatuses = ZJMFConnectionManager::getSyncStatus();

        Json::Value data;
        data["connections"] = Json::arrayValue;

        for (const auto& conn : connections) {
            Json::Value jc;
            jc["id"]            = static_cast<Json::Int64>(conn.id);
            jc["name"]          = conn.name;
            jc["type"]          = conn.type;
            jc["direction"]     = conn.direction;
            jc["api_base_url"]  = conn.apiBaseUrl;
            // Mask sensitive data
            jc["api_key"]       = conn.apiKey.substr(0, 8) + "...";
            jc["sync_interval"] = conn.syncInterval;
            jc["status"]        = conn.status;
            jc["last_sync_at"]  = conn.lastSyncAt;

            // Find matching sync status
            for (const auto& ss : syncStatuses) {
                if (ss.connectionId == conn.id) {
                    jc["sync_status"]  = ss.status;
                    jc["last_error"]   = ss.lastError;
                    jc["pending"]      = ss.pendingItems;
                    break;
                }
            }

            data["connections"].append(jc);
        }

        data["total_connections"] = static_cast<int>(connections.size());
        data["mock_mode"]         = ZJMFConnectionManager::isMockMode();

        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[ZJMFController] getSyncStatus error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/sync/zjmf/products
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFController::triggerProductSync(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto body = req->getJsonObject();
        int64_t connectionId = 0;

        if (body && body->isMember("connection_id")) {
            connectionId = (*body)["connection_id"].asInt64();
        }

        if (connectionId > 0) {
            // Sync single connection
            auto result = ZJMFSyncService::triggerSync(connectionId, "product");
            Json::Value resp;
            resp["connection_id"] = static_cast<Json::Int64>(connectionId);
            resp["success"]       = result.success;
            resp["items_synced"]  = result.itemsSynced;
            resp["items_created"] = result.itemsCreated;
            resp["items_error"]   = result.itemsError;
            resp["error"]         = result.errorMessage;
            callback(JsonResponse::ok(resp));
        } else {
            // Sync all v10 upstream connections
            ZJMFSyncService::runProductSync();
            callback(JsonResponse::ok(Json::Value("Product sync triggered for all connections")));
        }
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[ZJMFController] triggerProductSync error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/sync/zjmf/inventory
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFController::triggerInventorySync(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto body = req->getJsonObject();
        int64_t connectionId = 0;

        if (body && body->isMember("connection_id")) {
            connectionId = (*body)["connection_id"].asInt64();
        }

        if (connectionId > 0) {
            auto result = ZJMFSyncService::triggerSync(connectionId, "inventory");
            Json::Value resp;
            resp["connection_id"] = static_cast<Json::Int64>(connectionId);
            resp["success"]       = result.success;
            resp["items_synced"]  = result.itemsSynced;
            resp["items_error"]   = result.itemsError;
            resp["error"]         = result.errorMessage;
            callback(JsonResponse::ok(resp));
        } else {
            ZJMFSyncService::runInventorySync();
            callback(JsonResponse::ok(Json::Value("Inventory sync triggered for all connections")));
        }
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[ZJMFController] triggerInventorySync error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/sync/logs
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFController::getSyncLogs(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto query = req->getParameters();

        int64_t connectionId = 0;
        std::string entityType, statusFilter;
        int page = 1, perPage = 20;

        auto it = query.find("connection_id");
        if (it != query.end() && !it->second.empty()) {
            connectionId = std::stoll(it->second);
        }
        it = query.find("entity_type");
        if (it != query.end()) entityType = it->second;
        it = query.find("status");
        if (it != query.end()) statusFilter = it->second;
        it = query.find("page");
        if (it != query.end()) page = std::max(1, std::stoi(it->second));
        it = query.find("per_page");
        if (it != query.end()) perPage = std::max(1, std::min(100, std::stoi(it->second)));

        auto db = DbClient::getClient();

        // Build WHERE clause (parameterized values for safety)
        std::string whereClause;
        if (connectionId > 0) {
            whereClause += " WHERE zsl.connection_id = " + std::to_string(connectionId);
        }
        if (!entityType.empty()) {
            if (whereClause.empty()) whereClause += " WHERE";
            else whereClause += " AND";
            whereClause += " zsl.entity_type = '" + entityType + "'";
        }
        if (!statusFilter.empty()) {
            if (whereClause.empty()) whereClause += " WHERE";
            else whereClause += " AND";
            whereClause += " zsl.status = '" + statusFilter + "'";
        }

        // Count total
        std::string countSql = "SELECT COUNT(*) FROM zjmf_sync_logs zsl " + whereClause;
        auto countResult = db->execSqlSync(countSql);
        int64_t total = countResult.empty() ? 0 : countResult[0][0].as<int64_t>();

        // Fetch page
        int offsetVal = (page - 1) * perPage;
        std::string dataSql =
            "SELECT zsl.*, zc.name AS connection_name "
            "FROM zjmf_sync_logs zsl "
            "LEFT JOIN zjmf_connections zc ON zc.id = zsl.connection_id " +
            whereClause +
            " ORDER BY zsl.id DESC LIMIT $1 OFFSET $2";

        auto dataResult = db->execSqlSync(dataSql, perPage, offsetVal);

        Json::Value items(Json::arrayValue);
        for (const auto& row : dataResult) {
            Json::Value item;
            item["id"]              = static_cast<Json::Int64>(row["id"].as<int64_t>());
            item["connection_id"]   = static_cast<Json::Int64>(row["connection_id"].as<int64_t>());
            item["connection_name"] = row["connection_name"].as<std::string>();
            item["direction"]       = row["direction"].as<std::string>();
            item["entity_type"]     = row["entity_type"].as<std::string>();
            item["action"]          = row["action"].as<std::string>();
            item["status"]          = row["status"].as<std::string>();
            item["error_message"]   = row["error_message"].as<std::string>();
            item["retry_count"]     = row["retry_count"].as<int>();
            item["created_at"]      = row["created_at"].as<std::string>();

            // Include truncated request/response for debugging
            std::string reqData = row["request_data"].as<std::string>();
            std::string respData = row["response_data"].as<std::string>();
            if (reqData.size() > 500) reqData = reqData.substr(0, 500) + "...(truncated)";
            if (respData.size() > 500) respData = respData.substr(0, 500) + "...(truncated)";

            item["request_data"]  = reqData;
            item["response_data"] = respData;

            items.append(item);
        }

        Json::Value data;
        data["items"]    = items;
        data["total"]    = static_cast<Json::Int64>(total);
        data["page"]     = page;
        data["per_page"] = perPage;

        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[ZJMFController] getSyncLogs error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/webhooks/zjmf
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFController::handleWebhook(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        // Verify signature
        if (!verifyWebhookSignature(req)) {
            LOG_IDC_WARN(req, "[ZJMFController] Webhook signature verification failed");
            callback(JsonResponse::error(403, "Invalid signature",
                                         drogon::k403Forbidden));
            return;
        }

        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Invalid JSON body"));
            return;
        }

        LOG_IDC_INFO(req, "[ZJMFController] Webhook received: "
                     << Json::FastWriter().write(*body));

        // Process the webhook
        auto result = processWebhook(req, *body);
        callback(JsonResponse::ok(result));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[ZJMFController] handleWebhook error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Private: webhook signature verification
// ═══════════════════════════════════════════════════════════════════════════

bool ZJMFController::verifyWebhookSignature(
    const drogon::HttpRequestPtr& req) {
    // ZJMF sends signature in X-ZJMF-Signature header
    // Format: HMAC-SHA256 hex digest of the request body
    std::string signature = req->getHeader("X-ZJMF-Signature");
    if (signature.empty()) {
        // Try alternative header names
        signature = req->getHeader("X-Signature");
    }
    if (signature.empty()) {
        LOG_WARN << "[ZJMFController] No signature header found";
        return false;
    }

    std::string body = std::string(req->body());
    std::string secret = CryptoUtil::getWebhookSecret();

    // HMAC-SHA256 verification
    return CryptoUtil::hmacSha256Verify(body, secret, signature);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Private: webhook processing (webhook-first strategy)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value ZJMFController::processWebhook(
    const drogon::HttpRequestPtr& req,
    const Json::Value& payload) {
    // Extract event type
    std::string eventType = payload.get("event", payload.get("type", "")).asString();
    Json::Value data = payload.get("data", payload);

    Json::Value result;
    result["event"] = eventType;
    result["processed"] = true;

    try {
        // ── provisioning status updates ────────────────────────────────────
        // Delegate to ZJMFProvisioningService for server/invoice provisioning
        bool isProvisionEvent =
            (eventType == "server_status" || eventType == "server.provision" ||
             eventType == "invoice_status" || eventType == "invoice.update");

        if (isProvisionEvent) {
            auto provResult = ZJMFProvisioningService::handleWebhookCallback(payload);
            if (provResult["processed"].asBool()) {
                result["subscription_id"] = provResult["subscription_id"];
                result["provision_status"] = provResult["provision_status"];
                LOG_INFO << "[ZJMFController] Webhook: provisioning update processed "
                         << "sub_id=" << provResult["subscription_id"]
                         << " status=" << provResult["provision_status"];
            } else {
                result["processed"] = false;
                result["note"] = provResult.get("note", provResult.get("error", "Unknown"));
            }

        // ── payment notification ──────────────────────────────────────────
        } else if (eventType == "payment" || eventType == "invoice.paid") {
            std::string invoiceId = data["invoice_id"].asString();
            std::string transactionId = data.get("transaction_id", "").asString();
            std::string amount = data.get("amount", "0.00").asString();

            LOG_INFO << "[ZJMFController] Webhook: payment received for invoice "
                     << invoiceId << " amount=" << amount;

        // ── order status update ───────────────────────────────────────────
        } else if (eventType == "order_status" || eventType == "order.update") {
            std::string orderId = data["id"].asString();
            std::string status  = data.get("status", "").asString();

            LOG_INFO << "[ZJMFController] Webhook: order " << orderId
                     << " status → " << status;

        } else {
            LOG_INFO << "[ZJMFController] Webhook: unhandled event type: "
                     << eventType;
            result["processed"] = false;
            result["note"] = "Unhandled event type: " + eventType;
        }

        // Log webhook receipt to zjmf_sync_logs
        try {
            auto db = DbClient::getClient();
            db->execSqlSync(
                "INSERT INTO zjmf_sync_logs "
                "(connection_id, direction, entity_type, entity_id, remote_id, "
                " action, request_data, response_data, status) "
                "VALUES (0, 'inbound', 'webhook', 0, $1, 'callback', "
                " $2::jsonb, $3::jsonb, 'success')",
                data.get("id", "").asString(),
                Json::FastWriter().write(payload),
                Json::FastWriter().write(result));
        } catch (...) {}

    } catch (const std::exception& e) {
        LOG_ERROR << "[ZJMFController] processWebhook error: " << e.what();
        result["processed"] = false;
        result["error"] = e.what();
    }

    return result;
}

} // namespace idc
