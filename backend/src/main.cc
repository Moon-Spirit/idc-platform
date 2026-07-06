#include <drogon/drogon.h>
#include "utils/db_client.h"
#include "utils/error_handler.h"
#include "utils/logger.h"
#include "utils/config.h"

#include "controllers/auth_controller.h"
#include "controllers/setup_controller.h"
#include "controllers/bandwidth_controller.h"
#include "controllers/distributor_controller.h"
#include "controllers/user_controller.h"
#include "controllers/billing_controller.h"
#include "controllers/invoice_controller.h"
#include "controllers/payment_controller.h"
#include "controllers/balance_controller.h"
#include "controllers/dashboard_controller.h"
#include "controllers/zjmf_controller.h"
#include "controllers/provisioning_controller.h"
#include "cron/billing_cron.h"
#include "cron/dunning_cron.h"
#include "cron/zjmf_sync_cron.h"
#include "services/zjmf_adapter.h"
#include "services/zjmf_provisioning_service.h"
#include "filters/jwt_filter.h"
#include "filters/rbac_filter.h"

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

    // ── Helper: register a param-name → factory in DrClassMap ──────────────
    static auto registerParamFilter =
        [](const std::string& name, const std::string& param) {
            drogon::DrClassMap::registerClass(
                name,
                [param]() -> drogon::DrObjectBase* {
                    return new idc::RBACFilter(param);
                },
                [param]() -> std::shared_ptr<drogon::DrObjectBase> {
                    return std::make_shared<idc::RBACFilter>(param);
                });
        };

    // ── Register filters explicitly (CRTP auto-registration unreliable) ─────
    drogon::app().registerFilter(std::make_shared<idc::JWTFilter>());
    drogon::app().registerFilter(std::make_shared<idc::RBACFilter>(""));
    // Short names for controller macros ("JWTFilter")
    drogon::DrClassMap::registerClass(
        "JWTFilter",
        []() -> drogon::DrObjectBase* { return new idc::JWTFilter(); },
        []() -> std::shared_ptr<drogon::DrObjectBase> {
            return std::make_shared<idc::JWTFilter>();
        });

    // Base RBACFilter name (no-arg)
    registerParamFilter("RBACFilter", "");

    // RBACFilter(permission:code) — one registration per permission code used
    // by controllers. Drogon parses "RBACFilter(user:list)" into filter name
    // "RBACFilter" with parameter "user:list".
    const std::vector<std::string> rbacPermissions = {
        // distributor
        "distributor:create",  "distributor:delete",  "distributor:list",
        "distributor:read",    "distributor:settlement", "distributor:update",
        // invoice
        "invoice:create",  "invoice:delete",  "invoice:export",
        "invoice:generate", "invoice:list",   "invoice:pay",
        "invoice:read",     "invoice:update",  "invoice:void",
        // metered
        "metered:read", "metered:write",
        // addon
        "addon:read", "addon:write",
        // order
        "order:approve", "order:create", "order:delete",
        "order:list",    "order:read",   "order:reject",
        "order:update",
        // payment
        "payment:list", "payment:read", "payment:refund",
        // price
        "price:create", "price:delete", "price:list",
        "price:read",   "price:update",
        // product
        "product:create", "product:delete", "product:list",
        "product:read",   "product:update",
        // provisioning
        "provisioning:read", "provisioning:retry",
        // subscription
        "subscription:create", "subscription:delete", "subscription:list",
        "subscription:provision", "subscription:read", "subscription:suspend",
        "subscription:terminate", "subscription:update",
        // sync
        "sync:config", "sync:read", "sync:trigger",
        // user
        "user:create", "user:delete", "user:list",
        "user:read",   "user:update",
        // billing
        "billing:read", "billing:run",
        // dunning
        "dunning:run",
        // permission
        "permission:list", "permission:read",
        // role
        "role:create", "role:delete", "role:list",
        "role:read",   "role:update",
        // operation
        "operation:list", "operation:read",
        // report
        "report:export", "report:read",
        // system
        "system:read", "system:update",
    };
    for (const auto& p : rbacPermissions) {
        registerParamFilter("RBACFilter(" + p + ")", p);
    }

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

    // ── Install-secret path-prefix guard ─────────────────────────────────────
    // After setup, all /api/v1/* paths (except /api/v1/setup/*) must include
    // the install_secret as the first path segment, e.g.
    //   /api/v1/a1b2c3d4e5f67890/auth/login  →  /api/v1/auth/login
    drogon::app().registerPreRoutingAdvice(
        [](const drogon::HttpRequestPtr& req,
           drogon::AdviceCallback&&      callback,
           drogon::AdviceChainCallback&&  chained) {
            const std::string& path = req->path();

            // Always allow /api/v1/setup/* (setup wizard is public)
            if (path.find("/api/v1/setup/") == 0) {
                chained();
                return;
            }

            // Only intercept /api/v1/* paths
            if (path.find("/api/v1/") != 0) {
                chained();
                return;
            }

            try {
                auto db = idc::DbClient::getClient();
                auto rows = db->execSqlSync(
                    "SELECT value FROM system_config WHERE key = 'install_secret'");

                if (!rows.empty()) {
                    // ── System is installed ─────────────────────────────────
                    std::string secret = rows[0]["value"].as<std::string>();
                    // Everything after "/api/v1/"
                    std::string rest = path.substr(8);
                    auto slashPos = rest.find('/');
                    std::string firstSeg = (slashPos != std::string::npos)
                                               ? rest.substr(0, slashPos)
                                               : rest;

                    if (firstSeg == secret) {
                        // Strip the secret segment and continue
                        std::string newPath = (slashPos != std::string::npos)
                                                  ? "/api/v1" + rest.substr(slashPos)
                                                  : "/api/v1";
                        req->setPath(newPath);
                        chained();
                    } else {
                        // Wrong / missing secret → 404
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::k404NotFound);
                        resp->setBody("Not Found");
                        callback(resp);
                    }
                } else {
                    // ── Not installed yet ───────────────────────────────────
                    // Block all /api/v1/* (setup/* already allowed above)
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k404NotFound);
                    resp->setBody("Not Found");
                    callback(resp);
                }
            } catch (...) {
                // DB not available → treat as not-installed
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k404NotFound);
                resp->setBody("Not Found");
                callback(resp);
            }
        });

    // ── Attach CORS headers to every response ────────────────────────────────
    drogon::app().registerPostHandlingAdvice(
        [](const drogon::HttpRequestPtr& req,
           const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("X-Request-ID",
                            idc::Logger::getRequestId(req));
        });

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
