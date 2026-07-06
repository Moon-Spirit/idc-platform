#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Payment API controller.
///
/// All endpoints require JWT authentication.
/// Dealers can only access their own payments (RBAC enforced).
///
/// Endpoints:
///   POST  /api/v1/payments/alipay/precreate   — Alipay payment creation
///   POST  /api/v1/payments/wechat/precreate    — WeChat payment creation
///   POST  /api/v1/payments/notify              — Unified payment callback
///   GET   /api/v1/payments/{id}                — Payment status
///   POST  /api/v1/payments/{id}/refund         — Refund (stub, Phase 1)
///
/// Note: The payment notification endpoint POST /api/v1/payments/notify
/// returns plain text "success" or "fail" (as required by Alipay/WeChat
/// gateway protocol), NOT the standard JSON response format.
class PaymentController
    : public drogon::HttpController<PaymentController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(PaymentController::alipayPrecreate,
                      "/api/v1/payments/alipay/precreate", drogon::Post,
                      "JWTFilter", "RBACFilter(payment:read)");
        ADD_METHOD_TO(PaymentController::wechatPrecreate,
                      "/api/v1/payments/wechat/precreate", drogon::Post,
                      "JWTFilter", "RBACFilter(payment:read)");
        // Notification endpoint: NO JWT filter (called by Alipay/WeChat gateway)
        ADD_METHOD_TO(PaymentController::notify,
                      "/api/v1/payments/notify", drogon::Post);
        ADD_METHOD_TO(PaymentController::getPayment,
                      "/api/v1/payments/{id}", drogon::Get,
                      "JWTFilter", "RBACFilter(payment:read)");
        ADD_METHOD_TO(PaymentController::refund,
                      "/api/v1/payments/{id}/refund", drogon::Post,
                      "JWTFilter", "RBACFilter(payment:refund)");
    METHOD_LIST_END

    /// POST /api/v1/payments/alipay/precreate
    /// Body: { "invoice_id": 1 }
    /// Creates an Alipay payment and returns QR code URL.
    static void alipayPrecreate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/payments/wechat/precreate
    /// Body: { "invoice_id": 1 }
    /// Creates a WeChat Pay payment and returns QR code URL.
    static void wechatPrecreate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// POST /api/v1/payments/notify
    /// Unified payment callback endpoint for Alipay and WeChat.
    ///
    /// Receives form-urlencoded data from Alipay or JSON/XML from WeChat.
    /// Returns plain text "success" or "fail" (gateway protocol).
    ///
    /// This endpoint is NOT JWT-protected — it is called by the payment
    /// gateways, not by the frontend.
    static void notify(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/payments/{id}
    /// Get payment status and details.
    static void getPayment(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// POST /api/v1/payments/{id}/refund
    /// Refund a payment (Phase 1 stub — not yet implemented).
    static void refund(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

private:
    /// Helper: extract user_id from JWT attributes.
    static int64_t getUserId(const drogon::HttpRequestPtr& req);

    /// Helper: extract role_id from JWT attributes.
    static int64_t getRoleId(const drogon::HttpRequestPtr& req);

    /// Helper: get distributor_id for the current user.
    static int64_t getUserDistributorId(int64_t userId);

    /// Common handler for precreate (alipay / wechat).
    static void handlePrecreate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& method);

    /// Create a plain text response for the payment gateway callback.
    static drogon::HttpResponsePtr textResponse(const std::string& body,
                                                  drogon::HttpStatusCode status);
};

} // namespace idc
