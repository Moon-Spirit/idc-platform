#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Balance management API controller.
///
/// All endpoints require JWT authentication.
///
/// Endpoints:
///   GET   /api/v1/balance              — Current user's balance
///   POST  /api/v1/balance/top-up       — Admin: add balance for distributor
class BalanceController
    : public drogon::HttpController<BalanceController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(BalanceController::getBalance,
                      "/api/v1/balance", drogon::Get,
                      "JWTFilter", "RBACFilter(payment:read)");
        ADD_METHOD_TO(BalanceController::topUp,
                      "/api/v1/balance/top-up", drogon::Post,
                      "JWTFilter", "RBACFilter(distributor:update)");
    METHOD_LIST_END

    /// GET /api/v1/balance
    /// Returns the current user's distributor balance.
    static void getBalance(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/balance/top-up
    /// Admin-only: add funds to a distributor's prepaid balance.
    /// Body: { "distributor_id": 1, "amount": "1000.00", "remark": "" }
    static void topUp(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    /// Helper: extract user_id from JWT attributes.
    static int64_t getUserId(const drogon::HttpRequestPtr& req);

    /// Helper: extract role_id from JWT attributes.
    static int64_t getRoleId(const drogon::HttpRequestPtr& req);

    /// Helper: get distributor_id for the current user.
    static int64_t getUserDistributorId(int64_t userId);
};

} // namespace idc
