#pragma once

#include <drogon/drogon.h>
#include <cstdlib>
#include <string>

namespace idc {

/// Application configuration loaded from config.json.
///
/// Environment variable overrides (checked at startup):
///   IDC_DB_PASSWORD  — replaces db_clients[0].password
///   IDC_JWT_SECRET   — replaces jwt.secret
///
/// Call `Config::init()` from main.cc after loading the config file.
class Config {
public:
    static void init() {
        const char* envJwtSecret = std::getenv("IDC_JWT_SECRET");
        std::string effectiveJwtSecret;
        if (envJwtSecret && envJwtSecret[0] != '\0') {
            effectiveJwtSecret = envJwtSecret;
            LOG_INFO << "[Config] JWT secret overridden via IDC_JWT_SECRET env var";
        }

        auto& json = drogon::app().getCustomConfig();
        auto& jwt  = json["jwt"];

        // -- JWT ----------------------------------------------------------
        if (!effectiveJwtSecret.empty()) {
            jwtSecret = effectiveJwtSecret;
        } else {
            jwtSecret = jwt.get("secret", "").asString();
        }
        jwtExpiresIn = jwt.get("expires_in", 86400).asInt();
        if (jwtSecret.empty()) {
            LOG_WARN << "[Config] jwt.secret is empty — using default (INSECURE)";
            jwtSecret = "idc-platform-default-secret-do-not-use-in-production";
        }
        if (jwtExpiresIn <= 0) {
            jwtExpiresIn = 86400; // 24 hours
        }

        LOG_INFO << "[Config] JWT expires_in = " << jwtExpiresIn << "s";
    }

    static std::string jwtSecret;
    static int         jwtExpiresIn;
};

// Static definitions — one copy per translation unit via inline
inline std::string Config::jwtSecret;
inline int         Config::jwtExpiresIn = 86400;

} // namespace idc
