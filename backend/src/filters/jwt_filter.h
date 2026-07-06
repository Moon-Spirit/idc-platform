#pragma once

#include <drogon/HttpFilter.h>

namespace idc {

/// JWT authentication filter.
///
/// Extracts Bearer token from the Authorization header, verifies the HS256
/// signature, checks the Redis blacklist, and attaches user_id / role_id /
/// username to the request attributes on success.
///
/// Usage:
///   METHOD_ADD(MyCtrl::handler, "/path", Get, "JWTFilter");
///
/// On failure the filter short-circuits with:
///   401 — missing / invalid / expired token
///   401 — blacklisted token
///   503 — Redis unavailable (fail-closed)
class JWTFilter : public drogon::HttpFilter<JWTFilter> {
public:
    JWTFilter() = default;

    virtual void doFilter(const drogon::HttpRequestPtr&      req,
                          drogon::FilterCallback&&            fcb,
                          drogon::FilterChainCallback&&       fccb) override;
};

} // namespace idc
