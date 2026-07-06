#include "ali_pay.h"
#include "utils/logger.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: current timestamp for mock trade_no
// ═══════════════════════════════════════════════════════════════════════════

static std::string nowCompact() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y%m%d%H%M%S");
    return oss.str();
}

static std::string randomDigits(size_t len) {
    static const char digits[] = "0123456789";
    static std::mt19937 gen(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<> dist(0, sizeof(digits) - 2);
    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += digits[dist(gen)];
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Signing string builder
// ═══════════════════════════════════════════════════════════════════════════

std::string AliPayUtil::buildSignString(const Json::Value& params) {
    // Collect keys, excluding sign and sign_type
    std::vector<std::string> keys;
    for (const auto& key : params.getMemberNames()) {
        if (key == "sign" || key == "sign_type") continue;
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    std::ostringstream oss;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) oss << "&";
        oss << keys[i] << "=" << params[keys[i]].asString();
    }
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Sign (Phase 1 stub)
// ═══════════════════════════════════════════════════════════════════════════

std::string AliPayUtil::sign(const Json::Value& params,
                              const std::string& /*privateKey*/) {
    // Phase 1 stub: return a mock base64-encoded signature
    // Real implementation would use OpenSSL RSA_sign(NID_sha256, ...)
    std::string signStr = buildSignString(params);
    LOG_DEBUG << "[AliPay] Stub sign: " << signStr.substr(0, 64) << "...";

    // Return a deterministic mock signature (base64-encoded SHA256 of signing string)
    // In production this would be RSA_sign with the merchant's private key.
    std::string mockSig = "MOCK_ALIPAY_RSA2_SIGNATURE_" + signStr.substr(0, 16);
    return mockSig;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Verify (Phase 1 stub)
// ═══════════════════════════════════════════════════════════════════════════

bool AliPayUtil::verify(const Json::Value& params,
                         const std::string& /*signature*/,
                         const std::string& /*alipayPublicKey*/) {
    // Phase 1 stub: always return true
    // Real implementation would use OpenSSL RSA_verify(NID_sha256, ...)
    // with Alipay's public key to verify the signature.
    LOG_DEBUG << "[AliPay] Stub verify: always true (Phase 1)";
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Precreate (Phase 1 stub)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value AliPayUtil::precreate(const std::string& outTradeNo,
                                   const std::string& totalAmount,
                                   const std::string& subject) {
    LOG_INFO << "[AliPay] Precreate order: outTradeNo=" << outTradeNo
             << " amount=" << totalAmount << " subject=" << subject;

    // Phase 1 stub: return a simulated QR code URL
    // Real implementation would POST to Alipay and parse the response.
    Json::Value result;
    result["qr_code_url"] = "https://qr.alipay.com/mock_" + outTradeNo;
    result["trade_no"] = mockTradeNo();
    result["out_trade_no"] = outTradeNo;
    result["total_amount"] = totalAmount;
    result["subject"] = subject;

    LOG_DEBUG << "[AliPay] Mock precreate response: qr_code_url="
              << result["qr_code_url"].asString();

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Mock trade number
// ═══════════════════════════════════════════════════════════════════════════

std::string AliPayUtil::mockTradeNo() {
    // Alipay trade_no is typically 28-32 digits
    return nowCompact() + randomDigits(16);
}

} // namespace idc
