#pragma once

#include <drogon/HttpController.h>

namespace idc {

/// User management controller (admin only).
///
/// Endpoints:
///   GET  /api/v1/users            — list users (paginated)
///   POST /api/v1/users            — create a new user (with bcrypt hash)
///   PUT  /api/v1/users/:id/password  — change password for a user
class UserController
    : public drogon::HttpController<UserController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::listUsers,
                  "/api/v1/users", drogon::Get,
                  "JWTFilter", "RBACFilter(user:list)");
    ADD_METHOD_TO(UserController::createUser,
                  "/api/v1/users", drogon::Post,
                  "JWTFilter", "RBACFilter(user:create)");
    ADD_METHOD_TO(UserController::updatePassword,
                  "/api/v1/users/:id/password", drogon::Put,
                  "JWTFilter", "RBACFilter(user:update)");
    METHOD_LIST_END

    // -- Handlers ----------------------------------------------------------

    /// GET /api/v1/users?page=1&per_page=20&keyword=
    static void listUsers(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/users
    /// Body: { "username": "...", "password": "...", "role_id": 1, ... }
    static void createUser(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// PUT /api/v1/users/:id/password
    /// Body: { "old_password": "...", "new_password": "..." }
    static void updatePassword(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace idc
