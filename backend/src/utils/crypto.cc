#include "crypto.h"
#include "logger.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

namespace idc {

// ============================================================================
// ── Internal helpers ──────────────────────────────────────────────────────────
// ============================================================================

namespace {

/// Base64 encode (standard alphabet, with padding).
std::string base64Encode(const unsigned char* data, size_t len) {
    BIO* bio  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    BIO_push(bio, bmem);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, data, static_cast<int>(len));
    (void)BIO_flush(bio);

    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(bio, &bptr);

    std::string result(bptr->data, bptr->length);
    BIO_free_all(bio);
    return result;
}

/// Base64 decode (standard alphabet).
std::vector<unsigned char> base64Decode(const std::string& input) {
    size_t len = input.size();
    std::vector<unsigned char> out(len);

    BIO* bio  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(input.data(), static_cast<int>(len));
    BIO_push(bio, bmem);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    int decoded = BIO_read(bio, out.data(), static_cast<int>(len));
    BIO_free_all(bio);

    if (decoded <= 0) return {};
    out.resize(static_cast<size_t>(decoded));
    return out;
}

/// Hex-encode a byte string.
std::string hexEncode(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

} // anonymous namespace

// ============================================================================
// ── AES-256-CBC ──────────────────────────────────────────────────────────────
// ============================================================================

std::optional<std::string> CryptoUtil::aesEncrypt(const std::string& plaintext) {
    std::string key = getAesKey();
    if (key.size() != 32) {
        LOG_ERROR << "[Crypto] AES key length is " << key.size()
                  << " (expected 32 bytes)";
        return std::nullopt;
    }

    // Generate random 16-byte IV
    std::vector<unsigned char> iv(16);
    RAND_bytes(iv.data(), 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        LOG_ERROR << "[Crypto] Failed to create EVP_CIPHER_CTX";
        return std::nullopt;
    }

    int len = 0;
    int ciphertextLen = 0;
    std::vector<unsigned char> ciphertext(
        plaintext.size() + EVP_MAX_BLOCK_LENGTH);

    // Init encrypt
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.data()),
                           iv.data()) != 1) {
        LOG_ERROR << "[Crypto] EVP_EncryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    // Encrypt
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                          reinterpret_cast<const unsigned char*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
        LOG_ERROR << "[Crypto] EVP_EncryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    ciphertextLen = len;

    // Finalize
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        LOG_ERROR << "[Crypto] EVP_EncryptFinal_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    ciphertextLen += len;
    ciphertext.resize(static_cast<size_t>(ciphertextLen));

    EVP_CIPHER_CTX_free(ctx);

    // Prepend IV to ciphertext, then base64-encode
    std::vector<unsigned char> combined;
    combined.reserve(iv.size() + ciphertext.size());
    combined.insert(combined.end(), iv.begin(), iv.end());
    combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());

    return base64Encode(combined.data(), combined.size());
}

std::optional<std::string> CryptoUtil::aesDecrypt(const std::string& ciphertextB64) {
    std::string key = getAesKey();
    if (key.size() != 32) {
        LOG_ERROR << "[Crypto] AES key length is " << key.size()
                  << " (expected 32 bytes)";
        return std::nullopt;
    }

    auto combined = base64Decode(ciphertextB64);
    if (combined.size() < 16) {
        LOG_ERROR << "[Crypto] Ciphertext too short (missing IV)";
        return std::nullopt;
    }

    // Extract IV (first 16 bytes)
    std::vector<unsigned char> iv(combined.begin(), combined.begin() + 16);
    std::vector<unsigned char> ciphertext(combined.begin() + 16, combined.end());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        LOG_ERROR << "[Crypto] Failed to create EVP_CIPHER_CTX";
        return std::nullopt;
    }

    int len = 0;
    int plaintextLen = 0;
    std::vector<unsigned char> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);

    // Init decrypt
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.data()),
                           iv.data()) != 1) {
        LOG_ERROR << "[Crypto] EVP_DecryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    // Decrypt
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                          ciphertext.data(),
                          static_cast<int>(ciphertext.size())) != 1) {
        LOG_ERROR << "[Crypto] EVP_DecryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    plaintextLen = len;

    // Finalize
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        LOG_ERROR << "[Crypto] EVP_DecryptFinal_ex failed (bad key/ciphertext?)";
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    plaintextLen += len;
    plaintext.resize(static_cast<size_t>(plaintextLen));

    EVP_CIPHER_CTX_free(ctx);

    return std::string(reinterpret_cast<const char*>(plaintext.data()),
                       plaintext.size());
}

// ============================================================================
// ── HMAC-SHA256 ──────────────────────────────────────────────────────────────
// ============================================================================

std::string CryptoUtil::hmacSha256Hex(const std::string& data,
                                       const std::string& key) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;

    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()),
         data.size(), md, &mdLen);

    return hexEncode(md, mdLen);
}

bool CryptoUtil::hmacSha256Verify(const std::string& data,
                                   const std::string& key,
                                   const std::string& expectedHexSig) {
    auto computed = hmacSha256Hex(data, key);
    // Constant-time comparison
    if (computed.size() != expectedHexSig.size()) return false;
    bool match = true;
    for (size_t i = 0; i < computed.size(); ++i) {
        if (computed[i] != expectedHexSig[i]) match = false;
    }
    return match;
}

// ============================================================================
// ── RSA-SHA256 Verify ─────────────────────────────────────────────────────────
// ============================================================================

bool CryptoUtil::rsaSha256Verify(const std::string& data,
                                  const std::string& signatureB64,
                                  const std::string& publicKeyPEM) {
    // Read public key
    BIO* bio = BIO_new_mem_buf(publicKeyPEM.data(),
                               static_cast<int>(publicKeyPEM.size()));
    if (!bio) return false;

    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return false;

    // Decode signature
    auto sigBytes = base64Decode(signatureB64);
    if (sigBytes.empty()) {
        EVP_PKEY_free(pkey);
        return false;
    }

    // Verify
    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    if (!mdCtx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    bool result = false;
    if (EVP_DigestVerifyInit(mdCtx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
        if (EVP_DigestVerify(mdCtx, sigBytes.data(), sigBytes.size(),
                             reinterpret_cast<const unsigned char*>(data.data()),
                             data.size()) == 1) {
            result = true;
        }
    }

    EVP_MD_CTX_free(mdCtx);
    EVP_PKEY_free(pkey);
    return result;
}

// ============================================================================
// ── Config helpers ────────────────────────────────────────────────────────────
// ============================================================================

std::string CryptoUtil::getAesKey() {
    const char* env = std::getenv(kEnvAesKey);
    if (env && std::strlen(env) > 0) {
        std::string key(env);
        // Pad or truncate to exactly 32 bytes
        if (key.size() < 32) key.resize(32, '\0');
        else if (key.size() > 32) key.resize(32);
        return key;
    }
    LOG_WARN << "[Crypto] " << kEnvAesKey
             << " not set — using default (INSECURE for production)";
    return kDefaultAesKey;
}

std::string CryptoUtil::getWebhookSecret() {
    const char* env = std::getenv(kEnvWebhookSecret);
    if (env && std::strlen(env) > 0) {
        return env;
    }
    LOG_WARN << "[Crypto] " << kEnvWebhookSecret
             << " not set — using default (INSECURE for production)";
    return kDefaultWebhookSecret;
}

} // namespace idc
