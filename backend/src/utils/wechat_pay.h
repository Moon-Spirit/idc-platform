#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace idc {

/// WeChat Pay payment gateway utility (Phase 1 stub).
///
/// In Phase 1, signature verification is mocked — no real API key is
/// required. The precreate method returns a simulated qr_code_url for the
/// frontend to display.
///
/// Real integration in a future phase will:
///   - Load WeChat app_id, mch_id, api_key from system_config
///   - Use HMAC-SHA256 for native signature verification
///   - Send XML requests to the WeChat Pay API
///
/// Reference: https://pay.weixin.qq.com/wiki/doc/api/native.php
class WechatPayUtil {
public:
    /// WeChat result code indicating successful payment.
    static constexpr const char* kResultSuccess = "SUCCESS";

    // ── Sign / Verify (HMAC-SHA256) ─────────────────────────────────────────

    /// Sign parameters with HMAC-SHA256 per WeChat Pay spec.
    ///
    /// Builds the signing string by sorting params by key, concatenating as
    /// key=value&key=value, appending "&key={apiKey}", then HMAC-SHA256
    /// hashing and converting to uppercase hex.
    ///
    /// @param params  Flat key-value map of WeChat Pay parameters.
    /// @param apiKey  WeChat Pay API key (商户平台API密钥). Ignored in Phase 1.
    /// @return Uppercase hex HMAC-SHA256 signature.
    static std::string sign(const Json::Value& params,
                            const std::string& apiKey = "");

    /// Verify a WeChat Pay callback signature.
    ///
    /// @param params    Callback parameters (including `sign`).
    /// @param signature The signature to verify (uppercase hex).
    /// @param apiKey    WeChat Pay API key.
    /// @return true if signature is valid.
    static bool verify(const Json::Value& params,
                       const std::string& signature,
                       const std::string& apiKey = "");

    // ── Precreate Order ─────────────────────────────────────────────────────

    /// Simulate a WeChat Pay native (QR code) precreate request.
    ///
    /// In Phase 1 this returns a mock qr_code_url without calling the WeChat
    /// API. The real implementation would POST XML to:
    ///   https://api.mch.weixin.qq.com/pay/unifiedorder
    ///
    /// @param outTradeNo  Merchant's unique order number (payment_no).
    /// @param totalFee    Transaction amount in cents (分). E.g. "100" = 1 yuan.
    /// @param body        Order description.
    /// @return JSON with { qr_code_url, prepay_id, trade_type }.
    static Json::Value precreate(const std::string& outTradeNo,
                                  const std::string& totalFee,
                                  const std::string& body);

    // ── Parameter helpers ──────────────────────────────────────────────────

    /// Build the signing string: sort by key, exclude sign, concatenate as
    /// key=value&key=value (without trailing &key=).
    static std::string buildSignString(const Json::Value& params);

    /// Convert yuan to cents (分): "12.34" → "1234".
    static std::string yuanToCents(const std::string& yuan);

    /// Generate a mock WeChat Pay transaction_id.
    static std::string mockTransactionId();
};

} // namespace idc
