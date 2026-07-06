#include "rate_limiter.h"
#include "logger.h"

namespace idc {

// ============================================================================
// ── Bucket ────────────────────────────────────────────────────────────────────
// ============================================================================

void RateLimiter::Bucket::refill() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - lastRefill_)
                       .count();

    double tokensToAdd = rate_ * static_cast<double>(elapsed) / 60000.0;
    if (tokensToAdd > 0) {
        tokens_ = std::min(tokens_ + tokensToAdd, burst_);
        lastRefill_ = now;
    }
}

bool RateLimiter::Bucket::tryConsume(int timeoutMs) {
    std::unique_lock<std::mutex> lock(mtx_);

    refill();

    if (tokens_ >= 1.0) {
        tokens_ -= 1.0;
        return true;
    }

    // Wait for a token to become available (with timeout)
    // Calculate expected wait time for one token
    double msPerToken = (rate_ > 0) ? (60000.0 / rate_) : 1000.0;
    int waitMs = std::min(timeoutMs, static_cast<int>(msPerToken));

    if (waitMs <= 0) return false;

    cv_.wait_for(lock, std::chrono::milliseconds(waitMs));

    // Try again after waiting
    refill();
    if (tokens_ >= 1.0) {
        tokens_ -= 1.0;
        return true;
    }

    return false;
}

// ============================================================================
// ── Singleton ────────────────────────────────────────────────────────────────
// ============================================================================

RateLimiter& RateLimiter::instance() {
    static RateLimiter inst;
    return inst;
}

// ============================================================================
// ── Per-connection ────────────────────────────────────────────────────────────
// ============================================================================

RateLimiter::Bucket& RateLimiter::getBucket(int64_t connectionId) {
    std::lock_guard<std::mutex> lock(globalMtx_);

    auto it = buckets_.find(connectionId);
    if (it != buckets_.end()) {
        return *it->second.bucket;
    }

    // Create new bucket with defaults
    BucketEntry entry;
    entry.rate   = kDefaultRate;
    entry.burst  = kDefaultBurst;
    entry.bucket = std::make_unique<Bucket>(kDefaultRate, kDefaultBurst);

    auto* ptr = entry.bucket.get();
    buckets_[connectionId] = std::move(entry);
    return *ptr;
}

void RateLimiter::setRate(int64_t connectionId, double rate, double burst) {
    std::lock_guard<std::mutex> lock(globalMtx_);
    buckets_[connectionId] = {
        std::make_unique<Bucket>(rate, burst),
        rate, burst
    };
    LOG_DEBUG << "[RateLimiter] Connection " << connectionId
              << " rate set to " << rate << " req/min (burst=" << burst << ")";
}

void RateLimiter::resetBucket(int64_t connectionId) {
    std::lock_guard<std::mutex> lock(globalMtx_);
    buckets_.erase(connectionId);
    LOG_DEBUG << "[RateLimiter] Connection " << connectionId << " bucket reset";
}

} // namespace idc
