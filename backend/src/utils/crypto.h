#pragma once

#include <json/json.h>
#include <string>
#include <optional>

namespace idc {

/// Cryptographic utilities for ZJMF integration.
///
/// Provides:
///   - AES-256-CBC encrypt/decrypt (for API key storage)
///   - HMAC-SHA256 sign/verify (for webhook payload validation)
///   - RSA-SHA256 verify (for signed webhook callbacks)
///
/// AES encryption key is loaded from the ZJMF_AES_KEY environment variable.
/// If the variable is not set, a compile-time default is used (INSECURE —
/// for dev only; production MUST configure the env var).
class CryptoUtil {
public:
    /// ── AES-256-CBC ──────────────────────────────────────────────────────────

    /// Encrypt plaintext using AES-256-CBC with the configured key.
    /// Returns base64-encoded ciphertext (IV prepended).
    /// On failure returns std::nullopt (logs internally).
    static std::optional<std::string> aesEncrypt(const std::string& plaintext);

    /// Decrypt a base64-encoded ciphertext (IV prepended) using AES-256-CBC.
    /// On failure returns std::nullopt (logs internally).
    static std::optional<std::string> aesDecrypt(const std::string& ciphertextB64);

    /// ── HMAC-SHA256 ──────────────────────────────────────────────────────────

    /// Compute HMAC-SHA256 of the data with the given key.
    /// Returns hex-encoded digest string.
    static std::string hmacSha256Hex(const std::string& data,
                                     const std::string& key);

    /// Verify HMAC-SHA256 signature (constant-time comparison).
    static bool hmacSha256Verify(const std::string& data,
                                 const std::string& key,
                                 const std::string& expectedHexSig);

    /// ── RSA-SHA256 ───────────────────────────────────────────────────────────

    /// Verify an RSA-SHA256 signature.
    /// @param data          The original signed data
    /// @param signatureB64  Base64-encoded signature
    /// @param publicKeyPEM  RSA public key in PEM format
    /// @return true if signature is valid
    static bool rsaSha256Verify(const std::string& data,
                                const std::string& signatureB64,
                                const std::string& publicKeyPEM);

    /// ── Configuration ────────────────────────────────────────────────────────

    /// Get the AES encryption key (from env ZJMF_AES_KEY or default).
    static std::string getAesKey();

    /// Get the webhook signing secret (from env ZJMF_WEBHOOK_SECRET or default).
    static std::string getWebhookSecret();

private:
    static constexpr const char* kDefaultAesKey =
        "idc-zjmf-default-aes-key-32bytes!!";  // 32 bytes for AES-256
    static constexpr const char* kEnvAesKey = "ZJMF_AES_KEY";
    static constexpr const char* kEnvWebhookSecret = "ZJMF_WEBHOOK_SECRET";
    static constexpr const char* kDefaultWebhookSecret =
        "idc-zjmf-webhook-secret-default";
};

} // namespace idc
