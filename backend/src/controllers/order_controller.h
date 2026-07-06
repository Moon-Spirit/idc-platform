#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Order management API controller.
///
/// All endpoints require JWT authentication.
/// State transition endpoints (approve/reject) require admin RBAC permissions.
///
/// Endpoints:
///   POST   /api/v1/orders              — submit order (cart → order)
///   GET    /api/v1/orders              — list orders (paginated, status filter)
///   GET    /api/v1/orders/{id}         — order detail with items + timeline
///   PUT    /api/v1/orders/{id}/approve — approve order (admin: order:approve)
///   PUT    /api/v1/orders/{id}/reject  — reject order (admin: order:reject)
///   PUT    /api/v1/orders/{id}/cancel  — cancel order (dealer or admin)
class OrderController : public drogon::HttpController<OrderController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(OrderController::submitOrder,
                      "/api/v1/orders", drogon::Post,
                      "JWTFilter");
        ADD_METHOD_TO(OrderController::listOrders,
                      "/api/v1/orders", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(OrderController::getOrder,
                      "/api/v1/orders/{id}", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(OrderController::approveOrder,
                      "/api/v1/orders/{id}/approve", drogon::Put,
                      "JWTFilter", "RBACFilter(order:approve)");
        ADD_METHOD_TO(OrderController::rejectOrder,
                      "/api/v1/orders/{id}/reject", drogon::Put,
                      "JWTFilter", "RBACFilter(order:reject)");
        ADD_METHOD_TO(OrderController::cancelOrder,
                      "/api/v1/orders/{id}/cancel", drogon::Put,
                      "JWTFilter");
    METHOD_LIST_END

    /// POST /api/v1/orders
    /// Body: { billing_cycle, remark }
    static void submitOrder(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/orders
    /// Query: ?status=&distributor_id=&page=&per_page=
    static void listOrders(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/orders/{id}
    static void getOrder(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         int64_t id);

    /// PUT /api/v1/orders/{id}/approve
    /// Body: { remark }
    static void approveOrder(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             int64_t id);

    /// PUT /api/v1/orders/{id}/reject
    /// Body: { reason }
    static void rejectOrder(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            int64_t id);

    /// PUT /api/v1/orders/{id}/cancel
    /// Body: { remark }
    static void cancelOrder(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            int64_t id);
};

} // namespace idc
