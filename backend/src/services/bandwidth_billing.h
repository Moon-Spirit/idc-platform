#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace idc {

/// Result of a single port's 95th percentile calculation.
struct Port95Result {
    int64_t portIndex;       ///< Port index (1-based, e.g. port-1)
    double  billingRate;     ///< 95th percentile rate (Mbps)
    int64_t totalSamples;    ///< Total samples for this port
    int64_t rankUsed;        ///< Rank position used (FLOOR(total * 0.05) + 1)
    double  bytesOutRate;    ///< Raw bytes_out_rate value at rank
};

/// Combined result for a subscription (may have multiple ports).
struct BandwidthBillResult {
    double  billingRate;     ///< Aggregated billing rate (sum of port rates)
    int64_t totalSamples;    ///< Sum of samples across all ports
    int64_t rankUsed;        ///< Rank used (based on total across ports)
    double  amount;          ///< Calculated bill amount (rate * unit_price)
    bool    usedFallback;    ///< True if fell back to commit rate (<20 samples)
    Json::Value portResults; ///< Array of per-port results as JSON
};

/// 95th percentile bandwidth billing calculation.
///
/// Uses the TimescaleDB bandwidth_samples hypertable with the Oracle-reviewed
/// formula: FLOOR(total_count * 0.05) + 1
///
/// Performs per-port calculation and aggregates multi-port subscriptions
/// into a single bill amount.
///
/// Insufficient samples: if fewer than 20 samples exist for any port, falls
/// back to using commit_rate * committed_bandwidth from the subscription's
/// product specs.
class BandwidthBillingService {
public:
    /// Calculate 95th percentile bandwidth for a subscription over a period.
    ///
    /// @param subscription_id  Subscription ID to bill
    /// @param period_start     Start of billing period (ISO 8601 / TIMESTAMPTZ)
    /// @param period_end       End of billing period (ISO 8601 / TIMESTAMPTZ)
    /// @param unit_price       Price per Mbps (from product_prices.bandwidth_95_price).
    ///                         If 0, amount will be 0 but billing_rate is still computed.
    /// @return BandwidthBillResult with billing_rate, total_samples, rank_used, amount, portResults
    static BandwidthBillResult calculate95thPercentile(
        int64_t subscription_id,
        const std::string& period_start,
        const std::string& period_end,
        double unit_price = 0.0);

    /// Get 95th percentile bandwidth for time-series charting.
    /// Returns daily 95th percentile values for a subscription over a range.
    ///
    /// @return JSON array of { date, billing_rate, total_samples }
    static Json::Value getDaily95thPercentiles(
        int64_t subscription_id,
        const std::string& period_start,
        const std::string& period_end);

    /// Get raw time-series bandwidth samples for charting.
    /// Returns samples aggregated at hourly level.
    ///
    /// @return JSON array of { time, port_id, bytes_out_rate }
    static Json::Value getTimeSeries(
        int64_t subscription_id,
        const std::string& period_start,
        const std::string& period_end);

private:
    /// Calculate 95th percentile for a single subscription+port combination.
    /// Returns {billing_rate, total_samples, rank_used} or zero billing_rate
    /// if insufficient samples.
    static Port95Result calculatePort95th(
        int64_t subscription_id,
        const std::string& portId,
        const std::string& period_start,
        const std::string& period_end);

    /// Get commit_rate from subscription product specs.
    /// Looks for specs.bandwidth.commit or specs.commit field.
    /// Returns 0.0 if not found.
    static double getCommitRate(int64_t subscription_id);
};

} // namespace idc
