#include "wechat_pay.h"
#include "utils/logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helpers
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

std::string WechatPayUtil::buildSignString(const Json::Value& params) {
    // Collect keys, excluding "sign"
    std::vector<std::string> keys;
    for (const auto& key : params.getMemberNames()) {
        if (key == "sign") continue;
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

std::string WechatPayUtil::sign(const Json::Value& params,
                                 const std::string& /*apiKey*/) {
    // Phase 1 stub: return a mock HMAC-SHA256 signature
    // Real implementation would use OpenSSL HMAC(EVP_sha256(), ...)
    std::string signStr = buildSignString(params);
    LOG_DEBUG << "[WechatPay] Stub sign: " << signStr.substr(0, 64) << "...";

    // Mock signature: SHA256 hex of signing string (uppercase)
    // In production this would be HMAC-SHA256(signStr + "&key=" + apiKey)
    std::string mockSig = "MOCK_WECHAT_HMACSHA256_SIGNATURE_"
                          + signStr.substr(0, 16);
    return mockSig;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Verify (Phase 1 stub)
// ═══════════════════════════════════════════════════════════════════════════

bool WechatPayUtil::verify(const Json::Value& /*params*/,
                            const std::string& /*signature*/,
                            const std::string& /*apiKey*/) {
    // Phase 1 stub: always return true
    // Real implementation would compute HMAC-SHA256(params + "&key=" + apiKey)
    // and compare (case-insensitive) with the provided signature.
    LOG_DEBUG << "[WechatPay] Stub verify: always true (Phase 1)";
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Precreate (Phase 1 stub)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value WechatPayUtil::precreate(const std::string& outTradeNo,
                                      const std::string& totalFee,
                                      const std::string& body) {
    LOG_INFO << "[WechatPay] Precreate order: outTradeNo=" << outTradeNo
             << " totalFee=" << totalFee << " body=" << body;

    // Phase 1 stub: return a simulated QR code URL
    // Real implementation would POST XML to WeChat Pay API.
    Json::Value result;
    result["qr_code_url"] = "weixin://wxpay/bizpayurl?mock_pr=" + outTradeNo;
    result["prepay_id"] = "wx" + nowCompact() + randomDigits(10);
    result["trade_type"] = "NATIVE";
    result["out_trade_no"] = outTradeNo;
    result["total_fee"] = totalFee;

    LOG_DEBUG << "[WechatPay] Mock precreate response: qr_code_url="
              << result["qr_code_url"].asString();

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Yuan → cents conversion
// ═══════════════════════════════════════════════════════════════════════════

std::string WechatPayUtil::yuanToCents(const std::string& yuan) {
    double amount = std::stod(yuan);
    int64_t cents = static_cast<int64_t>(std::round(amount * 100.0));
    return std::to_string(cents);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Mock transaction ID
// ═══════════════════════════════════════════════════════════════════════════

std::string WechatPayUtil::mockTransactionId() {
    // WeChat transaction_id: 420000 + date + 10 digits
    return "420000" + nowCompact() + randomDigits(10);
}

} // namespace idc
