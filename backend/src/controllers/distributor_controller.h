#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Distributor (经销商) management API controller.
///
/// All endpoints are protected by JWTFilter. Mutation endpoints additionally
/// require RBACFilter with distributor:create / distributor:update permissions.
///
/// Endpoints:
///   GET  /api/v1/distributors              — paginated list with filters
///   POST /api/v1/distributors              — create (admin: distributor:create)
///   GET  /api/v1/distributors/{id}         — detail with stats
///   PUT  /api/v1/distributors/{id}         — update (admin: distributor:update)
///   GET  /api/v1/distributors/{id}/tree    — recursive children tree
///   GET  /api/v1/distributors/{id}/children— direct children
///   GET  /api/v1/distributors/{id}/statistics — monthly statistics
class DistributorController
    : public drogon::HttpController<DistributorController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(DistributorController::listDistributors,
                      "/api/v1/distributors", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(DistributorController::createDistributor,
                      "/api/v1/distributors", drogon::Post,
                      "JWTFilter", "RBACFilter(distributor:create)");
        ADD_METHOD_TO(DistributorController::getDistributor,
                      "/api/v1/distributors/{id}", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(DistributorController::updateDistributor,
                      "/api/v1/distributors/{id}", drogon::Put,
                      "JWTFilter", "RBACFilter(distributor:update)");
        ADD_METHOD_TO(DistributorController::getDistributorTree,
                      "/api/v1/distributors/{id}/tree", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(DistributorController::getDirectChildren,
                      "/api/v1/distributors/{id}/children", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(DistributorController::getDistributorStatistics,
                      "/api/v1/distributors/{id}/statistics", drogon::Get,
                      "JWTFilter");
    METHOD_LIST_END

    // ── Handlers ──────────────────────────────────────────────────────────

    static void listDistributors(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    static void createDistributor(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    static void getDistributor(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    static void updateDistributor(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    static void getDistributorTree(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    static void getDirectChildren(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    static void getDistributorStatistics(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);
};

} // namespace idc
