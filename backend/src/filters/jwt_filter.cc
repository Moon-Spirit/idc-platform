#include "jwt_filter.h"

#include "utils/config.h"
#include "utils/jwt.h"
#include "utils/redis_client.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/drogon.h>

namespace idc {

void JWTFilter::doFilter(const drogon::HttpRequestPtr&      req,
                         drogon::FilterCallback&&            fcb,
                         drogon::FilterChainCallback&&       fccb) {
    // -- 1. Extract Bearer token -------------------------------------------
    auto authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ") {
        LOG_IDC_WARN(req, "[JWTFilter] Missing or invalid Authorization header");
        fcb(JsonResponse::unauthorized("Missing or invalid Authorization header"));
        return;
    }

    std::string token = authHeader.substr(7); // strip "Bearer "

    // -- 2. Verify JWT signature & expiry ----------------------------------
    auto payload = JWTUtil::verify(token, Config::jwtSecret);
    if (!payload) {
        LOG_IDC_WARN(req, "[JWTFilter] Invalid or expired token");
        fcb(JsonResponse::unauthorized("Invalid or expired token"));
        return;
    }

    // -- 3. Check Redis blacklist (fail-closed) -----------------------------
    std::string blacklistKey = "jwt:blacklist:" + payload->jti;
    try {
        auto redis = RedisClient::getClient();
        auto count = redis->execCommandSync<int64_t>(
            [](const drogon::nosql::RedisResult& r) { return r.asInteger(); },
            "EXISTS %s", blacklistKey.c_str());
        if (count > 0) {
            LOG_IDC_WARN(req, "[JWTFilter] Token is blacklisted (jti="
                              << payload->jti << ")");
            fcb(JsonResponse::unauthorized("Token has been revoked"));
            return;
        }
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[JWTFilter] Redis unavailable for blacklist check: "
                           << e.what());
        fcb(JsonResponse::error(503, "Authentication service unavailable",
                                drogon::k503ServiceUnavailable));
        return;
    }

    // -- 4. Attach user context to request attributes -----------------------
    auto attrs = req->getAttributes();
    attrs->insert("user_id",   payload->user_id);
    attrs->insert("role_id",   payload->role_id);
    attrs->insert("username",  payload->username);
    attrs->insert("jti",       payload->jti);
    attrs->insert("token_exp", payload->exp);

    LOG_IDC_DEBUG(req, "[JWTFilter] Authenticated user_id=" << payload->user_id
                       << " role_id=" << payload->role_id);

    fccb();
}

} // namespace idc
