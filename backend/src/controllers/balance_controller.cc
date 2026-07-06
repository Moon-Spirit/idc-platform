#include "balance_controller.h"
#include "services/balance_service.h"
#include "utils/response.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════════

int64_t BalanceController::getUserId(const drogon::HttpRequestPtr& req) {
    return req->getAttributes()->get<int64_t>("user_id");
}

int64_t BalanceController::getRoleId(const drogon::HttpRequestPtr& req) {
    return req->getAttributes()->get<int64_t>("role_id");
}

int64_t BalanceController::getUserDistributorId(int64_t userId) {
    auto db = DbClient::getClient();
    auto result = db->execSqlSync(
        "SELECT distributor_id FROM users WHERE id = $1", userId);
    if (result.empty()) return 0;
    auto val = result[0]["distributor_id"];
    return val.isNull() ? 0 : val.as<int64_t>();
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/balance
// ═══════════════════════════════════════════════════════════════════════════

void BalanceController::getBalance(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);
        bool isAdmin = (roleId == 1);

        int64_t distributorId = 0;

        // Admin can query any distributor's balance via ?distributor_id=
        if (isAdmin) {
            distributorId = static_cast<int64_t>(
                req->getOptionalParameter<int64_t>("distributor_id").value_or(0));
        }

        // If no distributor_id specified (or non-admin), use the user's own
        if (distributorId <= 0) {
            distributorId = getUserDistributorId(userId);
            if (distributorId <= 0) {
                if (isAdmin) {
                    // Admin users may not have a distributor; return zero balance
                    Json::Value zeroBalance;
                    zeroBalance["balance"] = "0.00";
                    zeroBalance["credit_limit"] = "0.00";
                    zeroBalance["distributor_id"] = Json::nullValue;
                    callback(JsonResponse::ok(zeroBalance));
                    return;
                }
                callback(JsonResponse::error(403,
                    "No distributor account associated with this user"));
                return;
            }
        }

        auto balance = BalanceService::getBalance(distributorId);
        if (balance.isNull()) {
            callback(JsonResponse::notFound("Distributor not found"));
            return;
        }

        callback(JsonResponse::ok(balance));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Balance] getBalance error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/balance/top-up
// ═══════════════════════════════════════════════════════════════════════════

void BalanceController::topUp(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        int64_t operatorId = getUserId(req);

        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("distributor_id") || !body->isMember("amount")) {
            callback(JsonResponse::error(400,
                "Missing required fields: distributor_id, amount"));
            return;
        }

        int64_t distributorId = static_cast<int64_t>(
            (*body)["distributor_id"].asInt64());
        double amount = std::stod((*body)["amount"].asString());
        std::string remark = body->get("remark", "").asString();

        auto result = BalanceService::topUpBalance(
            distributorId, amount, operatorId, remark);

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Balance] topUp error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
