#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Subscription (服务实例) management API controller.
///
/// All endpoints require JWT authentication.
/// Mutation endpoints (suspend/activate/terminate/upgrade) require admin RBAC.
///
/// Endpoints:
///   GET    /api/v1/subscriptions               — list subscriptions (paginated, status filter)
///   GET    /api/v1/subscriptions/{id}           — subscription detail with product info + timeline
///   PUT    /api/v1/subscriptions/{id}/suspend   — suspend service (admin: subscription:suspend)
///   PUT    /api/v1/subscriptions/{id}/activate  — resume suspended service (admin: subscription:update)
///   PUT    /api/v1/subscriptions/{id}/terminate — terminate service (admin: subscription:terminate)
///   POST   /api/v1/subscriptions/{id}/upgrade   — submit upgrade/downgrade (admin: subscription:update)
class SubscriptionController
    : public drogon::HttpController<SubscriptionController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(SubscriptionController::listSubscriptions,
                      "/api/v1/subscriptions", drogon::Get,
                      "JWTFilter", "RBACFilter(subscription:list)");
        ADD_METHOD_TO(SubscriptionController::getSubscription,
                      "/api/v1/subscriptions/{id}", drogon::Get,
                      "JWTFilter", "RBACFilter(subscription:read)");
        ADD_METHOD_TO(SubscriptionController::suspendSubscription,
                      "/api/v1/subscriptions/{id}/suspend", drogon::Put,
                      "JWTFilter", "RBACFilter(subscription:suspend)");
        ADD_METHOD_TO(SubscriptionController::activateSubscription,
                      "/api/v1/subscriptions/{id}/activate", drogon::Put,
                      "JWTFilter", "RBACFilter(subscription:update)");
        ADD_METHOD_TO(SubscriptionController::terminateSubscription,
                      "/api/v1/subscriptions/{id}/terminate", drogon::Put,
                      "JWTFilter", "RBACFilter(subscription:terminate)");
        ADD_METHOD_TO(SubscriptionController::upgradeSubscription,
                      "/api/v1/subscriptions/{id}/upgrade", drogon::Post,
                      "JWTFilter", "RBACFilter(subscription:update)");
    METHOD_LIST_END

    /// GET /api/v1/subscriptions
    /// Query: ?status=&distributor_id=&page=&per_page=
    static void listSubscriptions(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/subscriptions/{id}
    static void getSubscription(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// PUT /api/v1/subscriptions/{id}/suspend
    /// Body: { "reason": "逾期未付款" }
    static void suspendSubscription(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// PUT /api/v1/subscriptions/{id}/activate
    /// Body: { "reason": "已付款" }
    static void activateSubscription(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// PUT /api/v1/subscriptions/{id}/terminate
    /// Body: { "reason": "客户要求终止" }
    static void terminateSubscription(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// POST /api/v1/subscriptions/{id}/upgrade
    /// Body: { "specs": { "ram": "128GB", "disk": "4x480GB SSD" },
    ///         "effective_date": "immediate" }
    static void upgradeSubscription(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);
};

} // namespace idc
