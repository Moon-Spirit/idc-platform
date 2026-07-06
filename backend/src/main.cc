#include <drogon/drogon.h>
#include "utils/error_handler.h"
#include "utils/logger.h"
#include "utils/config.h"

#include "controllers/auth_controller.h"
#include "controllers/bandwidth_controller.h"
#include "controllers/distributor_controller.h"
#include "controllers/user_controller.h"
#include "controllers/billing_controller.h"
#include "controllers/invoice_controller.h"
#include "controllers/payment_controller.h"
#include "controllers/balance_controller.h"
#include "controllers/zjmf_controller.h"
#include "controllers/provisioning_controller.h"
#include "cron/billing_cron.h"
#include "cron/dunning_cron.h"
#include "cron/zjmf_sync_cron.h"
#include "services/zjmf_adapter.h"
#include "services/zjmf_provisioning_service.h"

#include <fstream>
#include <iostream>
#include <json/json.h>

int main() {
    // ── Load configuration with env var overrides ──────────────────────────
    // Read config.json, apply environment variable overrides, then pass the
    // modified JSON to Drogon's loadConfigJson.
    {
        std::ifstream ifs("config.json");
        Json::Value configJson;
        Json::CharReaderBuilder reader;
        std::string errs;
        if (Json::parseFromStream(reader, ifs, &configJson, &errs)) {
            // Override DB password from IDC_DB_PASSWORD env var
            const char* dbPassword = std::getenv("IDC_DB_PASSWORD");
            if (dbPassword && dbPassword[0] != '\0') {
                if (configJson.isMember("db_clients")
                    && configJson["db_clients"].isArray()
                    && configJson["db_clients"].size() > 0) {
                    configJson["db_clients"][0u]["password"] = dbPassword;
                    LOG_INFO << "[Config] DB password overridden via IDC_DB_PASSWORD env var";
                }
            }
            drogon::app().loadConfigJson(configJson);
        } else {
            LOG_ERROR << "[Config] Failed to parse config.json: " << errs;
            drogon::app().loadConfigFile("config.json");
        }
    }

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

    // ── Register billing cron (daily check for billing day) ───────────────
    idc::BillingCron::init();

    // ── Register dunning cron (daily check for overdue invoices) ──────────
    idc::DunningCron::init();

    // ── Initialize ZJMF connection manager ──────────────────────────────────
    idc::ZJMFConnectionManager::init();

    // ── Register ZJMF sync cron (product/5min, inventory/1min, client/10min) ─
    idc::ZJMFSyncCron::init();

    // ── Register provisioning polling timer (every 30 seconds) ─────────────
    {
        auto loop = drogon::app().getLoop();
        // First run after 30 seconds startup delay
        loop->runAfter(30, []() {
            idc::ZJMFProvisioningService::runPollingCheck();
        });
        loop->runEvery(
            static_cast<double>(idc::ZJMFProvisioningService::kPollIntervalSeconds),
            []() {
                try {
                    idc::ZJMFProvisioningService::runPollingCheck();
                } catch (const std::exception& e) {
                    LOG_ERROR << "[Provisioning] Polling error: " << e.what();
                }
            });
        LOG_INFO << "[Provisioning] Polling timer registered (every "
                 << idc::ZJMFProvisioningService::kPollIntervalSeconds << "s)";
    }

    // ── Start server ─────────────────────────────────────────────────────────
    std::cout << "IDC Platform API Server started on port 8080" << std::endl;

    drogon::app().addListener("0.0.0.0", 8080).run();

    return 0;
}
