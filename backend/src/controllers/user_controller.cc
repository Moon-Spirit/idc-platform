#include "user_controller.h"

#include "utils/config.h"
#include "utils/db_client.h"
#include "utils/password.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>

#include <json/json.h>

#include <string>
#include <cstdint>

namespace idc {

/// Map a DB row to a JSON user object (excludes password_hash).
static Json::Value rowToUserJson(const drogon::orm::Row& row) {
    Json::Value item;
    item["id"]              = static_cast<Json::Int64>(row["id"].as<int64_t>());
    item["username"]        = row["username"].as<std::string>();
    item["real_name"]       = row["real_name"].as<std::string>();
    item["email"]           = row["email"].as<std::string>();
    item["phone"]           = row["phone"].as<std::string>();
    item["role"]            = row["role_name"].as<std::string>();
    item["role_id"]         = static_cast<Json::Int64>(row["role_id"].as<int64_t>());
    item["distributor_id"]  = row["distributor_id"].isNull()
                                ? Json::Value(Json::nullValue)
                                : Json::Value(static_cast<Json::Int64>(
                                     row["distributor_id"].as<int64_t>()));
    item["status"]          = row["status"].as<int16_t>();
    item["last_login_at"]   = row["last_login_at"].isNull()
                                ? Json::Value(Json::nullValue)
                                : Json::Value(row["last_login_at"].as<std::string>());
    item["created_at"]      = row["created_at"].as<std::string>();
    return item;
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/users
// ═══════════════════════════════════════════════════════════════════════════

void UserController::listUsers(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    // -- 1. Parse pagination params -----------------------------------------
    int page     = req->getOptionalParameter<int>("page").value_or(1);
    int perPage  = req->getOptionalParameter<int>("per_page").value_or(20);
    std::string keyword = req->getOptionalParameter<std::string>("keyword").value_or("");

    if (page < 1)     page = 1;
    if (perPage < 1)  perPage = 20;
    if (perPage > 100) perPage = 100;

    int offset = (page - 1) * perPage;

    auto db = DbClient::getClient();
    try {
        std::string baseJoin =
            "FROM users u "
            "JOIN roles r ON r.id = u.role_id "
            "WHERE u.status >= 0";

        std::string countSql = "SELECT COUNT(*) AS total " + baseJoin;
        std::string listSql =
            "SELECT u.id, u.username, u.email, u.phone, u.real_name, "
            "       u.status, u.role_id, u.distributor_id, "
            "       u.last_login_at, u.created_at, "
            "       r.name AS role_name "
            + baseJoin;

        // Execute DB queries — inline all values to avoid Drogon PG bind issues
        bool hasKeyword = !keyword.empty();
        std::string likeClause;
        if (hasKeyword) {
            // Escape single quotes in keyword for inline SQL
            std::string kw = keyword;
            for (size_t i = 0; i < kw.size(); ++i) {
                if (kw[i] == '\'') { kw.insert(i, "'"); ++i; }
            }
            std::string pattern = "%" + kw + "%";
            likeClause =
                " AND (u.username ILIKE '" + pattern + "'"
                " OR u.real_name ILIKE '" + pattern + "'"
                " OR u.email ILIKE '" + pattern + "'"
                " OR u.phone ILIKE '" + pattern + "')";
        }

        auto countRes = db->execSqlSync(countSql + likeClause + ";");
        auto rows = db->execSqlSync(
            listSql + likeClause +
            " ORDER BY u.created_at DESC LIMIT " + std::to_string(perPage) +
            " OFFSET " + std::to_string(offset) + ";");

        int64_t total = countRes[0]["total"].as<int64_t>();

        Json::Value items(Json::arrayValue);
        for (const auto& row : rows) {
            items.append(rowToUserJson(row));
        }

        Json::Value data;
        data["items"]    = items;
        data["total"]    = static_cast<Json::Int64>(total);
        data["page"]     = page;
        data["per_page"] = perPage;

        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[User] listUsers DB error: " << e.what());
        callback(JsonResponse::serverError("Internal server error"));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/users
// ═══════════════════════════════════════════════════════════════════════════

void UserController::createUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto json = req->getJsonObject();
    if (!json) {
        callback(JsonResponse::error(400, "Request body must be JSON"));
        return;
    }

    std::string username = (*json).get("username", "").asString();
    std::string password = (*json).get("password", "").asString();
    int64_t     roleId   = (*json).get("role_id", 0).asInt64();

    if (username.empty() || password.empty() || roleId <= 0) {
        callback(JsonResponse::error(400,
            "username, password, and role_id are required"));
        return;
    }

    if (password.length() < 6) {
        callback(JsonResponse::error(400,
            "Password must be at least 6 characters"));
        return;
    }

    std::string email    = (*json).get("email", "").asString();
    std::string phone    = (*json).get("phone", "").asString();
    std::string realName = (*json).get("real_name", "").asString();
    int64_t distributorId = (*json).get("distributor_id", 0).asInt64();

    auto db = DbClient::getClient();
    try {
        // Check duplicate username
        auto existing = db->execSqlSync(
            "SELECT id FROM users WHERE username = $1", username);
        if (!existing.empty()) {
            callback(JsonResponse::error(409, "Username already exists"));
            return;
        }

        // bcrypt cost=12
        std::string passwordHash = PasswordUtil::hash(password, 12);

        auto result = db->execSqlSync(
            "INSERT INTO users (username, password_hash, email, phone, "
            "                  real_name, role_id, distributor_id) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7) "
            "RETURNING id, created_at",
            username, passwordHash,
            email.empty() ? nullptr : email.c_str(),
            phone.empty() ? nullptr : phone.c_str(),
            realName.empty() ? nullptr : realName.c_str(),
            roleId,
            distributorId > 0 ? std::optional<int64_t>(distributorId) : std::nullopt);

        if (result.empty()) {
            callback(JsonResponse::serverError("Failed to create user"));
            return;
        }

        int64_t newId = result[0]["id"].as<int64_t>();

        Json::Value data;
        data["id"]         = static_cast<Json::Int64>(newId);
        data["username"]   = username;
        data["role_id"]    = static_cast<Json::Int64>(roleId);
        data["created_at"] = result[0]["created_at"].as<std::string>();

        LOG_IDC_INFO(req, "[User] Created user id=" << newId
                        << " username=" << username);
        callback(JsonResponse::ok(data));
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_IDC_ERROR(req, "[User] createUser DB error: " << e.base().what());
        callback(JsonResponse::serverError("Database error"));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/users/:id/password
// ═══════════════════════════════════════════════════════════════════════════

void UserController::updatePassword(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    // -- 1. Parse path parameter --------------------------------------------
    auto userIdStr = req->getOptionalParameter<std::string>("id");
    if (!userIdStr) {
        callback(JsonResponse::error(400, "User ID is required"));
        return;
    }

    int64_t targetUserId;
    try {
        targetUserId = std::stoll(*userIdStr);
    } catch (...) {
        callback(JsonResponse::error(400, "Invalid user ID"));
        return;
    }

    // -- 2. Parse body ------------------------------------------------------
    auto json = req->getJsonObject();
    if (!json || !json->isMember("new_password")) {
        callback(JsonResponse::error(400, "new_password is required"));
        return;
    }

    std::string newPassword = (*json)["new_password"].asString();
    if (newPassword.length() < 6) {
        callback(JsonResponse::error(400,
            "Password must be at least 6 characters"));
        return;
    }

    // -- 3. Permission: admin can change any; non-admin only own ------------
    auto attrs = req->getAttributes();
    if (!attrs->find("user_id") || !attrs->find("role_id")) {
        callback(JsonResponse::unauthorized());
        return;
    }
    int64_t currentUserId = attrs->get<int64_t>("user_id");
    bool isAdmin = (attrs->get<int64_t>("role_id") == 1); // admin role_id=1

    if (!isAdmin && currentUserId != targetUserId) {
        callback(JsonResponse::forbidden(
            "You can only change your own password"));
        return;
    }

    // -- 4. Verify old password if changing own -----------------------------
    if (currentUserId == targetUserId && json->isMember("old_password")) {
        std::string oldPassword = (*json)["old_password"].asString();
        auto db = DbClient::getClient();
        try {
            auto result = db->execSqlSync(
                "SELECT password_hash FROM users WHERE id = $1",
                targetUserId);
            if (!result.empty()) {
                std::string currentHash = result[0]["password_hash"].as<std::string>();
                if (!PasswordUtil::verify(oldPassword, currentHash)) {
                    callback(JsonResponse::error(400, "Old password is incorrect"));
                    return;
                }
            }
        } catch (const std::exception& e) {
            LOG_IDC_ERROR(req, "[User] Password verify error: " << e.what());
            callback(JsonResponse::serverError("Internal server error"));
            return;
        }
    }

    // -- 5. Hash and update -------------------------------------------------
    auto db = DbClient::getClient();
    try {
        std::string newHash = PasswordUtil::hash(newPassword, 12);

        auto result = db->execSqlSync(
            "UPDATE users SET password_hash = $1, updated_at = NOW() "
            "WHERE id = $2 AND status >= 0",
            newHash, targetUserId);

        if (result.affectedRows() == 0) {
            callback(JsonResponse::notFound("User not found or disabled"));
            return;
        }

        LOG_IDC_INFO(req, "[User] Password updated for user_id=" << targetUserId
                        << " by user_id=" << currentUserId);
        callback(JsonResponse::ok(std::string("Password updated")));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[User] updatePassword DB error: " << e.what());
        callback(JsonResponse::serverError("Internal server error"));
    }
}

} // namespace idc
