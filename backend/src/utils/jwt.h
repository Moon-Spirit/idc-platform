#pragma once

#include <json/json.h>
#include <optional>
#include <string>

namespace idc {

/// HS256 JWT sign / verify utility.
///
/// Pure OpenSSL HMAC-SHA256 + custom base64url encoding.
/// No external JWT library required.
class JWTUtil {
public:
    /// Decoded JWT payload.
    struct Payload {
        int64_t  user_id    = 0;
        int64_t  role_id    = 0;
        std::string username;
        std::string jti;         // unique token ID (UUID)
        int64_t  iat        = 0; // issued-at (unix epoch)
        int64_t  exp        = 0; // expires-at (unix epoch)

        Json::Value toJson() const;
        static Payload fromJson(const Json::Value& jv);
    };

    /// Sign a payload and return the compact JWT string.
    static std::string sign(const Payload& payload, const std::string& secret);

    /// Verify a JWT token and decode its payload.
    /// Returns nullopt on any failure (bad signature, expired, malformed).
    static std::optional<Payload> verify(const std::string& token,
                                         const std::string& secret);

private:
    // -- base64url helpers ------------------------------------------------
    static std::string base64UrlEncode(const std::string& raw);
    static std::string base64UrlDecode(const std::string& encoded);

    // -- HMAC-SHA256 ------------------------------------------------------
    static std::string hmacSha256(const std::string& data,
                                  const std::string& key);
};

} // namespace idc
