#include "auth_controller.h"

#include "utils/config.h"
#include "utils/db_client.h"
#include "utils/jwt.h"
#include "utils/password.h"
#include "utils/redis_client.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>

#include <json/json.h>
#include <cstdint>

#include <ctime>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════════════════

static constexpr int kRateLimitMax     = 5;
static constexpr int kRateLimitWindow  = 60;

// ═══════════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════════

static int checkRateLimit(const std::string& ip) {
    std::string key = "rate_limit:login:" + ip;
    try {
        auto redis = RedisClient::getClient();
        int64_t count = redis->execCommandSync<int64_t>(
            [](const drogon::nosql::RedisResult& r) { return r.asInteger(); },
            "INCR %s", key.c_str());
        if (count == 1) {
            redis->execCommandSync<std::string>(
                [](const drogon::nosql::RedisResult& r) { return r.asString(); },
                "EXPIRE %s %d", key.c_str(), kRateLimitWindow);
        }
        return static_cast<int>(kRateLimitMax - count);
    } catch (const std::exception& e) {
        LOG_WARN << "[Auth] Rate-limit check failed (Redis): " << e.what();
        return -1; // fail-closed
    }
}

static Json::Value getPermissions(int64_t role_id) {
    auto db = DbClient::getClient();
    auto rows = db->execSqlSync(
        "SELECT p.code FROM permissions p "
        "JOIN role_permissions rp ON p.id = rp.permission_id "
        "WHERE rp.role_id = $1 ORDER BY p.code",
        role_id);
    Json::Value arr(Json::arrayValue);
    for (const auto& row : rows) {
        arr.append(row["code"].as<std::string>());
    }
    return arr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/auth/login
// ═══════════════════════════════════════════════════════════════════════════

void AuthController::login(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto json = req->getJsonObject();
    if (!json || !json->isMember("username") || !json->isMember("password")) {
        callback(JsonResponse::error(400, "username and password are required"));
        return;
    }

    std::string username = (*json)["username"].asString();
    std::string password = (*json)["password"].asString();
    if (username.empty() || password.empty()) {
        callback(JsonResponse::error(400, "username and password are required"));
        return;
    }

    // Rate limit
    std::string clientIp = req->getPeerAddr().toIp();
    int remaining = checkRateLimit(clientIp);
    if (remaining < 0) {
        callback(JsonResponse::error(503, "Authentication service unavailable",
                                     drogon::k503ServiceUnavailable));
        return;
    }
    if (remaining <= 0) {
        LOG_IDC_WARN(req, "[Auth] Rate limit exceeded for IP=" << clientIp);
        callback(JsonResponse::error(429, "Too many login attempts, try again later",
                                     drogon::k429TooManyRequests));
        return;
    }

    // Look up user
    auto db = DbClient::getClient();
    try {
        auto result = db->execSqlSync(
            "SELECT u.id, u.username, u.password_hash, u.status, "
            "       u.real_name, u.email, u.phone, "
            "       u.role_id, u.distributor_id, "
            "       r.name AS role_name "
            "FROM users u "
            "JOIN roles r ON r.id = u.role_id "
            "WHERE u.username = $1",
            username);

        if (result.empty()) {
            callback(JsonResponse::unauthorized("Invalid username or password"));
            return;
        }

        auto row = result[0];

        // Verify password
        if (!PasswordUtil::verify(password, row["password_hash"].as<std::string>())) {
            callback(JsonResponse::unauthorized("Invalid username or password"));
            return;
        }

        // Check status
        int16_t status = row["status"].as<int16_t>();
        if (status == 0) {
            callback(JsonResponse::forbidden("Account is disabled"));
            return;
        }
        if (status < 0) {
            callback(JsonResponse::unauthorized("Account has been deleted"));
            return;
        }

        // Build JWT
        int64_t  now     = static_cast<int64_t>(::time(nullptr));
        int64_t  userId  = row["id"].as<int64_t>();
        int64_t  roleId  = row["role_id"].as<int64_t>();
        std::string roleName = row["role_name"].as<std::string>();
        std::string jti   = drogon::utils::getUuid();

        JWTUtil::Payload payload;
        payload.user_id  = userId;
        payload.role_id  = roleId;
        payload.username = username;
        payload.jti      = jti;
        payload.iat      = now;
        payload.exp      = now + Config::jwtExpiresIn;

        std::string token = JWTUtil::sign(payload, Config::jwtSecret);

        // Update last_login_at (non-fatal)
        try {
            db->execSqlSync("UPDATE users SET last_login_at = NOW() WHERE id = $1", userId);
        } catch (...) {}

        // Get permissions
        Json::Value perms = getPermissions(roleId);

        // Build response
        Json::Value userData;
        userData["id"]              = static_cast<Json::Int64>(userId);
        userData["username"]        = username;
        userData["role"]            = roleName;
        userData["real_name"]       = row["real_name"].as<std::string>();
        userData["email"]           = row["email"].as<std::string>();
        userData["phone"]           = row["phone"].as<std::string>();
        userData["distributor_id"]  = row["distributor_id"].isNull()
            ? Json::Value(Json::nullValue)
            : Json::Value(static_cast<Json::Int64>(row["distributor_id"].as<int64_t>()));
        userData["permissions"]     = perms;

        Json::Value data;
        data["token"]       = token;
        data["expires_in"]  = Config::jwtExpiresIn;
        data["user"]        = userData;

        LOG_IDC_INFO(req, "[Auth] Login success: user_id=" << userId
                         << " role=" << roleName);
        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Auth] DB error: " << e.what());
        callback(JsonResponse::serverError("Internal server error"));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/auth/logout
// ═══════════════════════════════════════════════════════════════════════════

void AuthController::logout(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto attrs = req->getAttributes();
    if (!attrs->find("jti") || !attrs->find("token_exp")) {
        callback(JsonResponse::unauthorized("Invalid token"));
        return;
    }

    std::string jti = attrs->get<std::string>("jti");
    int ttl = static_cast<int>(attrs->get<int64_t>("token_exp")
                                - static_cast<int64_t>(::time(nullptr)));
    if (ttl <= 0) {
        callback(JsonResponse::ok(std::string("Already logged out")));
        return;
    }

    try {
        auto redis = RedisClient::getClient();
        redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult& r) { return r.asString(); },
            "SETEX %s %d %s",
            ("jwt:blacklist:" + jti).c_str(), ttl, "revoked");
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Auth] Redis blacklist failed: " << e.what());
        callback(JsonResponse::error(503, "Authentication service unavailable",
                                     drogon::k503ServiceUnavailable));
        return;
    }

    LOG_IDC_INFO(req, "[Auth] Logout success: jti=" << jti);
    callback(JsonResponse::ok(std::string("Logged out")));
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/auth/me
// ═══════════════════════════════════════════════════════════════════════════

void AuthController::me(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto attrs = req->getAttributes();
    if (!attrs->find("user_id")) {
        callback(JsonResponse::unauthorized());
        return;
    }
    int64_t userId = attrs->get<int64_t>("user_id");

    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "SELECT u.id, u.username, u.email, u.phone, u.real_name, "
            "       u.status, u.role_id, u.distributor_id, "
            "       u.last_login_at, u.created_at, "
            "       r.name AS role_name "
            "FROM users u "
            "JOIN roles r ON r.id = u.role_id "
            "WHERE u.id = $1", userId);

        if (result.empty()) {
            callback(JsonResponse::notFound("User not found"));
            return;
        }

        auto row = result[0];
        Json::Value u;
        u["id"]             = static_cast<Json::Int64>(row["id"].as<int64_t>());
        u["username"]       = row["username"].as<std::string>();
        u["real_name"]      = row["real_name"].as<std::string>();
        u["email"]          = row["email"].as<std::string>();
        u["phone"]          = row["phone"].as<std::string>();
        u["role"]           = row["role_name"].as<std::string>();
        u["role_id"]        = static_cast<Json::Int64>(row["role_id"].as<int64_t>());
        u["distributor_id"] = row["distributor_id"].isNull()
            ? Json::Value(Json::nullValue)
            : Json::Value(static_cast<Json::Int64>(row["distributor_id"].as<int64_t>()));
        u["status"]         = row["status"].as<int16_t>();
        u["last_login_at"]   = row["last_login_at"].isNull()
            ? Json::Value(Json::nullValue)
            : Json::Value(row["last_login_at"].as<std::string>());
        u["created_at"]      = row["created_at"].as<std::string>();
        u["permissions"]     = getPermissions(row["role_id"].as<int64_t>());

        callback(JsonResponse::ok(u));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Auth] /me error: " << e.what());
        callback(JsonResponse::serverError("Internal server error"));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/auth/refresh
// ═══════════════════════════════════════════════════════════════════════════

void AuthController::refresh(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ") {
        callback(JsonResponse::unauthorized("Missing token"));
        return;
    }
    std::string oldToken = authHeader.substr(7);

    auto payload = JWTUtil::verify(oldToken, Config::jwtSecret);
    if (!payload) {
        callback(JsonResponse::unauthorized("Invalid or expired token"));
        return;
    }

    int64_t now = static_cast<int64_t>(::time(nullptr));

    JWTUtil::Payload newPayload;
    newPayload.user_id  = payload->user_id;
    newPayload.role_id  = payload->role_id;
    newPayload.username = payload->username;
    newPayload.jti      = drogon::utils::getUuid();
    newPayload.iat      = now;
    newPayload.exp      = now + Config::jwtExpiresIn;

    std::string newToken = JWTUtil::sign(newPayload, Config::jwtSecret);

    // Blacklist old token
    int oldTtl = static_cast<int>(payload->exp - now);
    if (oldTtl > 0) {
        try {
            auto redis = RedisClient::getClient();
            redis->execCommandSync<std::string>(
                [](const drogon::nosql::RedisResult& r) { return r.asString(); },
                "SETEX %s %d %s",
                ("jwt:blacklist:" + payload->jti).c_str(), oldTtl, "refreshed");
        } catch (const std::exception& e) {
            LOG_IDC_WARN(req, "[Auth] Refresh: old token blacklist failed: "
                              << e.what());
        }
    }

    Json::Value data;
    data["token"]      = newToken;
    data["expires_in"] = Config::jwtExpiresIn;

    LOG_IDC_INFO(req, "[Auth] Token refreshed for user_id=" << payload->user_id);
    callback(JsonResponse::ok(data));
}

} // namespace idc
