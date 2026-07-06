#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Dashboard statistics controller.
///
/// Endpoints:
///   GET  /api/v1/dashboard/stats  — aggregate platform statistics
class DashboardController
    : public drogon::HttpController<DashboardController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(DashboardController::getStats,
                      "/api/v1/dashboard/stats", drogon::Get,
                      "JWTFilter", "RBACFilter(system:read)");
    METHOD_LIST_END

    /// GET /api/v1/dashboard/stats
    /// Returns aggregate platform statistics (distributors, users, orders, etc.)
    static void getStats(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace idc
