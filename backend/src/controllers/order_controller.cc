#include "order_controller.h"
#include "services/order_service.h"
#include "services/product_service.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: extract JWT attributes from request
// ═══════════════════════════════════════════════════════════════════════════

namespace {
    int64_t getUserId(const drogon::HttpRequestPtr& req) {
        return req->getAttributes()->get<int64_t>("user_id");
    }

    int64_t getRoleId(const drogon::HttpRequestPtr& req) {
        return req->getAttributes()->get<int64_t>("role_id");
    }

    std::string getUsername(const drogon::HttpRequestPtr& req) {
        return req->getAttributes()->get<std::string>("username");
    }

    /// Get the distributor_id for the current user.
    /// Admin can pass ?distributor_id= to act on behalf of another.
    int64_t resolveDistributorId(const drogon::HttpRequestPtr& req) {
        auto attrs = req->getAttributes();
        int64_t roleId = attrs->get<int64_t>("role_id");
        int64_t userId = attrs->get<int64_t>("user_id");

        // Admin can pass distributor_id query param
        if (roleId == 1) {
            auto distId = req->getOptionalParameter<int64_t>("distributor_id");
            if (distId.has_value()) {
                return distId.value();
            }
        }

        // Get distributor_id from user record
        auto db = drogon::app().getDbClient("idc_db");
        auto result = db->execSqlSync(
            "SELECT distributor_id FROM users WHERE id = $1", userId);
        if (result.empty() || result[0]["distributor_id"].isNull()) {
            return 0;
        }
        return result[0]["distributor_id"].as<int64_t>();
    }
} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/orders
// ═══════════════════════════════════════════════════════════════════════════

void OrderController::submitOrder(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);
        int64_t distributorId = resolveDistributorId(req);

        if (distributorId <= 0) {
            callback(JsonResponse::error(400, "User has no distributor assigned"));
            return;
        }

        std::string billingCycle = body->get("billing_cycle", "").asString();
        std::string remark = body->get("remark", "").asString();

        auto result = OrderService::createOrder(
            userId, roleId, distributorId, billingCycle, remark);

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Order] submitOrder error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/orders
// ═══════════════════════════════════════════════════════════════════════════

void OrderController::listOrders(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);

        // Query parameters
        int page = req->getOptionalParameter<int>("page").value_or(1);
        int perPage = req->getOptionalParameter<int>("per_page").value_or(20);
        std::string status = req->getOptionalParameter<std::string>("status")
                               .value_or("");
        int64_t distributorId = static_cast<int64_t>(
            req->getOptionalParameter<int64_t>("distributor_id").value_or(-1));

        // Clamp pagination
        if (page < 1) page = 1;
        if (perPage < 1) perPage = 20;
        if (perPage > 100) perPage = 100;

        auto data = OrderService::listOrders(
            userId, roleId, status, distributorId, page, perPage);

        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Order] listOrders error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/orders/{id}
// ═══════════════════════════════════════════════════════════════════════════

void OrderController::getOrder(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);

        auto order = OrderService::getOrderById(id, userId, roleId);
        if (order.isNull()) {
            callback(JsonResponse::notFound("Order not found"));
            return;
        }

        callback(JsonResponse::ok(order));
    } catch (const std::invalid_argument& e) {
        // Permission-denied disguise as 404
        callback(JsonResponse::notFound("Order not found"));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Order] getOrder error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/orders/{id}/approve
// ═══════════════════════════════════════════════════════════════════════════

void OrderController::approveOrder(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        std::string remark;
        if (body && body->isMember("remark")) {
            remark = (*body)["remark"].asString();
        }

        auto result = OrderService::transitionOrder(
            id, "approved",
            getUserId(req), getUsername(req),
            getRoleId(req), remark);

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Order] approveOrder error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/orders/{id}/reject
// ═══════════════════════════════════════════════════════════════════════════

void OrderController::rejectOrder(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        std::string reason;
        if (body && body->isMember("reason")) {
            reason = (*body)["reason"].asString();
        } else {
            reason = "订单被驳回";
        }

        auto result = OrderService::transitionOrder(
            id, "rejected",
            getUserId(req), getUsername(req),
            getRoleId(req), reason);

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Order] rejectOrder error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/orders/{id}/cancel
// ═══════════════════════════════════════════════════════════════════════════

void OrderController::cancelOrder(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        std::string remark;
        if (body && body->isMember("remark")) {
            remark = (*body)["remark"].asString();
        }

        auto result = OrderService::transitionOrder(
            id, "cancelled",
            getUserId(req), getUsername(req),
            getRoleId(req), remark);

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Order] cancelOrder error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
