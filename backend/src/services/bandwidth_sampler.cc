#include "bandwidth_sampler.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

namespace idc {

namespace {
    // Simple LCG state for reproducible-ish randomness
    thread_local uint32_t g_seed = 0;

    uint32_t xorshift32_impl() {
        if (g_seed == 0) {
            g_seed = static_cast<uint32_t>(std::time(nullptr) ^ 0xDEADBEEF);
        }
        uint32_t x = g_seed;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        g_seed = x;
        return x;
    }

    /// Return a random double in [0, 1)
    double randDouble() {
        return static_cast<double>(xorshift32_impl()) / 4294967296.0;
    }

    /// Return a random double in [min, max)
    double randRange(double min, double max) {
        return min + randDouble() * (max - min);
    }
} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  Private: XorShift32 PRNG (replacement for static class method)
// ═══════════════════════════════════════════════════════════════════════════

uint32_t BandwidthSampler::xorshift32() {
    return xorshift32_impl();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Private: parse ISO 8601 time
// ═══════════════════════════════════════════════════════════════════════════

BandwidthSampler::TimeComponents BandwidthSampler::parseTime(
    const std::string& isoTime) {
    // Parse ISO 8601 format: "2026-07-01 00:00:00+08" or "2026-07-01T00:00:00Z"
    struct tm tm = {};
    int tzHour = 0, tzMin = 0;

    // Try PostgreSQL format first: "2026-07-01 00:00:00+08"
    if (sscanf(isoTime.c_str(), "%d-%d-%d %d:%d:%d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec) >= 3) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = -1;
    } else {
        // Try ISO 8601 with T: "2026-07-01T00:00:00Z"
        sscanf(isoTime.c_str(), "%d-%d-%dT%d:%d:%d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = -1;
    }

    time_t unixTime = timegm(&tm);
    // Adjust for timezone offset if present
    if (sscanf(isoTime.c_str(), "%*d-%*d-%*d%*[ T]%*d:%*d:%*d%d:%d",
               &tzHour, &tzMin) >= 1) {
        // The format is +08 or -05:00
        unixTime -= tzHour * 3600;
        unixTime -= tzMin * 60;
    }

    struct tm* gmt = gmtime(&unixTime);
    TimeComponents comp;
    comp.hourOfDay = static_cast<double>(gmt->tm_hour) +
                     static_cast<double>(gmt->tm_min) / 60.0;
    comp.dayOfWeek = gmt->tm_wday;
    comp.unixSeconds = static_cast<int64_t>(unixTime);
    return comp;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Private: generate a realistic bandwidth rate
// ═══════════════════════════════════════════════════════════════════════════

double BandwidthSampler::generateRate(double hourOfDay, int dayOfWeek,
                                       double baseRate, bool isBurst) {
    // Daily sinusoidal pattern: peak at 14:00, trough at 04:00
    // Scale: 0.3 (trough) to 1.0 (peak)
    double dailyFactor = 0.65 + 0.35 * std::cos((hourOfDay - 14.0) * (M_PI / 12.0));

    // Weekend factor: lower baseline on weekends
    double weekendFactor = (dayOfWeek == 0 || dayOfWeek == 6) ? 0.6 : 1.0;

    // Random noise: ±10%
    double noise = 1.0 + randRange(-0.1, 0.1);

    // Burst: 2x normal
    double burstFactor = isBurst ? 2.0 : 1.0;

    double rate = baseRate * dailyFactor * weekendFactor * noise * burstFactor;

    // Ensure minimum of 1 Mbps
    if (rate < 1.0) rate = 1.0;

    return rate;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public: generate mock samples (multi-port)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value BandwidthSampler::generateMockSamples(
    int64_t subscription_id,
    const Json::Value& port_ids,
    const std::string& start,
    const std::string& end,
    double baseRateMbps) {

    if (!port_ids.isArray() || port_ids.empty()) {
        Json::Value err;
        err["error"] = "port_ids must be a non-empty array";
        return err;
    }

    auto db = DbClient::getClient();

    // Parse start and end times
    TimeComponents startComp = parseTime(start);
    TimeComponents endComp = parseTime(end);

    int64_t startSec = startComp.unixSeconds;
    int64_t endSec = endComp.unixSeconds;
    int64_t intervalSec = 300; // 5 minutes

    // Validate subscription exists
    auto subCheck = db->execSqlSync(
        "SELECT id FROM subscriptions WHERE id = $1", subscription_id);
    if (subCheck.empty()) {
        Json::Value err;
        err["error"] = "Subscription not found: " + std::to_string(subscription_id);
        return err;
    }

    int64_t samplesGenerated = 0;
    int portCount = port_ids.size();

    // Re-seed for reproducibility per call
    g_seed = static_cast<uint32_t>(std::time(nullptr) ^
              static_cast<uint32_t>(subscription_id) ^ 0xBEEFCAFE);

    // Pre-compute burst times (randomly select ~2% of intervals as burst)
    int64_t totalSlots = (endSec - startSec) / intervalSec;
    std::vector<bool> isBurstSlot(static_cast<size_t>(totalSlots), false);
    for (size_t i = 0; i < isBurstSlot.size(); ++i) {
        if (randDouble() < 0.02) { // 2% chance of burst
            isBurstSlot[i] = true;
        }
    }

    std::vector<std::string> portIdStrs;
    for (const auto& pid : port_ids) {
        portIdStrs.push_back(pid.asString());
    }

    // Generate in batches of 100 for performance
    std::ostringstream batchSql;
    int batchCount = 0;

    for (int64_t t = startSec; t < endSec; t += intervalSec) {
        // Compute time components for this slot
        struct tm* gmt = gmtime(&t);
        double hourOfDay = static_cast<double>(gmt->tm_hour) +
                           static_cast<double>(gmt->tm_min) / 60.0;
        int dayOfWeek = gmt->tm_wday;
        size_t slotIdx = static_cast<size_t>((t - startSec) / intervalSec);
        bool burst = slotIdx < isBurstSlot.size() && isBurstSlot[slotIdx];

        // Generate ISO time string
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S+00", gmt);
        std::string timeStr(timeBuf);

        for (int pi = 0; pi < portCount; ++pi) {
            // Each port gets correlated but independently jittered rate
            double portJitter = 1.0 + randRange(-0.2, 0.2);
            double rateOut = generateRate(hourOfDay, dayOfWeek,
                                           baseRateMbps * portJitter, burst);
            double rateIn = rateOut * randRange(0.3, 0.8); // Inbound is lower
            double bytesOutRate = rateOut; // Already in Mbps
            double bytesInRate = rateIn;

            // Convert Mbps to bytes for the 5-minute cumulative counters
            // rate(bps) = rate(Mbps) * 1,000,000
            // bytes in 5 min = rate(bps) * 300 seconds / 8
            double bytesOut = (rateOut * 1000000.0 * 300.0) / 8.0;
            double bytesIn = (rateIn * 1000000.0 * 300.0) / 8.0;

            if (batchCount > 0) {
                batchSql << ",";
            }
            batchSql << "("
                     << "'" << timeStr << "',"
                     << subscription_id << ","
                     << "'" << portIdStrs[pi] << "',"
                     << static_cast<int64_t>(bytesIn) << ","
                     << static_cast<int64_t>(bytesOut) << ","
                     << bytesInRate << ","
                     << bytesOutRate
                     << ")";
            batchCount++;
            samplesGenerated++;

            // Flush batch every 100 rows
            if (batchCount >= 100) {
                std::string sql = "INSERT INTO bandwidth_samples "
                    "(time, subscription_id, port_id, bytes_in, bytes_out, "
                    "bytes_in_rate, bytes_out_rate) VALUES " +
                    batchSql.str();
                try {
                    db->execSqlSync(sql);
                } catch (const std::exception& e) {
                    LOG_ERROR << "[BandwidthSampler] Batch insert failed: "
                              << e.what();
                    // Continue with next batch
                }
                batchSql.str("");
                batchCount = 0;
            }
        }
    }

    // Flush remaining batch
    if (batchCount > 0) {
        std::string sql = "INSERT INTO bandwidth_samples "
            "(time, subscription_id, port_id, bytes_in, bytes_out, "
            "bytes_in_rate, bytes_out_rate) VALUES " +
            batchSql.str();
        try {
            db->execSqlSync(sql);
        } catch (const std::exception& e) {
            LOG_ERROR << "[BandwidthSampler] Final batch insert failed: "
                      << e.what();
        }
    }

    Json::Value result;
    result["subscription_id"] = static_cast<Json::Int64>(subscription_id);
    result["port_count"] = portCount;
    result["samples_generated"] = static_cast<Json::Int64>(samplesGenerated);
    result["start"] = start;
    result["end"] = end;
    result["base_rate_mbps"] = baseRateMbps;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public: generate mock samples (single-port convenience)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value BandwidthSampler::generateMockSamples(
    int64_t subscription_id,
    const std::string& port_id,
    const std::string& start,
    const std::string& end,
    double baseRateMbps) {

    Json::Value ports(Json::arrayValue);
    ports.append(port_id);
    return generateMockSamples(subscription_id, ports, start, end, baseRateMbps);
}

} // namespace idc
