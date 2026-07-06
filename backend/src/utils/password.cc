#include "password.h"

#include <crypt.h>
#include <openssl/rand.h>
#include <cstring>
#include <stdexcept>

namespace idc {

static_assert(sizeof(unsigned long long) >= 8, "64-bit type required");

const char PasswordUtil::kBCryptAlphabet[65] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

// -----------------------------------------------------------------------
//  bcrypt salt generation
//  Format: $2b$<cost, 2 digits>$<22-char-base64-salt>
// -----------------------------------------------------------------------
std::string PasswordUtil::generateSalt(int cost) {
    if (cost < 4 || cost > 31) {
        throw std::invalid_argument("bcrypt cost must be 4–31");
    }

    // Generate 16 random bytes (128 bits) → 22 base64 chars
    unsigned char raw[16];
    if (RAND_bytes(raw, sizeof(raw)) != 1) {
        throw std::runtime_error("RAND_bytes failed for bcrypt salt");
    }

    // Encode to bcrypt base64 (24 bits → 4 chars, repeated)
    char salt[30];
    int len = snprintf(salt, sizeof(salt), "$2b$%02d$", cost);
    if (len < 0) throw std::runtime_error("snprintf failed");

    // Encode 16 bytes → 22 chars
    // bcrypt base64: 6 bits per char, 24 bits per 4-char group
    unsigned int i = 0;
    auto appendB64 = [&](unsigned int val, int chars) {
        for (int c = chars - 1; c >= 0; --c) {
            salt[len++] = kBCryptAlphabet[(val >> (c * 6)) & 0x3F];
        }
    };

    while (i < 16) {
        unsigned int val = (static_cast<unsigned int>(raw[i]) << 16)
                         | (static_cast<unsigned int>(raw[i + 1]) << 8)
                         | static_cast<unsigned int>(raw[i + 2]);
        appendB64(val, 4);
        i += 3;
    }

    salt[len] = '\0';
    return std::string(salt);
}

// -----------------------------------------------------------------------
//  hash
// -----------------------------------------------------------------------
std::string PasswordUtil::hash(const std::string& password, int cost) {
    std::string salt = generateSalt(cost);

    struct crypt_data data;
    std::memset(&data, 0, sizeof(data));
    data.initialized = 0;

    char* result = crypt_r(password.c_str(), salt.c_str(), &data);
    if (result == nullptr) {
        throw std::runtime_error("crypt_r failed for bcrypt hash");
    }

    return std::string(result);
}

// -----------------------------------------------------------------------
//  verify
// -----------------------------------------------------------------------
bool PasswordUtil::verify(const std::string& password, const std::string& hash) {
    struct crypt_data data;
    std::memset(&data, 0, sizeof(data));
    data.initialized = 0;

    char* result = crypt_r(password.c_str(), hash.c_str(), &data);
    if (result == nullptr) {
        return false;
    }
    return std::string(result) == hash;
}

} // namespace idc
