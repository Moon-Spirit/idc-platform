#pragma once

#include <drogon/HttpController.h>

namespace idc {

/// Setup / installation wizard controller.
///
/// All endpoints are public — no auth required.
///
/// Endpoints:
///   POST /api/v1/setup/check-env   — check system requirements
///   POST /api/v1/setup/test-db     — test DB connection with provided credentials
///   POST /api/v1/setup/test-redis  — test Redis connection
///   POST /api/v1/setup/run         — execute installation
///   GET  /api/v1/setup/status      — check if system is already installed
class SetupController
    : public drogon::HttpController<SetupController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SetupController::checkEnv,
                  "/api/v1/setup/check-env", drogon::Post);
    ADD_METHOD_TO(SetupController::testDb,
                  "/api/v1/setup/test-db", drogon::Post);
    ADD_METHOD_TO(SetupController::testRedis,
                  "/api/v1/setup/test-redis", drogon::Post);
    ADD_METHOD_TO(SetupController::runInstall,
                  "/api/v1/setup/run", drogon::Post);
    ADD_METHOD_TO(SetupController::status,
                  "/api/v1/setup/status", drogon::Get);
    METHOD_LIST_END

    // -- Handlers ----------------------------------------------------------

    /// POST /api/v1/setup/check-env
    static void checkEnv(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/setup/test-db
    static void testDb(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/setup/test-redis
    static void testRedis(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/setup/run
    static void runInstall(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/setup/status
    static void status(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace idc
