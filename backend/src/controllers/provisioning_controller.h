#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Provisioning management API controller.
///
/// Endpoints:
///   POST /api/v1/provisioning/retry/{subscription_id}  — manual retry for failed provisioning
///   GET  /api/v1/provisioning/status/{subscription_id}  — current provisioning status
///
/// All endpoints require JWT authentication and RBAC permissions.
class ProvisioningController
    : public drogon::HttpController<ProvisioningController> {
public:
    METHOD_LIST_BEGIN
        // Manual retry (admin only: provisioning:retry)
        ADD_METHOD_TO(ProvisioningController::retryProvisioning,
                      "/api/v1/provisioning/retry/{subscription_id}",
                      drogon::Post,
                      "JWTFilter", "RBACFilter(provisioning:retry)");

        // View provisioning status (admin + dealer: provisioning:read)
        ADD_METHOD_TO(ProvisioningController::getProvisioningStatus,
                      "/api/v1/provisioning/status/{subscription_id}",
                      drogon::Get,
                      "JWTFilter", "RBACFilter(provisioning:read)");
    METHOD_LIST_END

    /// POST /api/v1/provisioning/retry/{subscription_id}
    /// Retry provisioning for a failed subscription.
    static void retryProvisioning(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t subscription_id);

    /// GET /api/v1/provisioning/status/{subscription_id}
    /// Get current provisioning status for a subscription.
    static void getProvisioningStatus(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t subscription_id);
};

} // namespace idc
