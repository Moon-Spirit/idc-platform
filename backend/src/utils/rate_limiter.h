#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>

namespace idc {

/// Token-bucket rate limiter per connection.
///
/// Default configuration: 30 requests per minute per connection.
/// Tries to acquire a token before every API call; blocks if the bucket is empty.
///
/// Thread-safe. Uses a global map keyed by connection ID.
class RateLimiter {
public:
    /// Default rate: 30 requests per 60 seconds.
    static constexpr double  kDefaultRate  = 30.0;
    static constexpr double  kDefaultBurst = 30.0;
    static constexpr int     kDefaultWindowSec = 60;

    /// ── Token bucket instance ────────────────────────────────────────────────

    class Bucket {
    public:
        Bucket(double rate, double burst)
            : rate_(rate)
            , burst_(burst)
            , tokens_(burst)
            , lastRefill_(std::chrono::steady_clock::now()) {}

        /// Try to consume one token.
        /// Blocks up to `timeoutMs` milliseconds if no token is available.
        /// Returns true if a token was acquired.
        bool tryConsume(int timeoutMs = 1000);

    private:
        double rate_;
        double burst_;
        double tokens_;
        std::chrono::steady_clock::time_point lastRefill_;
        std::mutex mtx_;
        std::condition_variable cv_;

        void refill();
    };

    /// ── Singleton ────────────────────────────────────────────────────────────

    /// Get the global rate limiter instance.
    static RateLimiter& instance();

    /// ── Per-connection access ─────────────────────────────────────────────────

    /// Get or create a bucket for the given connection ID.
    Bucket& getBucket(int64_t connectionId);

    /// Configure the rate for a connection (rate = requests per window).
    void setRate(int64_t connectionId, double rate, double burst);

    /// Reset a connection's bucket (e.g. on connection config change).
    void resetBucket(int64_t connectionId);

private:
    RateLimiter() = default;

    struct BucketEntry {
        std::unique_ptr<Bucket> bucket;
        double rate  = kDefaultRate;
        double burst = kDefaultBurst;
    };

    std::mutex globalMtx_;
    std::unordered_map<int64_t, BucketEntry> buckets_;
};

} // namespace idc
