#include "payment_controller.h"
#include "services/payment_service.h"
#include "utils/response.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════════

int64_t PaymentController::getUserId(const drogon::HttpRequestPtr& req) {
    return req->getAttributes()->get<int64_t>("user_id");
}

int64_t PaymentController::getRoleId(const drogon::HttpRequestPtr& req) {
    return req->getAttributes()->get<int64_t>("role_id");
}

int64_t PaymentController::getUserDistributorId(int64_t userId) {
    auto db = DbClient::getClient();
    auto result = db->execSqlSync(
        "SELECT distributor_id FROM users WHERE id = $1", userId);
    if (result.empty()) return 0;
    auto val = result[0]["distributor_id"];
    return val.isNull() ? 0 : val.as<int64_t>();
}

drogon::HttpResponsePtr PaymentController::textResponse(
    const std::string& body, drogon::HttpStatusCode status) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(status);
    resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    resp->setBody(body);
    return resp;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Common precreate handler
// ═══════════════════════════════════════════════════════════════════════════

void PaymentController::handlePrecreate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& method) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);

        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("invoice_id")) {
            callback(JsonResponse::error(400, "Missing required field: invoice_id"));
            return;
        }

        int64_t invoiceId = static_cast<int64_t>((*body)["invoice_id"].asInt64());

        // Get distributor_id for the current user
        bool isAdmin = (roleId == 1);
        int64_t distributorId = 0;

        if (isAdmin && body->isMember("distributor_id") && !(*body)["distributor_id"].isNull()) {
            // Admin can specify a distributor
            distributorId = static_cast<int64_t>((*body)["distributor_id"].asInt64());
        } else {
            distributorId = getUserDistributorId(userId);
            if (distributorId <= 0) {
                callback(JsonResponse::error(403, "No distributor account associated with this user"));
                return;
            }
        }

        auto payment = PaymentService::createPayment(
            invoiceId, distributorId, method);

        callback(JsonResponse::ok(payment));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Payment] " << method << " precreate error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/payments/alipay/precreate
// ═══════════════════════════════════════════════════════════════════════════

void PaymentController::alipayPrecreate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    handlePrecreate(req, std::move(callback), "alipay");
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/payments/wechat/precreate
// ═══════════════════════════════════════════════════════════════════════════

void PaymentController::wechatPrecreate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    handlePrecreate(req, std::move(callback), "wechat");
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/payments/notify
// ═══════════════════════════════════════════════════════════════════════════

void PaymentController::notify(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        // ── 1. Parse callback data ──────────────────────────────────────
        // Alipay sends application/x-www-form-urlencoded
        // WeChat sends application/xml (or application/json in some SDKs)
        // For Phase 1 we accept both form-urlencoded and JSON.
        Json::Value callbackData;

        std::string contentType = req->getHeader("Content-Type");

        if (contentType.find("application/x-www-form-urlencoded") != std::string::npos ||
            contentType.find("multipart/form-data") != std::string::npos) {
            // Parse form-urlencoded parameters
            auto params = req->getParameters();
            for (const auto& [key, val] : params) {
                callbackData[key] = val;
            }
        } else {
            // Try JSON
            auto jsonPtr = req->getJsonObject();
            if (jsonPtr) {
                callbackData = *jsonPtr;
            } else {
                LOG_IDC_WARN(req, "[Payment] Unrecognized callback format: " << contentType);
                callback(textResponse("fail", drogon::k400BadRequest));
                return;
            }
        }

        if (callbackData.empty()) {
            LOG_IDC_WARN(req, "[Payment] Empty callback data");
            callback(textResponse("fail", drogon::k400BadRequest));
            return;
        }

        // ── 2. Detect gateway from callback fields ──────────────────────
        // Alipay has "sign_type", "trade_status", etc.
        // WeChat has "result_code", "return_code", etc.
        std::string gateway;
        if (callbackData.isMember("sign_type") ||
            callbackData.isMember("trade_status")) {
            gateway = "alipay";
        } else if (callbackData.isMember("result_code") ||
                   callbackData.isMember("return_code")) {
            gateway = "wechat";
        } else if (callbackData.isMember("method")) {
            // Fallback: check explicit method field
            gateway = callbackData["method"].asString();
        } else {
            // Default to alipay for Phase 1
            gateway = "alipay";
            LOG_IDC_INFO(req, "[Payment] Could not detect gateway, defaulting to alipay");
        }

        // ── 3. Process the callback ─────────────────────────────────────
        auto result = PaymentService::processPaymentCallback(gateway, callbackData);

        LOG_IDC_INFO(req, "[Payment] Callback processed: gateway=" << gateway
                     << " payment_no=" << result.get("payment_no", "").asString()
                     << " duplicate=" << result.get("duplicate", false).asBool());

        // ── 4. Return success to gateway ────────────────────────────────
        // Alipay/WeChat expect plain text "success" or "fail"
        callback(textResponse("success", drogon::k200OK));

    } catch (const std::invalid_argument& e) {
        LOG_IDC_ERROR(req, "[Payment] Callback validation error: " << e.what());
        callback(textResponse("fail", drogon::k200OK)); // Still 200 OK — gateway expects HTTP 200
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Payment] Callback error: " << e.what());
        callback(textResponse("fail", drogon::k200OK));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/payments/{id}
// ═══════════════════════════════════════════════════════════════════════════

void PaymentController::getPayment(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto payment = PaymentService::getPaymentById(id);
        if (payment.isNull()) {
            callback(JsonResponse::notFound("Payment not found"));
            return;
        }

        // Permission check: dealers can only see their own payments
        int64_t roleId = getRoleId(req);
        if (roleId != 1) {
            int64_t userId = getUserId(req);
            int64_t userDistId = getUserDistributorId(userId);
            int64_t payDistId = payment["distributor_id"].asInt64();
            if (userDistId != payDistId) {
                callback(JsonResponse::notFound("Payment not found"));
                return;
            }
        }

        callback(JsonResponse::ok(payment));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Payment] getPayment error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/payments/{id}/refund
// ═══════════════════════════════════════════════════════════════════════════

void PaymentController::refund(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto result = PaymentService::processRefund(id);
        callback(JsonResponse::ok(result));
    } catch (const std::runtime_error& e) {
        // Refund not implemented in Phase 1
        callback(JsonResponse::error(501, e.what()));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Payment] refund error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
