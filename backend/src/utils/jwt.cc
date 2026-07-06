#include "jwt.h"

#include <drogon/utils/Utilities.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <cstring>
#include <sstream>

namespace idc {

// ============================================================================
// -- Base64url ---------------------------------------------------------------
// ============================================================================

static const char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static const unsigned char kBase64DecodeTable[256] = {
    // 0-255 lookup: 0xFF = invalid, 0-63 = valid base64 char value
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3E, 0xFF, 0x3E, 0xFF, 0x3F,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F,
    0xFF, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,
    0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
    0x31, 0x32, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // rest is 0xFF (invalid)
};

std::string JWTUtil::base64UrlEncode(const std::string& raw) {
    // Standard base64
    std::string b64;
    b64.reserve(((raw.size() + 2) / 3) * 4);

    unsigned char buf[3];
    size_t i = 0;
    while (i < raw.size()) {
        size_t remaining = raw.size() - i;
        size_t len = remaining < 3 ? remaining : 3;
        std::memset(buf, 0, 3);
        std::memcpy(buf, raw.data() + i, len);
        i += len;

        b64.push_back(kBase64UrlAlphabet[buf[0] >> 2]);
        b64.push_back(kBase64UrlAlphabet[((buf[0] & 0x03) << 4) | (buf[1] >> 4)]);
        if (len > 1) {
            b64.push_back(
                kBase64UrlAlphabet[((buf[1] & 0x0F) << 2) | (buf[2] >> 6)]);
        }
        if (len > 2) {
            b64.push_back(kBase64UrlAlphabet[buf[2] & 0x3F]);
        }
    }

    // Already URL-safe alphabet, but strip padding (=) for JWT
    auto pos = b64.find('=');
    if (pos != std::string::npos) {
        b64.resize(pos);
    }
    return b64;
}

std::string JWTUtil::base64UrlDecode(const std::string& encoded) {
    if (encoded.empty()) return {};

    // Restore padding
    std::string input = encoded;
    switch (input.size() % 4) {
        case 2: input += "=="; break;
        case 3: input += "=";  break;
        case 1: return {};     // malformed
    }

    std::string out;
    out.reserve((input.size() / 4) * 3);

    for (size_t i = 0; i < input.size(); i += 4) {
        unsigned char b[4];
        for (size_t j = 0; j < 4; ++j) {
            unsigned char c = static_cast<unsigned char>(input[i + j]);
            if (c == '=') {
                b[j] = 0;
            } else if (c == '-') {
                b[j] = 62;
            } else if (c == '_') {
                b[j] = 63;
            } else {
                auto val = kBase64DecodeTable[c];
                if (val == 0xFF) return {}; // invalid char
                b[j] = val;
            }
        }

        out.push_back(static_cast<char>((b[0] << 2) | (b[1] >> 4)));
        if (input[i + 2] != '=') {
            out.push_back(static_cast<char>(((b[1] & 0x0F) << 4) | (b[2] >> 2)));
        }
        if (input[i + 3] != '=') {
            out.push_back(static_cast<char>(((b[2] & 0x03) << 6) | b[3]));
        }
    }
    return out;
}

// ============================================================================
// -- HMAC-SHA256 -------------------------------------------------------------
// ============================================================================

std::string JWTUtil::hmacSha256(const std::string& data,
                                const std::string& key) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;

    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()),
         data.size(), md, &md_len);

    return std::string(reinterpret_cast<const char*>(md), md_len);
}

// ============================================================================
// -- Payload helpers ---------------------------------------------------------
// ============================================================================

Json::Value JWTUtil::Payload::toJson() const {
    Json::Value jv;
    jv["user_id"]  = static_cast<Json::Int64>(user_id);
    jv["role_id"]  = static_cast<Json::Int64>(role_id);
    jv["username"] = username;
    jv["jti"]      = jti;
    jv["iat"]      = static_cast<Json::Int64>(iat);
    jv["exp"]      = static_cast<Json::Int64>(exp);
    return jv;
}

JWTUtil::Payload JWTUtil::Payload::fromJson(const Json::Value& jv) {
    Payload p;
    p.user_id  = jv.get("user_id",  Json::Int64(0)).asInt64();
    p.role_id  = jv.get("role_id",  Json::Int64(0)).asInt64();
    p.username = jv.get("username", "").asString();
    p.jti      = jv.get("jti",      "").asString();
    p.iat      = jv.get("iat",      Json::Int64(0)).asInt64();
    p.exp      = jv.get("exp",      Json::Int64(0)).asInt64();
    return p;
}

// ============================================================================
// -- Sign / Verify -----------------------------------------------------------
// ============================================================================

std::string JWTUtil::sign(const Payload& payload, const std::string& secret) {
    // Header: {"alg":"HS256","typ":"JWT"}
    Json::Value header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";
    std::string headerB64 = base64UrlEncode(header.toStyledString());
    // Remove whitespace from styled output — use compact format
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string headerCompact = Json::writeString(builder, header);
    headerB64 = base64UrlEncode(headerCompact);

    // Payload
    std::string payloadCompact = Json::writeString(builder, payload.toJson());
    std::string payloadB64    = base64UrlEncode(payloadCompact);

    // Sign
    std::string signingInput = headerB64 + "." + payloadB64;
    std::string sig          = hmacSha256(signingInput, secret);
    std::string sigB64       = base64UrlEncode(sig);

    return signingInput + "." + sigB64;
}

std::optional<JWTUtil::Payload> JWTUtil::verify(const std::string& token,
                                                 const std::string& secret) {
    // Split token: header.payload.signature
    auto firstDot  = token.find('.');
    if (firstDot == std::string::npos) return std::nullopt;
    auto secondDot = token.find('.', firstDot + 1);
    if (secondDot == std::string::npos) return std::nullopt;

    std::string headerB64  = token.substr(0, firstDot);
    std::string payloadB64 = token.substr(firstDot + 1, secondDot - firstDot - 1);
    std::string sigB64     = token.substr(secondDot + 1);

    // Verify signature
    std::string signingInput = headerB64 + "." + payloadB64;
    std::string expectedSig  = hmacSha256(signingInput, secret);
    std::string expectedB64  = base64UrlEncode(expectedSig);

    // Constant-time comparison
    if (sigB64.size() != expectedB64.size()) return std::nullopt;
    bool match = true;
    for (size_t i = 0; i < sigB64.size(); ++i) {
        if (sigB64[i] != expectedB64[i]) match = false;
    }
    if (!match) return std::nullopt;

    // Decode payload
    std::string payloadJson = base64UrlDecode(payloadB64);
    if (payloadJson.empty()) return std::nullopt;

    Json::Value jv;
    Json::CharReaderBuilder readerBuilder;
    std::string errs;
    auto reader = std::unique_ptr<Json::CharReader>(readerBuilder.newCharReader());
    if (!reader->parse(payloadJson.data(),
                        payloadJson.data() + payloadJson.size(),
                        &jv, &errs)) {
        return std::nullopt;
    }

    auto p = Payload::fromJson(jv);

    // Check expiration
    int64_t now = static_cast<int64_t>(::time(nullptr));
    if (p.exp > 0 && now >= p.exp) return std::nullopt;

    // Validate required fields
    if (p.user_id <= 0 || p.jti.empty()) return std::nullopt;

    return p;
}

} // namespace idc
