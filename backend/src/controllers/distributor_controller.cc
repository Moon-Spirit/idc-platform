#include "distributor_controller.h"
#include "services/distributor_service.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/distributors
// ═══════════════════════════════════════════════════════════════════════════

void DistributorController::listDistributors(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        // Query parameters
        int page     = req->getOptionalParameter<int>("page").value_or(1);
        int perPage  = req->getOptionalParameter<int>("per_page").value_or(20);
        int16_t status  = static_cast<int16_t>(
            req->getOptionalParameter<int>("status").value_or(-1));
        int16_t level   = static_cast<int16_t>(
            req->getOptionalParameter<int>("level").value_or(0));
        std::string keyword = req->getOptionalParameter<std::string>("keyword")
                                .value_or("");
        int64_t parentId = static_cast<int64_t>(
            req->getOptionalParameter<int64_t>("parent_id").value_or(-1));

        // Clamp pagination
        if (page < 1) page = 1;
        if (perPage < 1) perPage = 20;
        if (perPage > 100) perPage = 100;

        auto data = DistributorService::listDistributors(
            page, perPage, status, level, keyword, parentId);
        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Distributor] listDistributors error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/distributors
// ═══════════════════════════════════════════════════════════════════════════

void DistributorController::createDistributor(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("name")) {
            callback(JsonResponse::error(400, "Missing required field: name"));
            return;
        }

        int64_t newId = DistributorService::createDistributor(*body);

        Json::Value data;
        data["id"] = static_cast<Json::Int64>(newId);
        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Distributor] createDistributor error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/distributors/:id
// ═══════════════════════════════════════════════════════════════════════════

void DistributorController::getDistributor(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto distributor = DistributorService::getDistributorById(id);
        if (distributor.isNull()) {
            callback(JsonResponse::notFound("Distributor not found"));
            return;
        }
        callback(JsonResponse::ok(distributor));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Distributor] getDistributor error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/distributors/:id
// ═══════════════════════════════════════════════════════════════════════════

void DistributorController::updateDistributor(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        DistributorService::updateDistributor(id, *body);
        callback(JsonResponse::ok(std::string("Distributor updated")));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Distributor] updateDistributor error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/distributors/:id/tree
// ═══════════════════════════════════════════════════════════════════════════

void DistributorController::getDistributorTree(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto tree = DistributorService::getDistributorTree(id);
        if (tree.isNull()) {
            callback(JsonResponse::notFound("Distributor not found"));
            return;
        }
        callback(JsonResponse::ok(tree));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Distributor] getDistributorTree error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/distributors/:id/children
// ═══════════════════════════════════════════════════════════════════════════

void DistributorController::getDirectChildren(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto children = DistributorService::getDirectChildren(id);
        callback(JsonResponse::ok(children));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Distributor] getDirectChildren error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/distributors/:id/statistics
// ═══════════════════════════════════════════════════════════════════════════

void DistributorController::getDistributorStatistics(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto stats = DistributorService::getDistributorStatistics(id);
        if (stats.isNull()) {
            callback(JsonResponse::notFound("Distributor not found"));
            return;
        }
        callback(JsonResponse::ok(stats));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Distributor] getDistributorStatistics error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
