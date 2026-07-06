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
        // ── std::exception handler (covers DrogonException, orm exceptions) ─
        drogon::app().setCustomExceptionHandler(
            [](const std::exception& ex,
               const drogon::HttpRequestPtr& req) -> drogon::HttpResponsePtr {
                LOG_ERROR << "[ErrorHandler] Unhandled exception: " << ex.what()
                          << " | request: " << (req ? req->path() : "(null)");

                return JsonResponse::serverError(ex.what());
            });

        // ── Register a middleware to catch non-std::exception throws ────────
        //     (bare strings, ints, etc.) that the custom handler can't reach.
        drogon::app().registerPreRoutingAdvice(
            [](const drogon::HttpRequestPtr& req,
               drogon::AdviceCallback&&        callback,
               drogon::AdviceChainCallback&&    chained) {
                try {
                    chained();
                } catch (const drogon::DrogonException& e) {
                    LOG_ERROR << "[ErrorHandler] DrogonException: " << e.what();
                    callback(JsonResponse::serverError(e.what()));
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
