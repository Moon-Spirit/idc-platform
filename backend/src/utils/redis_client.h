#pragma once

#include <drogon/nosql/RedisClient.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace idc {

/// RedisClient convenience wrapper.
///
/// All methods are best-effort: if Redis is unavailable or returns an error,
/// they log a warning and return an empty / false result (graceful degradation).
///
/// Usage:
///   RedisClient::set("key", "value");
///   auto val = RedisClient::get("key");
///   RedisClient::expire("key", 60);
///
/// Distributed lock:
///   auto lock = RedisClient::Lock("my_resource");
///   if (lock.acquire()) {
///       // critical section
///   }  // released automatically on destruction
class RedisClient {
public:
    /// Default Redis client name in config.json.
    static constexpr const char* kDefaultRedisName = "idc_redis";

    /// Get a RedisClient from the framework pool.
    static drogon::nosql::RedisClientPtr getClient(
        const std::string& name = kDefaultRedisName) {
        return drogon::app().getRedisClient(name);
    }

    // ── Key-value operations ─────────────────────────────────────────────────

    /// Set a key to a string value.
    static bool set(const std::string& key, const std::string& value) {
        return exec("SET %s %s", key, value);
    }

    /// Set a key with an expiry TTL (seconds).
    static bool setex(const std::string& key, int ttl, const std::string& value) {
        return exec("SETEX %s %d %s", key, ttl, value);
    }

    /// Get the string value of a key.
    static std::optional<std::string> get(const std::string& key) {
        try {
            auto res = getClient()->execCommandSync("GET %s", key.c_str());
            if (res && res->type() != drogon::nosql::RedisResultType::kNil) {
                return res->asString();
            }
        } catch (const std::exception& e) {
            LOG_WARN << "[RedisClient] GET " << key << " failed: " << e.what();
        }
        return std::nullopt;
    }

    /// Delete one or more keys. Returns number of keys removed.
    static int64_t del(const std::string& key) {
        try {
            auto res = getClient()->execCommandSync("DEL %s", key.c_str());
            if (res) return res->asInteger();
        } catch (const std::exception& e) {
            LOG_WARN << "[RedisClient] DEL " << key << " failed: " << e.what();
        }
        return 0;
    }

    /// Set a TTL (seconds) on a key. Returns true if the timeout was set.
    static bool expire(const std::string& key, int ttl) {
        try {
            auto res = getClient()->execCommandSync("EXPIRE %s %d", key.c_str(), ttl);
            return res && res->asInteger() > 0;
        } catch (const std::exception& e) {
            LOG_WARN << "[RedisClient] EXPIRE " << key << " failed: " << e.what();
        }
        return false;
    }

    /// Check if a key exists.
    static bool exists(const std::string& key) {
        try {
            auto res = getClient()->execCommandSync("EXISTS %s", key.c_str());
            return res && res->asInteger() > 0;
        } catch (const std::exception& e) {
            LOG_WARN << "[RedisClient] EXISTS " << key << " failed: " << e.what();
        }
        return false;
    }

    // ── Distributed lock ─────────────────────────────────────────────────────

    class Lock {
    public:
        /// @param key   Resource key (auto-prefixed with "lock:").
        /// @param ttl   Lock expiry in seconds (auto-release safety net).
        explicit Lock(const std::string& key, int ttl = 30)
            : key_("lock:" + key), ttl_(ttl), token_(generateToken()), acquired_(false) {}

        ~Lock() {
            if (acquired_) release();
        }

        /// Acquire the lock (blocking, best-effort).
        bool acquire() {
            try {
                auto res = getClient()->execCommandSync(
                    "SET %s %s NX EX %d",
                    key_.c_str(), token_.c_str(), ttl_);
                acquired_ = res && res->asString() == "OK";
                return acquired_;
            } catch (const std::exception& e) {
                LOG_WARN << "[RedisClient] Lock acquire " << key_ << " failed: "
                         << e.what();
            }
            return false;
        }

        /// Release the lock (Lua script ensures only the owner releases).
        void release() {
            if (!acquired_) return;
            try {
                // Safe unlock: only delete if our token matches
                const char* script =
                    "if redis.call('get', KEYS[1]) == ARGV[1] then "
                    "  return redis.call('del', KEYS[1]) "
                    "else "
                    "  return 0 "
                    "end";
                getClient()->execCommandSync(
                    "EVAL %s 1 %s %s", script, key_.c_str(), token_.c_str());
            } catch (const std::exception& e) {
                LOG_WARN << "[RedisClient] Lock release " << key_ << " failed: "
                         << e.what();
            }
            acquired_ = false;
        }

        bool isAcquired() const { return acquired_; }

        // Non-copyable, movable
        Lock(const Lock&)            = delete;
        Lock& operator=(const Lock&) = delete;
        Lock(Lock&& other) noexcept
            : key_(std::move(other.key_)),
              ttl_(other.ttl_),
              token_(std::move(other.token_)),
              acquired_(other.acquired_) {
            other.acquired_ = false;
        }
        Lock& operator=(Lock&& other) noexcept {
            if (this != &other) {
                if (acquired_) release();
                key_      = std::move(other.key_);
                ttl_      = other.ttl_;
                token_    = std::move(other.token_);
                acquired_ = other.acquired_;
                other.acquired_ = false;
            }
            return *this;
        }

    private:
        static std::string generateToken() {
            // Thread ID + counter for uniqueness
            static std::atomic<uint64_t> counter{0};
            auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
            return std::to_string(tid) + ":" + std::to_string(++counter);
        }

        std::string key_;
        int ttl_;
        std::string token_;
        bool acquired_;
    };

private:
    RedisClient() = delete;

    /// Execute a non-result Redis command (SET, SETEX, etc.).
    template <typename... Args>
    static bool exec(const char* fmt, const std::string& key, Args&&... args) {
        try {
            auto res = getClient()->execCommandSync(fmt, key.c_str(),
                                                    std::forward<Args>(args)...);
            return res && res->asString() == "OK";
        } catch (const std::exception& e) {
            LOG_WARN << "[RedisClient] command failed: " << e.what();
        }
        return false;
    }
};

}  // namespace idc
