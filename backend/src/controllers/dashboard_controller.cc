#include "dashboard_controller.h"
#include "utils/db_client.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <json/json.h>

#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/dashboard/stats
// ═══════════════════════════════════════════════════════════════════════════

void DashboardController::getStats(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto db = DbClient::getClient();
        Json::Value stats;

        // Total distributors (not deleted)
        auto distCount = db->execSqlSync(
            "SELECT COUNT(*) AS cnt FROM distributors WHERE status >= 0;");
        stats["total_distributors"] = distCount.empty()
            ? 0 : static_cast<Json::Int64>(distCount[0]["cnt"].as<int64_t>());

        // Active distributors
        auto activeDist = db->execSqlSync(
            "SELECT COUNT(*) AS cnt FROM distributors WHERE status = 1;");
        stats["active_distributors"] = activeDist.empty()
            ? 0 : static_cast<Json::Int64>(activeDist[0]["cnt"].as<int64_t>());

        // Total users (not deleted)
        auto userCount = db->execSqlSync(
            "SELECT COUNT(*) AS cnt FROM users WHERE status >= 0;");
        stats["total_users"] = userCount.empty()
            ? 0 : static_cast<Json::Int64>(userCount[0]["cnt"].as<int64_t>());

        // Total orders (excluding cancelled/rejected)
        auto orderStats = db->execSqlSync(
            "SELECT COUNT(*) AS cnt,"
            " COALESCE(SUM(final_amount), 0) AS revenue"
            " FROM orders WHERE status NOT IN ('cancelled','rejected');");
        if (!orderStats.empty()) {
            stats["total_orders"] = static_cast<Json::Int64>(
                orderStats[0]["cnt"].as<int64_t>());
            stats["total_revenue"] = orderStats[0]["revenue"].as<std::string>();
        } else {
            stats["total_orders"] = 0;
            stats["total_revenue"] = "0.00";
        }

        // Active subscriptions
        auto subCount = db->execSqlSync(
            "SELECT COUNT(*) AS cnt FROM subscriptions WHERE status = 'active';");
        stats["active_subscriptions"] = subCount.empty()
            ? 0 : static_cast<Json::Int64>(subCount[0]["cnt"].as<int64_t>());

        // Overdue invoices
        auto overdueInv = db->execSqlSync(
            "SELECT COUNT(*) AS cnt FROM invoices"
            " WHERE status = 'unpaid' AND due_date < CURRENT_DATE;");
        stats["overdue_invoices"] = overdueInv.empty()
            ? 0 : static_cast<Json::Int64>(overdueInv[0]["cnt"].as<int64_t>());

        // Monthly revenue (current month)
        auto monthlyRev = db->execSqlSync(
            "SELECT COALESCE(SUM(final_amount), 0) AS revenue"
            " FROM orders"
            " WHERE status NOT IN ('cancelled','rejected')"
            " AND date_trunc('month', created_at) = date_trunc('month', NOW());");
        stats["monthly_revenue"] = monthlyRev.empty()
            ? "0.00" : monthlyRev[0]["revenue"].as<std::string>();

        callback(JsonResponse::ok(stats));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Dashboard] getStats error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
