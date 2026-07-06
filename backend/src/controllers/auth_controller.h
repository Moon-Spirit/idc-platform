#pragma once

#include <drogon/HttpController.h>

namespace idc {

/// Authentication & session controller.
///
/// Endpoints:
///   POST /api/v1/auth/login    — authenticate, return JWT
///   POST /api/v1/auth/logout   — revoke current token
///   GET  /api/v1/auth/me       — current user profile
///   POST /api/v1/auth/refresh  — rotate token
class AuthController
    : public drogon::HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::login,  "/api/v1/auth/login",  drogon::Post);
    ADD_METHOD_TO(AuthController::logout, "/api/v1/auth/logout", drogon::Post,
                  "JWTFilter");
    ADD_METHOD_TO(AuthController::me,     "/api/v1/auth/me",     drogon::Get,
                  "JWTFilter");
    ADD_METHOD_TO(AuthController::refresh,"/api/v1/auth/refresh",drogon::Post,
                  "JWTFilter");
    METHOD_LIST_END

    // -- Handlers ----------------------------------------------------------

    /// POST /api/v1/auth/login
    static void login(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/auth/logout
    static void logout(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/auth/me
    static void me(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/auth/refresh
    static void refresh(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace idc
