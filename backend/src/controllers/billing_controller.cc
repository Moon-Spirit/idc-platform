#include "billing_controller.h"
#include "services/billing_engine.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/billing/run
// ═══════════════════════════════════════════════════════════════════════════

void BillingController::runBilling(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        // Parse optional cycle parameter
        std::string cycle = "both";
        auto body = req->getJsonObject();
        if (body && body->isMember("cycle")) {
            cycle = (*body)["cycle"].asString();
            if (cycle != "monthly" && cycle != "yearly" && cycle != "both") {
                callback(JsonResponse::error(400,
                    "Invalid cycle. Must be 'monthly', 'yearly', or 'both'"));
                return;
            }
        }

        Json::Value result;
        Json::Value monthlyResult;
        Json::Value yearlyResult;

        if (cycle == "monthly" || cycle == "both") {
            LOG_IDC_INFO(req, "[Billing] Manual monthly billing trigger");
            monthlyResult = BillingEngine::runMonthlyBilling();
        }

        if (cycle == "yearly" || cycle == "both") {
            LOG_IDC_INFO(req, "[Billing] Manual yearly billing trigger");
            yearlyResult = BillingEngine::runYearlyBilling();
        }

        if (cycle == "both") {
            result["monthly"] = monthlyResult;
            result["yearly"]  = yearlyResult;
        } else if (cycle == "monthly") {
            result = monthlyResult;
        } else {
            result = yearlyResult;
        }

        callback(JsonResponse::ok(result));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Billing] runBilling error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/billing/status
// ═══════════════════════════════════════════════════════════════════════════

void BillingController::getBillingStatus(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto status = BillingEngine::getLastRunStatus();
        callback(JsonResponse::ok(status));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Billing] getBillingStatus error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
