#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// ZJMF integration API controller.
///
/// Endpoints:
///   GET   /api/v1/sync/zjmf/status    — connection + sync status dashboard
///   POST  /api/v1/sync/zjmf/products  — manual product sync trigger
///   POST  /api/v1/sync/zjmf/inventory — manual inventory sync trigger
///   GET   /api/v1/sync/logs           — sync logs with filters
///   POST  /api/v1/webhooks/zjmf       — webhook receiver for ZJMF callbacks
class ZJMFController
    : public drogon::HttpController<ZJMFController> {
public:
    METHOD_LIST_BEGIN
        // Sync status dashboard (admin + dealer: readable)
        ADD_METHOD_TO(ZJMFController::getSyncStatus,
                      "/api/v1/sync/zjmf/status", drogon::Get,
                      "JWTFilter", "RBACFilter(sync:read)");

        // Manual sync triggers (admin only)
        ADD_METHOD_TO(ZJMFController::triggerProductSync,
                      "/api/v1/sync/zjmf/products", drogon::Post,
                      "JWTFilter", "RBACFilter(sync:trigger)");
        ADD_METHOD_TO(ZJMFController::triggerInventorySync,
                      "/api/v1/sync/zjmf/inventory", drogon::Post,
                      "JWTFilter", "RBACFilter(sync:trigger)");

        // Sync logs
        ADD_METHOD_TO(ZJMFController::getSyncLogs,
                      "/api/v1/sync/logs", drogon::Get,
                      "JWTFilter", "RBACFilter(sync:read)");

        // Webhook receiver (no auth — signature verification)
        ADD_METHOD_TO(ZJMFController::handleWebhook,
                      "/api/v1/webhooks/zjmf", drogon::Post);
    METHOD_LIST_END

    /// GET /api/v1/sync/zjmf/status
    /// Returns connection status and last sync info for dashboard.
    static void getSyncStatus(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/sync/zjmf/products
    /// Manually trigger product sync for a specific connection (or all).
    /// Body: { "connection_id": 123 } — optional, syncs all if omitted.
    static void triggerProductSync(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/sync/zjmf/inventory
    /// Manually trigger inventory sync.
    /// Body: { "connection_id": 123 } — optional.
    static void triggerInventorySync(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/sync/logs
    /// Query sync logs with optional filters.
    /// Query params: connection_id, entity_type, status, page, per_page
    static void getSyncLogs(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/webhooks/zjmf
    /// Receive ZJMF webhook callbacks.
    /// Verifies HMAC-SHA256 signature from X-ZJMF-Signature header.
    /// Supports: order_status, payment, server events.
    static void handleWebhook(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    /// Verify the webhook signature.
    /// @return true if signature is valid
    static bool verifyWebhookSignature(const drogon::HttpRequestPtr& req);

    /// Process a webhook payload (delegates to appropriate handler).
    static Json::Value processWebhook(const drogon::HttpRequestPtr& req,
                                      const Json::Value& payload);
};

} // namespace idc
