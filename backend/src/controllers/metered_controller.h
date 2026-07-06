#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Metered billing & addon service API controller.
///
/// Handles hourly metered usage recording/querying and addon service
/// lifecycle management (extra_ips, ddos_protection, backup_storage).
///
/// All endpoints require JWT authentication.
/// Mutation endpoints require the corresponding RBAC permission.
///
/// Endpoints:
///   GET    /api/v1/subscriptions/{id}/metered          — get metered usage for period
///   POST   /api/v1/subscriptions/{id}/metered          — record runtime hours (admin/auto)
///   POST   /api/v1/subscriptions/{id}/metered/calculate — run hourly billing calc
///   GET    /api/v1/subscriptions/{id}/addons            — list addon services
///   POST   /api/v1/subscriptions/{id}/addons            — create addon service
///   GET    /api/v1/addons/{addonId}                     — get addon detail
///   PUT    /api/v1/addons/{addonId}/cancel              — cancel addon service
class MeteredController
    : public drogon::HttpController<MeteredController> {
public:
    METHOD_LIST_BEGIN
        // Metered usage
        ADD_METHOD_TO(MeteredController::getMeteredUsage,
                      "/api/v1/subscriptions/{id}/metered", drogon::Get,
                      "JWTFilter", "RBACFilter(metered:read)");
        ADD_METHOD_TO(MeteredController::recordRuntime,
                      "/api/v1/subscriptions/{id}/metered", drogon::Post,
                      "JWTFilter", "RBACFilter(metered:write)");
        ADD_METHOD_TO(MeteredController::calculateHourly,
                      "/api/v1/subscriptions/{id}/metered/calculate", drogon::Post,
                      "JWTFilter", "RBACFilter(metered:write)");

        // Addon services (scoped under subscription)
        ADD_METHOD_TO(MeteredController::listAddons,
                      "/api/v1/subscriptions/{id}/addons", drogon::Get,
                      "JWTFilter", "RBACFilter(addon:read)");
        ADD_METHOD_TO(MeteredController::createAddon,
                      "/api/v1/subscriptions/{id}/addons", drogon::Post,
                      "JWTFilter", "RBACFilter(addon:write)");

        // Addon detail & cancellation (resource-scoped)
        ADD_METHOD_TO(MeteredController::getAddon,
                      "/api/v1/addons/{addonId}", drogon::Get,
                      "JWTFilter", "RBACFilter(addon:read)");
        ADD_METHOD_TO(MeteredController::cancelAddon,
                      "/api/v1/addons/{addonId}/cancel", drogon::Put,
                      "JWTFilter", "RBACFilter(addon:write)");
    METHOD_LIST_END

    // ── Metered usage ─────────────────────────────────────────────────

    /// GET /api/v1/subscriptions/{id}/metered
    /// Query: ?period_start=2025-01-01&period_end=2025-01-31
    static void getMeteredUsage(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// POST /api/v1/subscriptions/{id}/metered
    /// Body: { "date": "2025-01-15", "hours": 12.5, "source": "auto" }
    static void recordRuntime(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// POST /api/v1/subscriptions/{id}/metered/calculate
    /// Body: { "period_start": "2025-01-01", "period_end": "2025-01-31" }
    static void calculateHourly(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    // ── Addon services ───────────────────────────────────────────────

    /// GET /api/v1/subscriptions/{id}/addons
    static void listAddons(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// POST /api/v1/subscriptions/{id}/addons
    /// Body: { "addon_type": "extra_ips", "monthly_price": 50.00,
    ///         "start_date": "2025-01-15", "specs": { "ip_count": 4 } }
    static void createAddon(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// GET /api/v1/addons/{addonId}
    static void getAddon(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t addonId);

    /// PUT /api/v1/addons/{addonId}/cancel
    /// Body: { "end_date": "2025-06-30" }
    static void cancelAddon(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t addonId);
};

} // namespace idc
