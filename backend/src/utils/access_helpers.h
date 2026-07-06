#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpAppFramework.h>
#include <cstdint>
#include <string>

namespace idc {

/// Check if the given role_id corresponds to an administrator.
inline bool isAdmin(int64_t roleId) {
    return roleId == 1;
}

/// Resolve the distributor_id for the current request.
///
/// Admin users can override via ?distributor_id= query parameter.
/// Non-admin users always get their own distributor_id from the users table.
///
/// Returns 0 if the user has no distributor assigned.
inline int64_t resolveDistributorId(const drogon::HttpRequestPtr& req) {
    auto attrs = req->getAttributes();
    int64_t roleId = attrs->get<int64_t>("role_id");
    int64_t userId = attrs->get<int64_t>("user_id");

    // Admin can pass distributor_id query param
    if (roleId == 1) {
        auto distId = req->getOptionalParameter<int64_t>("distributor_id");
        if (distId.has_value()) {
            return distId.value();
        }
    }

    // Get distributor_id from user record
    auto db = drogon::app().getDbClient("idc_db");
    auto result = db->execSqlSync(
        "SELECT distributor_id FROM users WHERE id = $1", userId);
    if (result.empty() || result[0]["distributor_id"].isNull()) {
        return 0;
    }
    return result[0]["distributor_id"].as<int64_t>();
}

} // namespace idc
