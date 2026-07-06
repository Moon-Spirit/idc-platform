#include "bandwidth_controller.h"
#include "services/bandwidth_billing.h"
#include "services/bandwidth_sampler.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: extract JWT attributes from request
// ═══════════════════════════════════════════════════════════════════════════

namespace {
    int64_t getUserId(const drogon::HttpRequestPtr& req) {
        return req->getAttributes()->get<int64_t>("user_id");
    }

    int64_t getRoleId(const drogon::HttpRequestPtr& req) {
        return req->getAttributes()->get<int64_t>("role_id");
    }
} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/subscriptions/{id}/bandwidth
// ═══════════════════════════════════════════════════════════════════════════

void BandwidthController::getBandwidth(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);

        // Query parameters for time range
        std::string start = req->getOptionalParameter<std::string>("start")
                                .value_or("");
        std::string end = req->getOptionalParameter<std::string>("end")
                              .value_or("");

        // If no range specified, default to current month
        if (start.empty() || end.empty()) {
            auto db = drogon::app().getDbClient("idc_db");
            auto dateResult = db->execSqlSync(
                "SELECT date_trunc('month', NOW())::text AS month_start, "
                "NOW()::text AS now");
            if (!dateResult.empty()) {
                if (start.empty()) start = dateResult[0]["month_start"].as<std::string>();
                if (end.empty()) end = dateResult[0]["now"].as<std::string>();
            }
        }

        // Fetch subscription to verify it exists and check permissions
        auto db = drogon::app().getDbClient("idc_db");
        auto subResult = db->execSqlSync(
            "SELECT s.id, s.sub_no, s.distributor_id, "
            "d.name AS distributor_name, "
            "p.name AS product_name, p.type AS product_type "
            "FROM subscriptions s "
            "LEFT JOIN distributors d ON d.id = s.distributor_id "
            "LEFT JOIN products p ON p.id = s.product_id "
            "WHERE s.id = $1",
            id);

        if (subResult.empty()) {
            callback(JsonResponse::notFound("Subscription not found"));
            return;
        }

        // Permission check: dealers can only see their own subscriptions
        bool isAdmin = (roleId == 1);
        if (!isAdmin) {
            auto userDistResult = db->execSqlSync(
                "SELECT distributor_id FROM users WHERE id = $1", userId);
            if (!userDistResult.empty()) {
                int64_t userDistId = userDistResult[0]["distributor_id"].isNull()
                    ? 0 : userDistResult[0]["distributor_id"].as<int64_t>();
                int64_t subDistId = subResult[0]["distributor_id"].as<int64_t>();
                if (userDistId != subDistId) {
                    callback(JsonResponse::notFound("Subscription not found"));
                    return;
                }
            }
        }

        // Step 1: Get time-series data
        Json::Value timeSeries = BandwidthBillingService::getTimeSeries(
            id, start, end);

        // Step 2: Get monthly 95th percentile calculation
        BandwidthBillResult monthlyBill =
            BandwidthBillingService::calculate95thPercentile(
                id, start, end, 0.0); // unit_price=0 to get rate only

        // Step 3: Get daily 95th percentile values
        Json::Value daily95th = BandwidthBillingService::getDaily95thPercentiles(
            id, start, end);

        // Build response
        Json::Value data;
        data["subscription_id"] = static_cast<Json::Int64>(id);
        data["sub_no"] = subResult[0]["sub_no"].isNull()
            ? "" : subResult[0]["sub_no"].as<std::string>();
        data["product_name"] = subResult[0]["product_name"].isNull()
            ? "" : subResult[0]["product_name"].as<std::string>();
        data["product_type"] = subResult[0]["product_type"].isNull()
            ? "" : subResult[0]["product_type"].as<std::string>();
        data["period_start"] = start;
        data["period_end"] = end;

        // Time series (hourly aggregated)
        data["time_series"] = timeSeries;

        // Monthly 95th percentile
        Json::Value monthly95;
        monthly95["billing_rate"] = monthlyBill.billingRate;
        monthly95["total_samples"] = static_cast<Json::Int64>(monthlyBill.totalSamples);
        monthly95["rank_used"] = static_cast<Json::Int64>(monthlyBill.rankUsed);
        monthly95["amount"] = monthlyBill.amount;
        monthly95["used_fallback"] = monthlyBill.usedFallback;
        monthly95["port_results"] = monthlyBill.portResults;
        data["monthly_95th"] = monthly95;

        // Daily 95th percentile chart
        data["daily_95th"] = daily95th;

        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Bandwidth] getBandwidth error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/bandwidth/simulate
// ═══════════════════════════════════════════════════════════════════════════

void BandwidthController::simulateBandwidth(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        // Validate required fields
        if (!body->isMember("subscription_id")) {
            callback(JsonResponse::error(400, "Missing required field: subscription_id"));
            return;
        }
        if (!body->isMember("start") || !body->isMember("end")) {
            callback(JsonResponse::error(400, "Missing required fields: start, end"));
            return;
        }

        int64_t subscriptionId = (*body)["subscription_id"].asInt64();

        // Optional port_ids (default to ["port-1"])
        Json::Value portIds;
        if (body->isMember("port_ids") && (*body)["port_ids"].isArray()) {
            portIds = (*body)["port_ids"];
        } else {
            portIds = Json::arrayValue;
            portIds.append("port-1");
        }

        std::string start = (*body)["start"].asString();
        std::string end = (*body)["end"].asString();
        double baseRateMbps = body->get("base_rate_mbps", 100.0).asDouble();

        // Generate mock samples
        Json::Value simResult = BandwidthSampler::generateMockSamples(
            subscriptionId, portIds, start, end, baseRateMbps);

        // If there was an error, return it
        if (simResult.isMember("error")) {
            callback(JsonResponse::error(400, simResult["error"].asString()));
            return;
        }

        callback(JsonResponse::ok(simResult));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Bandwidth] simulateBandwidth error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
