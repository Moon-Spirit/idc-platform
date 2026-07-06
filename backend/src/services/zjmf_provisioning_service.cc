#include "zjmf_provisioning_service.h"
#include "services/zjmf_adapter.h"
#include "services/order_service.h"    // for transitionOrder on provisioning_failed
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <json/json.h>

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  provisionOrder — called after order approval
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFProvisioningService::provisionOrder(int64_t orderId) {
    LOG_INFO << "[Provisioning] Starting provisioning for order_id=" << orderId;

    auto db = DbClient::getClient();
    auto rows = db->execSqlSync(
        "SELECT id, sub_no, product_id, distributor_id, "
        "       remote_system, product_specs, provision_status "
        "FROM subscriptions "
        "WHERE order_id = $1 AND provision_status = 'pending'",
        orderId);

    if (rows.empty()) {
        LOG_INFO << "[Provisioning] No subscriptions to provision for order_id="
                 << orderId;
        return;
    }

    for (const auto& row : rows) {
        int64_t subId = row["id"].as<int64_t>();
        doProvision(subId, orderId);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  provisionSubscription — single subscription provisioning
// ═══════════════════════════════════════════════════════════════════════════

Json::Value ZJMFProvisioningService::provisionSubscription(int64_t subscriptionId) {
    auto db = DbClient::getClient();
    auto rows = db->execSqlSync(
        "SELECT id, order_id, sub_no, remote_system, provision_status "
        "FROM subscriptions WHERE id = $1",
        subscriptionId);

    if (rows.empty()) {
        Json::Value err;
        err["error"] = "Subscription not found";
        return err;
    }

    auto row = rows[0];
    int64_t orderId = row["order_id"].as<int64_t>();
    std::string provisionStatus = row["provision_status"].as<std::string>();

    // Only allow provisioning if pending or failed
    if (provisionStatus != "pending" && provisionStatus != "failed") {
        Json::Value err;
        err["error"] = "Cannot provision subscription with status: " + provisionStatus;
        return err;
    }

    bool ok = doProvision(subscriptionId, orderId);

    Json::Value result;
    result["subscription_id"] = static_cast<Json::Int64>(subscriptionId);
    result["sub_no"] = row["sub_no"].as<std::string>();
    result["submitted"] = ok;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  retryProvisioning — manual retry for failed provisioning
// ═══════════════════════════════════════════════════════════════════════════

Json::Value ZJMFProvisioningService::retryProvisioning(int64_t subscriptionId,
                                                        int64_t operatorId,
                                                        const std::string& operatorName) {
    auto db = DbClient::getClient();

    // Fetch subscription, verify it's in failed state
    auto rows = db->execSqlSync(
        "SELECT id, order_id, sub_no, remote_system, provision_status "
        "FROM subscriptions WHERE id = $1 FOR UPDATE",
        subscriptionId);

    if (rows.empty()) {
        Json::Value err;
        err["error"] = "Subscription not found";
        return err;
    }

    auto row = rows[0];
    std::string provisionStatus = row["provision_status"].as<std::string>();

    if (provisionStatus != "failed") {
        Json::Value err;
        err["error"] = "Provisioning is not in failed state, current: " + provisionStatus;
        return err;
    }

    int64_t orderId = row["order_id"].as<int64_t>();
    std::string remoteSystem = row["remote_system"].isNull()
        ? "" : row["remote_system"].as<std::string>();

    LOG_INFO << "[Provisioning] Manual retry for subscription_id=" << subscriptionId
             << " remote_system=" << remoteSystem;

    // Reset subscription for retry
    db->execSqlSync(
        "UPDATE subscriptions SET "
        "  provision_status = 'provisioning', "
        "  provisioning_error = NULL, "
        "  provisioning_attempts = provisioning_attempts + 1, "
        "  last_provision_attempt = NOW(), "
        "  provisioning_started_at = NOW(), "
        "  poll_count = 0, "
        "  poll_until = NOW() + INTERVAL '" + std::to_string(kMaxPollTimeSeconds) + " seconds', "
        "  updated_at = NOW() "
        "WHERE id = $1",
        subscriptionId);

    // Update order back to provisioning
    db->execSqlSync(
        "UPDATE orders SET status = 'provisioning', updated_at = NOW() "
        "WHERE id = $1 AND status = 'provisioning_failed'",
        orderId);

    // Record subscription timeline
    db->execSqlSync(
        "INSERT INTO subscription_timeline "
        "(subscription_id, from_status, to_status, operator_id, operator_name, remark) "
        "VALUES ($1, $2, 'provisioning', $3, $4, '管理员手动重试开通')",
        subscriptionId, row["status"].as<std::string>(),
        operatorId, operatorName);

    // Execute provisioning
    bool ok = doProvision(subscriptionId, orderId);

    Json::Value result;
    result["subscription_id"] = static_cast<Json::Int64>(subscriptionId);
    result["sub_no"] = row["sub_no"].as<std::string>();
    result["submitted"] = ok;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  getProvisionStatus — current provisioning status
// ═══════════════════════════════════════════════════════════════════════════

Json::Value ZJMFProvisioningService::getProvisionStatus(int64_t subscriptionId) {
    auto db = DbClient::getClient();
    auto rows = db->execSqlSync(
        "SELECT s.id, s.sub_no, s.order_id, s.order_item_id, s.distributor_id, "
        "       s.product_id, p.name AS product_name, "
        "       s.status, s.provision_status, "
        "       s.remote_system, s.remote_resource_id, "
        "       s.provisioning_attempts, s.provisioning_error, "
        "       s.provisioning_started_at, s.last_provision_attempt, "
        "       s.poll_count, s.poll_until, "
        "       s.provision_data, s.provision_webhook_data "
        "FROM subscriptions s "
        "LEFT JOIN products p ON p.id = s.product_id "
        "WHERE s.id = $1",
        subscriptionId);

    if (rows.empty()) {
        return Json::nullValue;
    }

    auto row = rows[0];
    Json::Value status;
    status["subscription_id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
    status["sub_no"] = row["sub_no"].as<std::string>();
    status["order_id"] = static_cast<Json::Int64>(row["order_id"].as<int64_t>());
    status["product_name"] = row["product_name"].isNull()
        ? "" : row["product_name"].as<std::string>();
    status["status"] = row["status"].as<std::string>();
    status["provision_status"] = row["provision_status"].as<std::string>();
    status["remote_system"] = row["remote_system"].isNull()
        ? Json::nullValue : Json::Value(row["remote_system"].as<std::string>());
    status["remote_resource_id"] = row["remote_resource_id"].isNull()
        ? Json::nullValue : Json::Value(row["remote_resource_id"].as<std::string>());
    status["provisioning_attempts"] = row["provisioning_attempts"].as<int>();
    status["provisioning_error"] = row["provisioning_error"].isNull()
        ? "" : row["provisioning_error"].as<std::string>();
    status["provisioning_started_at"] = row["provisioning_started_at"].isNull()
        ? "" : row["provisioning_started_at"].as<std::string>();
    status["last_provision_attempt"] = row["last_provision_attempt"].isNull()
        ? "" : row["last_provision_attempt"].as<std::string>();
    status["poll_count"] = row["poll_count"].as<int>();
    status["poll_until"] = row["poll_until"].isNull()
        ? "" : row["poll_until"].as<std::string>();

    // Parse provision_data JSON if present
    if (!row["provision_data"].isNull()) {
        std::string pdStr = row["provision_data"].as<std::string>();
        Json::Reader reader;
        Json::Value pd;
        if (reader.parse(pdStr, pd)) {
            status["provision_data"] = pd;
        }
    }

    // Parse provision_webhook_data JSON if present
    if (!row["provision_webhook_data"].isNull()) {
        std::string whStr = row["provision_webhook_data"].as<std::string>();
        Json::Reader reader;
        Json::Value wh;
        if (reader.parse(whStr, wh)) {
            status["provision_webhook_data"] = wh;
        }
    }

    // Fetch entity mappings
    auto mappingRows = db->execSqlSync(
        "SELECT remote_system, remote_id, remote_data, last_synced_at "
        "FROM zjmf_entity_mappings "
        "WHERE local_type = 'subscription' AND local_id = $1",
        subscriptionId);

    Json::Value mappings(Json::arrayValue);
    for (const auto& mr : mappingRows) {
        Json::Value m;
        m["remote_system"] = mr["remote_system"].as<std::string>();
        m["remote_id"] = mr["remote_id"].as<std::string>();

        if (!mr["remote_data"].isNull()) {
            std::string rdStr = mr["remote_data"].as<std::string>();
            Json::Reader reader;
            Json::Value rd;
            if (reader.parse(rdStr, rd)) {
                m["remote_data"] = rd;
            }
        }

        m["last_synced_at"] = mr["last_synced_at"].as<std::string>();
        mappings.append(m);
    }
    status["entity_mappings"] = mappings;

    // Fetch recent sync logs
    auto logRows = db->execSqlSync(
        "SELECT id, action, status, error_message, created_at "
        "FROM zjmf_sync_logs "
        "WHERE entity_type = 'subscription' AND entity_id = $1 "
        "ORDER BY id DESC LIMIT 10",
        subscriptionId);

    Json::Value logs(Json::arrayValue);
    for (const auto& lr : logRows) {
        Json::Value l;
        l["id"] = static_cast<Json::Int64>(lr["id"].as<int64_t>());
        l["action"] = lr["action"].as<std::string>();
        l["status"] = lr["status"].as<std::string>();
        l["error_message"] = lr["error_message"].isNull()
            ? "" : lr["error_message"].as<std::string>();
        l["created_at"] = lr["created_at"].as<std::string>();
        logs.append(l);
    }
    status["sync_logs"] = logs;

    return status;
}

// ═══════════════════════════════════════════════════════════════════════════
//  handleWebhookCallback — process provisioning status callback
// ═══════════════════════════════════════════════════════════════════════════

Json::Value ZJMFProvisioningService::handleWebhookCallback(
    const Json::Value& payload) {
    Json::Value result;
    result["processed"] = false;

    std::string eventType = payload.get("event", payload.get("type", "")).asString();
    Json::Value data = payload.get("data", payload);

    // Only handle provisioning-related events
    bool isServerEvent = (eventType == "server_status" ||
                          eventType == "server.provision");
    bool isInvoiceEvent = (eventType == "invoice_status" ||
                           eventType == "invoice.update");

    if (!isServerEvent && !isInvoiceEvent) {
        result["note"] = "Unhandled event type: " + eventType;
        return result;
    }

    std::string remoteId;
    std::string status;

    if (isServerEvent) {
        remoteId = data["id"].asString();
        status = data.get("status", "").asString();
    } else {
        remoteId = data["invoice_id"].isNull()
            ? data["id"].asString()
            : data["invoice_id"].asString();
        status = data.get("status", "").asString();
    }

    if (remoteId.empty()) {
        result["error"] = "No remote ID in webhook payload";
        return result;
    }

    // Look up subscription by entity mapping
    auto db = DbClient::getClient();
    auto mappingRows = db->execSqlSync(
        "SELECT local_id, local_type FROM zjmf_entity_mappings "
        "WHERE remote_system IN ('zjmf_v10', 'zjmf_finance') "
        "  AND remote_id = $1 AND local_type = 'subscription'",
        remoteId);

    if (mappingRows.empty()) {
        LOG_WARN << "[Provisioning] Webhook: no mapping found for remote_id="
                 << remoteId;
        result["error"] = "No matching subscription found for remote_id: " + remoteId;
        return result;
    }

    int64_t subId = mappingRows[0]["local_id"].as<int64_t>();

    // Map ZJMF status to provision_status
    std::string provisionStatus;
    if (status == "active" || status == "running" || status == "completed" ||
        status == "paid") {
        provisionStatus = "done";
    } else if (status == "suspended") {
        provisionStatus = "suspended";
    } else if (status == "terminated" || status == "cancelled") {
        provisionStatus = "terminated";
    } else if (status == "failed" || status == "error") {
        provisionStatus = "failed";
    } else {
        provisionStatus = "provisioning"; // still in progress
    }

    LOG_INFO << "[Provisioning] Webhook: sub_id=" << subId
             << " remote_id=" << remoteId
             << " status=" << status
             << " → provision_status=" << provisionStatus;

    // Store webhook data
    db->execSqlSync(
        "UPDATE subscriptions SET "
        "  provision_webhook_data = $1::jsonb, "
        "  updated_at = NOW() "
        "WHERE id = $2",
        Json::FastWriter().write(payload), subId);

    if (provisionStatus == "done") {
        completeProvisioning(subId, remoteId);
    } else if (provisionStatus == "failed") {
        failProvisioning(subId, 0, "Downstream provisioning failed with status: "
                                   + status);
    } else {
        // Still provisioning, just update the status
        db->execSqlSync(
            "UPDATE subscriptions SET provision_status = $1, updated_at = NOW() "
            "WHERE id = $2",
            provisionStatus, subId);
    }

    result["subscription_id"] = static_cast<Json::Int64>(subId);
    result["remote_id"] = remoteId;
    result["provision_status"] = provisionStatus;
    result["processed"] = true;

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  runPollingCheck — periodic polling for provisioning status
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFProvisioningService::runPollingCheck() {
    auto db = DbClient::getClient();

    // Find subscriptions that:
    // - provision_status = 'provisioning'
    // - remote_resource_id IS NOT NULL (already sent to downstream)
    // - poll_until > NOW() (still within polling window)
    auto rows = db->execSqlSync(
        "SELECT s.id, s.order_id, s.remote_system, s.remote_resource_id, "
        "       s.poll_count, s.poll_until "
        "FROM subscriptions s "
        "WHERE s.provision_status = 'provisioning' "
        "  AND s.remote_resource_id IS NOT NULL "
        "  AND (s.poll_until IS NULL OR s.poll_until > NOW()) "
        "LIMIT 50");

    for (const auto& row : rows) {
        int64_t subId = row["id"].as<int64_t>();
        int64_t orderId = row["order_id"].as<int64_t>();
        std::string remoteSystem = row["remote_system"].isNull()
            ? "" : row["remote_system"].as<std::string>();
        std::string remoteId = row["remote_resource_id"].as<std::string>();
        int pollCount = row["poll_count"].as<int>();
        std::string pollUntil = row["poll_until"].isNull()
            ? "" : row["poll_until"].as<std::string>();

        // Check if we've exceeded the polling timeout
        if (!pollUntil.empty()) {
            // poll_until is a TIMESTAMPTZ from DB; compare with NOW()
            // Already filtered by SQL WHERE poll_until > NOW()
        }

        // Check if we've exceeded max poll count
        if (pollCount >= kMaxPollCount) {
            LOG_WARN << "[Provisioning] Poll count exceeded for sub_id=" << subId
                     << " remote_id=" << remoteId
                     << " count=" << pollCount;
            failProvisioning(subId, orderId,
                             "Provisioning polling timeout after "
                             + std::to_string(pollCount) + " attempts");
            continue;
        }

        LOG_INFO << "[Provisioning] Polling sub_id=" << subId
                 << " remote_id=" << remoteId
                 << " attempt=" << (pollCount + 1);

        try {
            // Get adapter and query status
            int64_t connId = findDownstreamConnection(remoteSystem);
            if (connId == 0) {
                failProvisioning(subId, orderId,
                                 "No downstream connection for: " + remoteSystem);
                continue;
            }

            auto adapter = ZJMFConnectionManager::getAdapter(connId);
            if (!adapter) {
                failProvisioning(subId, orderId,
                                 "Failed to get adapter for connection: "
                                 + std::to_string(connId));
                continue;
            }

            // Increment poll count
            db->execSqlSync(
                "UPDATE subscriptions SET poll_count = poll_count + 1, "
                "updated_at = NOW() WHERE id = $1",
                subId);

            Json::Value statusResult;
            std::string provisionStatus;

            if (remoteSystem == "zjmf_v10") {
                statusResult = adapter->queryServerStatus(remoteId);
                std::string vmStatus = statusResult.get("status", "").asString();
                if (vmStatus == "active" || vmStatus == "running") {
                    provisionStatus = "done";
                } else if (vmStatus == "suspended") {
                    provisionStatus = "suspended";
                } else if (vmStatus == "terminated" || vmStatus == "error") {
                    provisionStatus = "failed";
                }
            } else if (remoteSystem == "zjmf_finance") {
                statusResult = adapter->queryInvoice(remoteId);
                std::string invStatus = statusResult.get("status", "").asString();
                if (invStatus == "paid" || invStatus == "completed") {
                    provisionStatus = "done";
                } else if (invStatus == "cancelled" || invStatus == "refunded") {
                    provisionStatus = "failed";
                }
            }

            // Log the poll
            logSyncAction(connId, "outbound", "subscription", subId,
                          remoteId, "poll",
                          Json::Value(Json::objectValue),
                          statusResult,
                          provisionStatus.empty() ? "pending" : "success",
                          "");

            if (provisionStatus == "done") {
                completeProvisioning(subId, remoteId);
            } else if (provisionStatus == "failed") {
                failProvisioning(subId, orderId,
                                 "Downstream status indicates failure");
            }
            // else: still provisioning, will poll again next cycle

        } catch (const std::exception& e) {
            LOG_ERROR << "[Provisioning] Poll error for sub_id=" << subId
                      << ": " << e.what();

            logSyncAction(0, "outbound", "subscription", subId,
                          remoteId, "poll",
                          Json::Value(Json::objectValue),
                          Json::Value(Json::objectValue),
                          "failed", e.what());
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  doProvision — core provisioning logic
// ═══════════════════════════════════════════════════════════════════════════

bool ZJMFProvisioningService::doProvision(int64_t subId, int64_t orderId) {
    auto db = DbClient::getClient();

    // Fetch subscription details
    auto rows = db->execSqlSync(
        "SELECT s.id, s.sub_no, s.product_id, s.distributor_id, "
        "       s.remote_system, s.product_specs, "
        "       s.billing_cycle, s.provision_status, "
        "       o.distributor_id AS order_distributor_id, "
        "       d.name AS distributor_name "
        "FROM subscriptions s "
        "JOIN orders o ON o.id = s.order_id "
        "LEFT JOIN distributors d ON d.id = s.distributor_id "
        "WHERE s.id = $1",
        subId);

    if (rows.empty()) {
        LOG_ERROR << "[Provisioning] Subscription not found: id=" << subId;
        return false;
    }

    auto row = rows[0];
    std::string remoteSystem = row["remote_system"].isNull()
        ? "" : row["remote_system"].as<std::string>();

    // If no remote system configured, mark as done immediately
    if (remoteSystem.empty()) {
        LOG_INFO << "[Provisioning] No remote_system for sub_id=" << subId
                 << ", marking as done";
        completeProvisioning(subId, "");
        return true;
    }

    // Find downstream connection matching the remote system type
    int64_t connId = findDownstreamConnection(remoteSystem);
    if (connId == 0) {
        LOG_ERROR << "[Provisioning] No downstream connection for remote_system="
                  << remoteSystem;
        failProvisioning(subId, orderId,
                         "No downstream connection configured for: " + remoteSystem);
        return false;
    }

    auto adapter = ZJMFConnectionManager::getAdapter(connId);
    if (!adapter) {
        failProvisioning(subId, orderId,
                         "Failed to initialize adapter for connection: "
                         + std::to_string(connId));
        return false;
    }

    // Mark subscription as provisioning
    db->execSqlSync(
        "UPDATE subscriptions SET "
        "  provision_status = 'provisioning', "
        "  provisioning_attempts = provisioning_attempts + 1, "
        "  last_provision_attempt = NOW(), "
        "  provisioning_started_at = COALESCE(provisioning_started_at, NOW()), "
        "  poll_count = 0, "
        "  poll_until = NOW() + INTERVAL '" + std::to_string(kMaxPollTimeSeconds) + " seconds', "
        "  updated_at = NOW() "
        "WHERE id = $1",
        subId);

    try {
        SyncResult syncResult;
        Json::Value requestPayload;

        if (remoteSystem == "zjmf_v10") {
            requestPayload = buildServerPayload(subId);
            syncResult = adapter->createServer(requestPayload);
        } else if (remoteSystem == "zjmf_finance") {
            requestPayload = buildInvoicePayload(subId);
            syncResult = adapter->createInvoice(requestPayload);
        } else {
            failProvisioning(subId, orderId,
                             "Unknown remote_system: " + remoteSystem);
            return false;
        }

        // Log the sync action
        int remoteId = syncResult.itemsCreated; // adapter reports created items count
        std::string remoteResourceId = std::to_string(remoteId);

        // Try to extract remote_id from syncResult
        Json::FastWriter writer;
        std::string responseStr = writer.write(syncResult.errorMessage.empty()
            ? Json::Value("ok") : Json::Value(syncResult.errorMessage));

        logSyncAction(connId, "outbound", "subscription", subId,
                      remoteResourceId,
                      (remoteSystem == "zjmf_v10") ? "createServer" : "createInvoice",
                      requestPayload,
                      syncResult.success
                          ? Json::Value("success")
                          : Json::Value(syncResult.errorMessage),
                      syncResult.success ? "success" : "failed",
                      syncResult.errorMessage);

        if (!syncResult.success) {
            failProvisioning(subId, orderId,
                             "Downstream API error: " + syncResult.errorMessage);
            return false;
        }

        // Update subscription with remote resource ID
        // The adapter returns the remote ID in itemsCreated for createServer/createInvoice
        if (remoteId > 0) {
            std::string remoteIdStr = std::to_string(remoteId);
            db->execSqlSync(
                "UPDATE subscriptions SET remote_resource_id = $1, "
                "updated_at = NOW() WHERE id = $2",
                remoteIdStr, subId);

            // Store entity mapping
            Json::Value remoteData;
            remoteData["remote_system"] = remoteSystem;
            remoteData["status"] = "provisioning";
            storeEntityMapping("subscription", subId, remoteSystem,
                               remoteIdStr, remoteData);
        }

        LOG_INFO << "[Provisioning] Submitted sub_id=" << subId
                 << " to " << remoteSystem
                 << " remote_id=" << remoteId;

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR << "[Provisioning] Exception provisioning sub_id=" << subId
                  << ": " << e.what();
        failProvisioning(subId, orderId,
                         std::string("Exception: ") + e.what());
        return false;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  completeProvisioning — handle successful provisioning
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFProvisioningService::completeProvisioning(
    int64_t subId, const std::string& remoteResourceId) {
    auto db = DbClient::getClient();

    LOG_INFO << "[Provisioning] Complete sub_id=" << subId
             << " remote_id=" << remoteResourceId;

    db->execSqlSync(
        "UPDATE subscriptions SET "
        "  provision_status = 'done', "
        "  status = 'active', "
        "  start_date = COALESCE(start_date, CURRENT_DATE), "
        "  provisioning_error = NULL, "
        "  poll_count = 0, "
        "  poll_until = NULL, "
        "  updated_at = NOW() "
        "WHERE id = $1 AND provision_status IN ('provisioning', 'pending')",
        subId);

    // Record timeline entry
    db->execSqlSync(
        "INSERT INTO subscription_timeline "
        "(subscription_id, from_status, to_status, operator_id, operator_name, remark) "
        "VALUES ($1, 'provisioning', 'active', 0, 'system', '开通完成')",
        subId);

    // Check if all subscriptions in the order are done, then activate the order
    auto orderRows = db->execSqlSync(
        "SELECT s.order_id FROM subscriptions s WHERE s.id = $1", subId);

    if (!orderRows.empty()) {
        int64_t orderId = orderRows[0]["order_id"].as<int64_t>();

        auto pendingRows = db->execSqlSync(
            "SELECT COUNT(*) AS cnt FROM subscriptions "
            "WHERE order_id = $1 AND provision_status != 'done'",
            orderId);

        int remaining = pendingRows.empty() ? 0 : pendingRows[0]["cnt"].as<int>();
        if (remaining == 0) {
            db->execSqlSync(
                "UPDATE orders SET status = 'active', updated_at = NOW() "
                "WHERE id = $1 AND status IN ('provisioning', 'approved')",
                orderId);

            db->execSqlSync(
                "INSERT INTO order_timeline "
                "(order_id, from_status, to_status, operator_id, operator_name, remark) "
                "VALUES ($1, 'provisioning', 'active', 0, 'system', '全部订阅开通完成')",
                orderId);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  failProvisioning — handle provisioning failure
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFProvisioningService::failProvisioning(
    int64_t subId, int64_t orderId, const std::string& errorMessage) {
    auto db = DbClient::getClient();

    LOG_ERROR << "[Provisioning] Failed sub_id=" << subId
              << " error=" << errorMessage;

    db->execSqlSync(
        "UPDATE subscriptions SET "
        "  provision_status = 'failed', "
        "  provisioning_error = $1, "
        "  poll_until = NULL, "
        "  updated_at = NOW() "
        "WHERE id = $2",
        errorMessage, subId);

    // Record timeline entry
    db->execSqlSync(
        "INSERT INTO subscription_timeline "
        "(subscription_id, from_status, to_status, operator_id, operator_name, remark) "
        "VALUES ($1, 'provisioning', 'provisioning_failed', 0, 'system', $2)",
        subId, "开通失败: " + errorMessage);

    // If orderId is known, mark order as provisioning_failed
    if (orderId > 0) {
        db->execSqlSync(
            "UPDATE orders SET status = 'provisioning_failed', updated_at = NOW() "
            "WHERE id = $1 AND status = 'provisioning'",
            orderId);

        db->execSqlSync(
            "INSERT INTO order_timeline "
            "(order_id, from_status, to_status, operator_id, operator_name, remark) "
            "VALUES ($1, 'provisioning', 'provisioning_failed', 0, 'system', "
            "'开通失败，需要人工介入')",
            orderId);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  storeEntityMapping
// ═══════════════════════════════════════════════════════════════════════════

void ZJMFProvisioningService::storeEntityMapping(
    const std::string& localType,
    int64_t localId,
    const std::string& remoteSystem,
    const std::string& remoteId,
    const Json::Value& remoteData) {
    try {
        auto db = DbClient::getClient();
        db->execSqlSync(
            "INSERT INTO zjmf_entity_mappings "
            "(local_type, local_id, remote_system, remote_id, remote_data) "
            "VALUES ($1, $2, $3, $4, $5::jsonb) "
            "ON CONFLICT (local_type, local_id, remote_system) "
            "DO UPDATE SET remote_id = $4, remote_data = $5::jsonb, "
            "last_synced_at = NOW()",
            localType, localId, remoteSystem, remoteId,
            Json::FastWriter().write(remoteData));
    } catch (const std::exception& e) {
        LOG_ERROR << "[Provisioning] Failed to store entity mapping: " << e.what();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  logSyncAction
// ═══════════════════════════════════════════════════════════════════════════

int64_t ZJMFProvisioningService::logSyncAction(
    int64_t connectionId,
    const std::string& direction,
    const std::string& entityType,
    int64_t entityId,
    const std::string& remoteId,
    const std::string& action,
    const Json::Value& requestData,
    const Json::Value& responseData,
    const std::string& status,
    const std::string& errorMessage) {
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "INSERT INTO zjmf_sync_logs "
            "(connection_id, direction, entity_type, entity_id, remote_id, "
            " action, request_data, response_data, status, error_message) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb, $8::jsonb, $9, $10) "
            "RETURNING id",
            connectionId, direction, entityType, entityId, remoteId,
            action,
            Json::FastWriter().write(requestData),
            Json::FastWriter().write(responseData),
            status, errorMessage);

        if (!result.empty()) {
            return result[0][0].as<int64_t>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[Provisioning] Failed to log sync action: " << e.what();
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  findDownstreamConnection
// ═══════════════════════════════════════════════════════════════════════════

int64_t ZJMFProvisioningService::findDownstreamConnection(
    const std::string& remoteSystem) {
    std::string connType;
    if (remoteSystem == "zjmf_v10") {
        connType = "v10";
    } else if (remoteSystem == "zjmf_finance") {
        connType = "finance";
    } else {
        return 0;
    }

    try {
        auto db = DbClient::getClient();
        auto rows = db->execSqlSync(
            "SELECT id FROM zjmf_connections "
            "WHERE type = $1 AND direction = 'downstream' AND status = 1 "
            "LIMIT 1",
            connType);

        if (!rows.empty()) {
            return rows[0]["id"].as<int64_t>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[Provisioning] findDownstreamConnection error: " << e.what();
    }

    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  buildServerPayload — build v10 createServer payload
// ═══════════════════════════════════════════════════════════════════════════

Json::Value ZJMFProvisioningService::buildServerPayload(int64_t subId) {
    auto db = DbClient::getClient();
    auto rows = db->execSqlSync(
        "SELECT s.sub_no, s.product_specs, s.distributor_id, "
        "       s.product_id, s.billing_cycle, "
        "       o.order_no, "
        "       d.name AS distributor_name "
        "FROM subscriptions s "
        "JOIN orders o ON o.id = s.order_id "
        "LEFT JOIN distributors d ON d.id = s.distributor_id "
        "WHERE s.id = $1",
        subId);

    Json::Value payload;
    if (rows.empty()) return payload;

    auto row = rows[0];
    payload["hostname"] = row["sub_no"].as<std::string>() + ".idc.internal";
    payload["notes"] = "来自分销平台订单 " + row["order_no"].as<std::string>()
                       + " 订阅 " + row["sub_no"].as<std::string>();

    // Parse specs for product configuration
    if (!row["product_specs"].isNull()) {
        std::string specsStr = row["product_specs"].as<std::string>();
        Json::Reader reader;
        Json::Value specs;
        if (reader.parse(specsStr, specs)) {
            // Map known spec fields to ZJMF payload
            if (specs.isMember("os")) {
                payload["os"] = specs["os"].asString();
            }
            if (specs.isMember("bandwidth")) {
                payload["bandwidth"] = specs["bandwidth"];
            }
            if (specs.isMember("ip_count")) {
                payload["ip_count"] = specs["ip_count"];
            }
            if (specs.isMember("ram")) {
                payload["ram"] = specs["ram"];
            }
            if (specs.isMember("cpu")) {
                payload["cpu"] = specs["cpu"];
            }
            if (specs.isMember("disk")) {
                payload["disk"] = specs["disk"];
            }
        }
    }

    payload["client_id"] = static_cast<Json::Int64>(row["distributor_id"].as<int64_t>());
    payload["product_id"] = static_cast<Json::Int64>(row["product_id"].as<int64_t>());

    return payload;
}

// ═══════════════════════════════════════════════════════════════════════════
//  buildInvoicePayload — build finance createInvoice payload
// ═══════════════════════════════════════════════════════════════════════════

Json::Value ZJMFProvisioningService::buildInvoicePayload(int64_t subId) {
    auto db = DbClient::getClient();
    auto rows = db->execSqlSync(
        "SELECT s.sub_no, s.product_specs, s.distributor_id, "
        "       s.product_id, s.billing_cycle, "
        "       o.order_no, oi.unit_price, oi.product_name, "
        "       oi.quantity, oi.period_months "
        "FROM subscriptions s "
        "JOIN orders o ON o.id = s.order_id "
        "JOIN order_items oi ON oi.order_id = o.id AND oi.product_id = s.product_id "
        "WHERE s.id = $1 "
        "LIMIT 1",
        subId);

    Json::Value payload;
    if (rows.empty()) return payload;

    auto row = rows[0];
    payload["client_id"] = static_cast<Json::Int64>(row["distributor_id"].as<int64_t>());

    // Build invoice items
    Json::Value items(Json::arrayValue);
    Json::Value item;
    item["description"] = row["product_name"].as<std::string>()
                          + " (" + row["sub_no"].as<std::string>() + ")";
    item["amount"] = row["unit_price"].as<std::string>();
    item["quantity"] = row["quantity"].as<int>();
    item["taxed"] = false;
    items.append(item);
    payload["items"] = items;

    payload["notes"] = "来自分销平台订单 " + row["order_no"].as<std::string>();

    return payload;
}

} // namespace idc
