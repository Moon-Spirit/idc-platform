#include "rbac_filter.h"

#include "utils/config.h"
#include "utils/db_client.h"
#include "utils/redis_client.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include <set>
#include <sstream>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Constructor
// ═══════════════════════════════════════════════════════════════════════════

RBACFilter::RBACFilter(const std::string& param) : permission_(param) {
}

// ═══════════════════════════════════════════════════════════════════════════
//  doFilter
// ═══════════════════════════════════════════════════════════════════════════

void RBACFilter::doFilter(const drogon::HttpRequestPtr&      req,
                          drogon::FilterCallback&&            fcb,
                          drogon::FilterChainCallback&&       fccb) {
    if (permission_.empty()) {
        fccb();
        return;
    }

    auto attrs = req->getAttributes();
    if (!attrs->find("role_id")) {
        LOG_IDC_WARN(req, "[RBACFilter] No role_id in request");
        fcb(JsonResponse::unauthorized("Authentication required"));
        return;
    }

    int64_t role_id = attrs->get<int64_t>("role_id");

    if (!checkPermission(role_id, permission_)) {
        LOG_IDC_WARN(req, "[RBACFilter] Forbidden: role_id=" << role_id
                          << " needs permission=" << permission_);
        fcb(JsonResponse::forbidden(
            "Insufficient permissions: " + permission_ + " required"));
        return;
    }

    LOG_IDC_DEBUG(req, "[RBACFilter] Granted: role_id=" << role_id
                       << " permission=" << permission_);
    fccb();
}

// ═══════════════════════════════════════════════════════════════════════════
//  checkPermission
// ═══════════════════════════════════════════════════════════════════════════

bool RBACFilter::checkPermission(int64_t role_id, const std::string& perm) {
    std::string cacheKey = "role_perms:" + std::to_string(role_id);

    // -- Try Redis ---------------------------------------------------------
    std::string cachedPerms;
    try {
        auto redis = RedisClient::getClient();
        cachedPerms = redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult& r) -> std::string {
                if (r.type() == drogon::nosql::RedisResultType::kNil)
                    return {};
                return r.asString();
            },
            "GET %s", cacheKey.c_str());
    } catch (const std::exception& e) {
        LOG_WARN << "[RBACFilter] Redis GET failed, falling back to PG: "
                 << e.what();
    }

    std::set<std::string> perms;

    if (!cachedPerms.empty()) {
        std::istringstream ss(cachedPerms);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty()) perms.insert(line);
        }
    } else {
        // Cache miss → PG
        try {
            perms = fetchRolePermissions(role_id);
        } catch (const std::exception& e) {
            LOG_ERROR << "[RBACFilter] DB query failed for role_id=" << role_id
                      << ": " << e.what();
            return false;
        }

        if (!perms.empty()) {
            std::string joined;
            for (const auto& p : perms) joined += p + "\n";
            try {
                auto redis = RedisClient::getClient();
                redis->execCommandSync<std::string>(
                    [](const drogon::nosql::RedisResult& r) { return r.asString(); },
                    "SETEX %s 3600 %s",
                    cacheKey.c_str(), joined.c_str());
            } catch (const std::exception& e) {
                LOG_WARN << "[RBACFilter] Redis cache write failed: " << e.what();
            }
        }
    }

    return perms.find(perm) != perms.end();
}

// ═══════════════════════════════════════════════════════════════════════════
//  fetchRolePermissions
// ═══════════════════════════════════════════════════════════════════════════

std::set<std::string> RBACFilter::fetchRolePermissions(int64_t role_id) {
    auto db = DbClient::getClient();
    auto rows = db->execSqlSync(
        "SELECT p.code "
        "FROM permissions p "
        "JOIN role_permissions rp ON p.id = rp.permission_id "
        "WHERE rp.role_id = $1",
        role_id);
    std::set<std::string> perms;
    for (const auto& row : rows) {
        perms.insert(row["code"].as<std::string>());
    }
    return perms;
}

} // namespace idc
