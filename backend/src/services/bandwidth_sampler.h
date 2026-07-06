#pragma once

#include <json/json.h>
#include <string>

namespace idc {

/// Mock bandwidth data simulator for development and testing.
///
/// Generates realistic-looking bandwidth samples at 5-minute intervals
/// and inserts them into the bandwidth_samples hypertable.
///
/// For Phase 1: mock data only (real ZJMF data integration comes in Wave 5).
///
/// Sample patterns:
///   - Daily sinusoidal pattern (low at night, peak during business hours)
///   - Weekday/weekend variation (weekends have lower baseline)
///   - Random noise (±10%) for realistic jitter
///   - Occasional burst events (2x normal) at random intervals
///   - Multiple ports use correlated but independently jittered values
class BandwidthSampler {
public:
    /// Generate mock bandwidth samples for a subscription over a time range.
    ///
    /// Creates one sample every 5 minutes (288 samples/day) with realistic
    /// bandwidth patterns. Samples are inserted in batches of 100 for
    /// performance.
    ///
    /// @param subscription_id  Subscription to generate data for
    /// @param port_ids         List of port identifiers (e.g. ["port-1", "port-2"])
    /// @param start            Start time (ISO 8601)
    /// @param end              End time (ISO 8601)
    /// @param baseRateMbps     Base bandwidth rate in Mbps (default: 100)
    ///                         Actual samples will vary around this value.
    /// @return JSON summary: { subscription_id, port_count, samples_generated,
    ///                         start, end, base_rate_mbps }
    static Json::Value generateMockSamples(
        int64_t subscription_id,
        const Json::Value& port_ids,
        const std::string& start,
        const std::string& end,
        double baseRateMbps = 100.0);

    /// Convenience: generate samples for a single-port subscription.
    static Json::Value generateMockSamples(
        int64_t subscription_id,
        const std::string& port_id,
        const std::string& start,
        const std::string& end,
        double baseRateMbps = 100.0);

private:
    /// Generate a realistic bandwidth rate at a given time offset.
    ///
    /// Pattern = daily_sine_offset + weekend_factor + noise + bursts
    /// Returns value in Mbps.
    static double generateRate(double hourOfDay, int dayOfWeek,
                                double baseRate, bool isBurst);

    /// Parse ISO 8601 time string to components for pattern generation.
    struct TimeComponents {
        double hourOfDay;  ///< 0-24
        int    dayOfWeek;  ///< 0=Sunday, 6=Saturday
        int64_t unixSeconds;
    };
    static TimeComponents parseTime(const std::string& isoTime);

    /// Convert a random double in [0,1) using a simple LCG.
    /// Seed is maintained per-call for reproducible-ish sequences.
    static uint32_t xorshift32();
};

} // namespace idc
