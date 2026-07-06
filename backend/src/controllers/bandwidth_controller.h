#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Bandwidth monitoring and billing API controller.
///
/// All endpoints require JWT authentication.
/// Simulation endpoint requires admin RBAC.
///
/// Endpoints:
///   GET  /api/v1/subscriptions/{id}/bandwidth  — bandwidth data for charts
///   POST /api/v1/bandwidth/simulate            — generate mock samples (admin)
class BandwidthController
    : public drogon::HttpController<BandwidthController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(BandwidthController::getBandwidth,
                      "/api/v1/subscriptions/{id}/bandwidth", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(BandwidthController::simulateBandwidth,
                      "/api/v1/bandwidth/simulate", drogon::Post,
                      "JWTFilter", "RBACFilter(bandwidth:simulate)");
    METHOD_LIST_END

    /// GET /api/v1/subscriptions/{id}/bandwidth
    /// Query: ?start=&end=
    ///
    /// Returns bandwidth data for charts:
    /// {
    ///   "subscription_id": ...,
    ///   "time_series": [ { time, port_id, avg_out_rate, max_out_rate, sample_count }, ... ],
    ///   "monthly_95th": {
    ///     "billing_rate": ...,
    ///     "total_samples": ...,
    ///     "rank_used": ...,
    ///     "amount": ...,
    ///     "port_results": [ ... ]
    ///   },
    ///   "daily_95th": [ { date, billing_rate, total_samples, rank_used }, ... ]
    /// }
    static void getBandwidth(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// POST /api/v1/bandwidth/simulate
    /// Body: {
    ///   "subscription_id": 1,
    ///   "port_ids": ["port-1", "port-2"],
    ///   "start": "2026-07-01 00:00:00",
    ///   "end": "2026-07-31 23:59:59",
    ///   "base_rate_mbps": 100,
    ///   "unit_price": 15.00
    /// }
    ///
    /// Generates mock bandwidth samples for testing the 95th percentile
    /// billing calculation. `port_ids` defaults to ["port-1"] if omitted.
    /// `base_rate_mbps` defaults to 100.
    static void simulateBandwidth(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace idc
