#pragma once

#include <drogon/drogon.h>
#include <drogon/HttpMiddleware.h>
#include "response.h"

#include <exception>
#include <string>

namespace idc {

/// Global exception handler.
///
/// Installs a custom exception handler on the Drogon framework so every
/// unhandled exception from a controller / handler is caught and returned as
/// a structured JSON error response.
///
/// Handled exception types:
///   - drogon::DrogonException      → 500
///   - drogon::orm::DrogonDbException → 500
///   - std::exception               → 500
///   - unknown (...)                → 500
///
/// Usage in main.cc:
///   idc::ErrorHandler::init();
class ErrorHandler {
public:
    /// Register the global exception handler on the app framework.
    static void init() {
        // ── Global exception handler ───────────────────────────────────────
        drogon::app().setExceptionHandler(
            [](const std::exception& ex,
               const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                LOG_ERROR << "[ErrorHandler] Unhandled exception: " << ex.what()
                          << " | request: " << (req ? req->path() : "(null)");

                callback(JsonResponse::serverError(ex.what()));
            });

        // ── Middleware to catch unknown exception types ────────────────────
        drogon::app().registerPreRoutingAdvice(
            [](const drogon::HttpRequestPtr& req,
               drogon::AdviceCallback&&        callback,
               drogon::AdviceChainCallback&&    chained) {
                try {
                    chained();
                } catch (const std::exception& e) {
                    LOG_ERROR << "[ErrorHandler] std::exception: " << e.what();
                    callback(JsonResponse::serverError(e.what()));
                } catch (...) {
                    LOG_ERROR << "[ErrorHandler] Unknown exception";
                    callback(JsonResponse::serverError("Unknown internal error"));
                }
            });
    }
};

}  // namespace idc
