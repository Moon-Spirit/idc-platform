#include "provisioning_controller.h"
#include "services/zjmf_provisioning_service.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <string>

namespace idc {

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
//  POST /api/v1/provisioning/retry/{subscription_id}
// ═══════════════════════════════════════════════════════════════════════════

void ProvisioningController::retryProvisioning(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t subscription_id) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);
        std::string username = getUsername(req);

        auto result = ZJMFProvisioningService::retryProvisioning(
            subscription_id, userId, username);

        if (result.isMember("error")) {
            callback(JsonResponse::error(400, result["error"].asString()));
            return;
        }

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[ProvisioningController] retryProvisioning error: "
                      << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/provisioning/status/{subscription_id}
// ═══════════════════════════════════════════════════════════════════════════

void ProvisioningController::getProvisioningStatus(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t subscription_id) {
    try {
        auto status = ZJMFProvisioningService::getProvisionStatus(subscription_id);

        if (status.isNull()) {
            callback(JsonResponse::notFound("Subscription not found"));
            return;
        }

        callback(JsonResponse::ok(status));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[ProvisioningController] getProvisioningStatus error: "
                      << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
