#include "metered_controller.h"
#include "services/metered_billing.h"
#include "services/addon_billing.h"
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
//  GET /api/v1/subscriptions/{id}/metered
// ═══════════════════════════════════════════════════════════════════════════

void MeteredController::getMeteredUsage(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        std::string periodStart = req->getOptionalParameter<std::string>("period_start")
                                      .value_or("");
        std::string periodEnd   = req->getOptionalParameter<std::string>("period_end")
                                      .value_or("");

        if (periodStart.empty() || periodEnd.empty()) {
            callback(JsonResponse::error(400,
                "Query parameters 'period_start' and 'period_end' are required "
                "(format: YYYY-MM-DD)"));
            return;
        }

        auto data = MeteredBillingService::getMeteredUsage(
            id, periodStart, periodEnd);

        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Metered] getMeteredUsage error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/subscriptions/{id}/metered
// ═══════════════════════════════════════════════════════════════════════════

void MeteredController::recordRuntime(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        // Validate required fields
        if (!body->isMember("date") || !body->isMember("hours")) {
            callback(JsonResponse::error(400,
                "Missing required fields: 'date' (YYYY-MM-DD) and 'hours'"));
            return;
        }

        std::string date  = (*body)["date"].asString();
        double hours      = (*body)["hours"].asDouble();
        std::string source = body->get("source", "admin").asString();
        std::string remarks = body->get("remarks", "").asString();

        // Validate source value
        if (source != "auto" && source != "admin" && source != "zjmf_sync") {
            source = "admin";
        }

        auto data = MeteredBillingService::recordRuntime(
            id, date, hours, source, remarks);

        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Metered] recordRuntime error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/subscriptions/{id}/metered/calculate
// ═══════════════════════════════════════════════════════════════════════════

void MeteredController::calculateHourly(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("period_start") || !body->isMember("period_end")) {
            callback(JsonResponse::error(400,
                "Missing required fields: 'period_start' and 'period_end' "
                "(format: YYYY-MM-DD)"));
            return;
        }

        std::string periodStart = (*body)["period_start"].asString();
        std::string periodEnd   = (*body)["period_end"].asString();

        auto data = MeteredBillingService::calculateHourly(
            id, periodStart, periodEnd);

        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Metered] calculateHourly error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/subscriptions/{id}/addons
// ═══════════════════════════════════════════════════════════════════════════

void MeteredController::listAddons(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto data = AddonBillingService::listAddons(id);
        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Addon] listAddons error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/subscriptions/{id}/addons
// ═══════════════════════════════════════════════════════════════════════════

void MeteredController::createAddon(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("addon_type") || !body->isMember("monthly_price") ||
            !body->isMember("start_date")) {
            callback(JsonResponse::error(400,
                "Missing required fields: 'addon_type', 'monthly_price', "
                "'start_date'"));
            return;
        }

        std::string addonType    = (*body)["addon_type"].asString();
        double monthlyPrice      = (*body)["monthly_price"].asDouble();
        std::string startDate    = (*body)["start_date"].asString();
        Json::Value specs        = body->get("specs", Json::objectValue);

        auto data = AddonBillingService::createAddon(
            id, addonType, monthlyPrice, startDate, specs);

        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Addon] createAddon error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/addons/{addonId}
// ═══════════════════════════════════════════════════════════════════════════

void MeteredController::getAddon(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t addonId) {
    try {
        auto data = AddonBillingService::getAddonById(addonId);
        if (data.isNull()) {
            callback(JsonResponse::notFound("Addon service not found"));
            return;
        }
        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Addon] getAddon error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/addons/{addonId}/cancel
// ═══════════════════════════════════════════════════════════════════════════

void MeteredController::cancelAddon(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t addonId) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("end_date")) {
            callback(JsonResponse::error(400,
                "Missing required field: 'end_date' (YYYY-MM-DD)"));
            return;
        }

        std::string endDate = (*body)["end_date"].asString();

        auto data = AddonBillingService::cancelAddon(addonId, endDate);
        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Addon] cancelAddon error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
