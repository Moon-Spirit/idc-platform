#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Billing operation API controller.
///
/// Endpoints:
///   POST  /api/v1/billing/run     — manually trigger billing (admin: billing:run)
///   GET   /api/v1/billing/status  — check last billing run status (admin: billing:read)
class BillingController
    : public drogon::HttpController<BillingController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(BillingController::runBilling,
                      "/api/v1/billing/run", drogon::Post,
                      "JWTFilter", "RBACFilter(billing:run)");
        ADD_METHOD_TO(BillingController::getBillingStatus,
                      "/api/v1/billing/status", drogon::Get,
                      "JWTFilter", "RBACFilter(billing:read)");
    METHOD_LIST_END

    /// POST /api/v1/billing/run
    /// Manually trigger a billing run.
    ///
    /// Optional body: { "cycle": "monthly"|"yearly" } — defaults to both.
    /// Returns billing summary JSON.
    static void runBilling(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/billing/status
    /// Get the last billing run status.
    static void getBillingStatus(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace idc
