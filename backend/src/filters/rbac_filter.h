#pragma once

#include <drogon/HttpFilter.h>
#include <string>

namespace idc {

/// RBAC permission filter.
///
/// Checks whether the authenticated user's role has the required permission
/// code. Permission lookups are cached in Redis for 1 hour.
///
/// Usage in path registration:
///   ADD_METHOD_TO(MyCtrl::handler, "/api/v1/users", Get,
///                 "JWTFilter", "RBACFilter(user:list)");
///
/// The filter parameter is the permission code (e.g. "user:create").
///
/// On failure:
///   403 — role lacks the required permission
///   503 — database unavailable
class RBACFilter : public drogon::HttpFilter<RBACFilter> {
public:
    /// Default constructor (required by Drogon for parameterless usage).
    RBACFilter() = default;

    /// Parameterized constructor — Drogon passes the permission code string.
    explicit RBACFilter(const std::string& param);

    virtual void doFilter(const drogon::HttpRequestPtr&      req,
                          drogon::FilterCallback&&            fcb,
                          drogon::FilterChainCallback&&       fccb) override;

private:
    std::string permission_; ///< Required permission code (e.g. "user:list")

    /// Check if a role has the permission by querying PG (with Redis cache).
    static bool checkPermission(int64_t role_id, const std::string& perm);

    /// Fetch permission codes for a role from PostgreSQL.
    static std::set<std::string> fetchRolePermissions(int64_t role_id);
};

} // namespace idc
