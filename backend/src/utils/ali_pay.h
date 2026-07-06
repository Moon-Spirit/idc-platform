#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace idc {

/// Alipay payment gateway utility (Phase 1 stub).
///
/// In Phase 1, signature verification is mocked — no real RSA2 keys are
/// required. The precreate method returns a simulated qr_code_url for the
/// frontend to display.
///
/// Real integration in a future phase will:
///   - Load Alipay app_id, private_key, alipay_public_key from system_config
///   - Use OpenSSL RSA to sign/verify with SHA256 (RSA2)
///   - Make HTTP POST to Alipay's openapi endpoint
///
/// Reference: https://opendocs.alipay.com/open/200/105310
class AliPayUtil {
public:
    /// Alipay trade status indicating successful payment.
    static constexpr const char* kTradeSuccess = "TRADE_SUCCESS";

    // ── Sign / Verify (RSA2) ────────────────────────────────────────────────

    /// Sign a parameter map with RSA2 (SHA256withRSA).
    ///
    /// Builds the signing string by sorting params by key, concatenating as
    /// key=value&key=value, then signing with the merchant's RSA private key.
    ///
    /// @param params    Flat key-value map of Alipay parameters.
    /// @param privateKey  RSA private key (PKCS8 PEM). Ignored in Phase 1 (stub).
    /// @return Base64-encoded signature string.
    static std::string sign(const Json::Value& params,
                            const std::string& privateKey = "");

    /// Verify an Alipay callback signature using RSA2.
    ///
    /// @param params      Callback parameters (including `sign` and `sign_type`).
    /// @param signature   The signature to verify.
    /// @param alipayPublicKey  Alipay's RSA public key. Ignored in Phase 1 (stub).
    /// @return true if signature is valid.
    static bool verify(const Json::Value& params,
                       const std::string& signature,
                       const std::string& alipayPublicKey = "");

    // ── Precreate Order ─────────────────────────────────────────────────────

    /// Simulate an Alipay precreate (QR code) request.
    ///
    /// In Phase 1 this returns a mock qr_code_url without calling the Alipay
    /// API. The real implementation would POST to:
    ///   https://openapi.alipay.com/gateway.do
    ///
    /// @param outTradeNo  Merchant's unique order number (payment_no).
    /// @param totalAmount Transaction amount (yuan, two decimal places).
    /// @param subject     Order description / title.
    /// @return JSON with { qr_code_url, trade_no }.
    static Json::Value precreate(const std::string& outTradeNo,
                                  const std::string& totalAmount,
                                  const std::string& subject);

    // ── Parameter helpers ──────────────────────────────────────────────────

    /// Build the signing string from parameters: sort by key, concatenate as
    /// key=value&key=value (excluding sign and sign_type).
    static std::string buildSignString(const Json::Value& params);

    /// Generate a mock Alipay trade_no (28-character string).
    static std::string mockTradeNo();
};

} // namespace idc
