#pragma once

#include <string>

namespace idc {

/// bcrypt password hashing utility.
///
/// Uses the system `crypt_r()` function (glibc / libxcrypt).
/// Thread-safe via reentrant crypt API.
///
/// Cost factor:
///   - Default: 12 (matching the project security standard)
///   - Range:   4–31
class PasswordUtil {
public:
    /// Hash a plaintext password with bcrypt.
    /// @param password  Plaintext password.
    /// @param cost      bcrypt cost factor (default: 12).
    /// @return Bcrypt hash string ($2b$...).
    static std::string hash(const std::string& password, int cost = 12);

    /// Verify a plaintext password against a bcrypt hash.
    /// @param password  Plaintext password.
    /// @param hash      Bcrypt hash from a previous hash() call.
    /// @return true if the password matches.
    static bool verify(const std::string& password, const std::string& hash);

private:
    /// Generate a bcrypt-compatible salt.
    /// Format: $2b$<cost>$<22-char-base64-salt>
    static std::string generateSalt(int cost);

    /// Custom base64 alphabet for bcrypt salts.
    static const char kBCryptAlphabet[65];
};

} // namespace idc
