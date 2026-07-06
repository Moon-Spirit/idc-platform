#include "subscription_controller.h"
#include "services/subscription_service.h"
#include "utils/access_helpers.h"
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

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/subscriptions
// ═══════════════════════════════════════════════════════════════════════════

void SubscriptionController::listSubscriptions(
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

        auto data = SubscriptionService::listSubscriptions(
            userId, roleId, status, distributorId, page, perPage);

        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Subscription] listSubscriptions error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/subscriptions/{id}
// ═══════════════════════════════════════════════════════════════════════════

void SubscriptionController::getSubscription(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);

        auto sub = SubscriptionService::getSubscriptionById(id, userId, roleId);
        if (sub.isNull()) {
            callback(JsonResponse::notFound("Subscription not found"));
            return;
        }

        callback(JsonResponse::ok(sub));
    } catch (const std::invalid_argument& e) {
        // Permission-denied disguise as 404
        callback(JsonResponse::notFound("Subscription not found"));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Subscription] getSubscription error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/subscriptions/{id}/suspend
// ═══════════════════════════════════════════════════════════════════════════

void SubscriptionController::suspendSubscription(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        std::string reason;
        if (body && body->isMember("reason")) {
            reason = (*body)["reason"].asString();
        }

        auto result = SubscriptionService::suspendSubscription(
            id, getUserId(req), getUsername(req), getRoleId(req), reason);

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Subscription] suspendSubscription error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/subscriptions/{id}/activate
// ═══════════════════════════════════════════════════════════════════════════

void SubscriptionController::activateSubscription(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        std::string reason;
        if (body && body->isMember("reason")) {
            reason = (*body)["reason"].asString();
        }

        auto result = SubscriptionService::activateSubscription(
            id, getUserId(req), getUsername(req), getRoleId(req), reason);

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Subscription] activateSubscription error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/subscriptions/{id}/terminate
// ═══════════════════════════════════════════════════════════════════════════

void SubscriptionController::terminateSubscription(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        std::string reason;
        if (body && body->isMember("reason")) {
            reason = (*body)["reason"].asString();
        }

        auto result = SubscriptionService::terminateSubscription(
            id, getUserId(req), getUsername(req), getRoleId(req), reason);

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Subscription] terminateSubscription error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/subscriptions/{id}/upgrade
// ═══════════════════════════════════════════════════════════════════════════

void SubscriptionController::upgradeSubscription(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("specs")) {
            callback(JsonResponse::error(400, "Missing required field: specs"));
            return;
        }

        Json::Value newSpecs = (*body)["specs"];
        std::string effectiveDate = body->get("effective_date", "immediate").asString();

        auto result = SubscriptionService::submitUpgrade(
            id, newSpecs, effectiveDate,
            getUserId(req), getRoleId(req));

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Subscription] upgradeSubscription error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
