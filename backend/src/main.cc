#include <drogon/drogon.h>
#include "utils/error_handler.h"
#include "utils/logger.h"
#include "utils/config.h"

#include "controllers/auth_controller.h"
#include "controllers/distributor_controller.h"
#include "controllers/user_controller.h"

#include <iostream>

int main() {
    // ── Load configuration ───────────────────────────────────────────────────
    drogon::app().loadConfigFile("config.json");

    // ── Load JWT config from custom section ─────────────────────────────────
    idc::Config::init();

    // ── Register global error handler ────────────────────────────────────────
    idc::ErrorHandler::init();

    // ── CORS pre-flight & common headers ─────────────────────────────────────
    drogon::app().registerPreRoutingAdvice(
        [](const drogon::HttpRequestPtr& req,
           drogon::AdviceCallback&&      callback,
           drogon::AdviceChainCallback&&  chained) {
            // Short-circuit OPTIONS preflight
            if (req->method() == drogon::Options) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k200OK);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                resp->addHeader("Access-Control-Allow-Methods",
                                "GET, POST, PUT, DELETE, PATCH, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers",
                                "Content-Type, Authorization, X-Request-ID");
                resp->addHeader("Access-Control-Max-Age", "86400");
                callback(resp);
                return;
            }
            chained();
        });

    // ── Inject request ID into every request ─────────────────────────────────
    drogon::app().registerPreRoutingAdvice(
        [](const drogon::HttpRequestPtr& req,
           drogon::AdviceCallback&& /*callback*/,
           drogon::AdviceChainCallback&& chained) {
            // If client sent one, reuse it; otherwise generate
            auto reqId = req->getHeader("X-Request-ID");
            if (reqId.empty()) {
                reqId = idc::Logger::generateId();
            }
            idc::Logger::setRequestId(req, reqId);
            chained();
        });

    // ── Attach CORS headers to every response ────────────────────────────────
    drogon::app().registerPostHandlingAdvice(
        [](const drogon::HttpRequestPtr& req,
           const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("X-Request-ID",
                            idc::Logger::getRequestId(req));
        });

    // ── Warn about Redis but do not block startup ────────────────────────────
    //     Drogon will try to connect lazily; if the pool is empty we log a
    //     note so operators know Redis is in degraded mode.
    try {
        auto redis = drogon::app().getRedisClient("idc_redis");
        if (!redis) {
            LOG_WARN << "[Startup] Redis client 'idc_redis' not available — "
                        "running in degraded mode";
        }
    } catch (const std::exception& e) {
        LOG_WARN << "[Startup] Redis client 'idc_redis' unavailable: "
                 << e.what() << " — running in degraded mode";
    }

    // ── Start server ─────────────────────────────────────────────────────────
    std::cout << "IDC Platform API Server started on port 8080" << std::endl;

    drogon::app().addListener("0.0.0.0", 8080).run();

    return 0;
}
