#include "bandwidth_billing.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Private: calculate 95th percentile for a single port
// ═══════════════════════════════════════════════════════════════════════════

Port95Result BandwidthBillingService::calculatePort95th(
    int64_t subscription_id,
    const std::string& portId,
    const std::string& period_start,
    const std::string& period_end) {

    Port95Result result{};
    result.billingRate = 0.0;
    result.totalSamples = 0;
    result.rankUsed = 0;
    result.bytesOutRate = 0.0;

    auto db = DbClient::getClient();

    // Use covering index idx_bw_samples_billing(subscription_id, time DESC, bytes_out_rate DESC)
    // Step 1: Count total samples for the port in the period
    auto countResult = db->execSqlSync(
        "SELECT COUNT(*) AS total "
        "FROM bandwidth_samples "
        "WHERE subscription_id = $1 "
        "  AND port_id = $2 "
        "  AND time >= $3::timestamptz "
        "  AND time < $4::timestamptz",
        subscription_id, portId, period_start, period_end);

    int64_t totalCount = countResult.empty() ? 0 : countResult[0]["total"].as<int64_t>();
    result.totalSamples = totalCount;

    // If fewer than 20 samples, return 0 billing rate (caller will fall back)
    if (totalCount < 20) {
        return result;
    }

    // Step 2: Calculate rank using Oracle-reviewed formula
    // FLOOR(total_count * 0.05) + 1
    // Example: 100 samples -> exclude top 5% (5 samples) -> take rank 6
    int64_t rank = static_cast<int64_t>(std::floor(totalCount * 0.05)) + 1;
    result.rankUsed = rank;

    // Step 3: Get the sample at the calculated rank
    // Uses ROW_NUMBER() ordered by bytes_out_rate DESC
    auto rankResult = db->execSqlSync(
        "WITH sorted AS ("
        "  SELECT "
        "    bytes_out_rate,"
        "    ROW_NUMBER() OVER (ORDER BY bytes_out_rate DESC) AS rn"
        "  FROM bandwidth_samples"
        "  WHERE subscription_id = $1"
        "    AND port_id = $2"
        "    AND time >= $3::timestamptz"
        "    AND time < $4::timestamptz"
        ") "
        "SELECT bytes_out_rate FROM sorted WHERE rn = $5",
        subscription_id, portId, period_start, period_end, rank);

    if (!rankResult.empty()) {
        result.bytesOutRate = rankResult[0]["bytes_out_rate"].as<double>();
        result.billingRate = result.bytesOutRate;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Private: get commit rate from subscription product specs
// ═══════════════════════════════════════════════════════════════════════════

double BandwidthBillingService::getCommitRate(int64_t subscription_id) {
    auto db = DbClient::getClient();

    auto result = db->execSqlSync(
        "SELECT p.specs "
        "FROM subscriptions s "
        "JOIN products p ON p.id = s.product_id "
        "WHERE s.id = $1",
        subscription_id);

    if (result.empty()) {
        return 0.0;
    }

    std::string specsStr = result[0]["specs"].isNull()
        ? "{}"
        : result[0]["specs"].as<std::string>();

    Json::Reader reader;
    Json::Value specs;
    if (!reader.parse(specsStr, specs)) {
        return 0.0;
    }

    // Check for bandwidth specs: could be specs.bandwidth.commit or specs.commit
    if (specs.isMember("bandwidth") && specs["bandwidth"].isMember("commit")) {
        std::string commitStr = specs["bandwidth"]["commit"].asString();
        // Parse "1Gbps" -> 1000, "500Mbps" -> 500
        double value = 0.0;
        std::string numStr;
        for (char c : commitStr) {
            if (std::isdigit(c) || c == '.') {
                numStr += c;
            } else {
                break;
            }
        }
        if (!numStr.empty()) {
            value = std::stod(numStr);
            // Convert Gbps to Mbps
            if (commitStr.find('G') != std::string::npos ||
                commitStr.find('g') != std::string::npos) {
                value *= 1000.0;
            }
        }
        return value;
    }

    if (specs.isMember("commit")) {
        std::string commitStr = specs["commit"].asString();
        double value = 0.0;
        std::string numStr;
        for (char c : commitStr) {
            if (std::isdigit(c) || c == '.') {
                numStr += c;
            } else {
                break;
            }
        }
        if (!numStr.empty()) {
            value = std::stod(numStr);
            if (commitStr.find('G') != std::string::npos ||
                commitStr.find('g') != std::string::npos) {
                value *= 1000.0;
            }
        }
        return value;
    }

    return 0.0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public: calculate 95th percentile for a subscription (multi-port aware)
// ═══════════════════════════════════════════════════════════════════════════

BandwidthBillResult BandwidthBillingService::calculate95thPercentile(
    int64_t subscription_id,
    const std::string& period_start,
    const std::string& period_end,
    double unit_price) {

    BandwidthBillResult bill{};
    bill.billingRate = 0.0;
    bill.totalSamples = 0;
    bill.rankUsed = 0;
    bill.amount = 0.0;
    bill.usedFallback = false;
    bill.portResults = Json::arrayValue;

    auto db = DbClient::getClient();

    // Step 1: Discover all port_ids for this subscription in the period
    auto portResult = db->execSqlSync(
        "SELECT DISTINCT port_id "
        "FROM bandwidth_samples "
        "WHERE subscription_id = $1 "
        "  AND time >= $2::timestamptz "
        "  AND time < $3::timestamptz "
        "ORDER BY port_id",
        subscription_id, period_start, period_end);

    // If no samples exist at all, try fallback
    if (portResult.empty()) {
        double commitRate = getCommitRate(subscription_id);
        if (commitRate > 0.0) {
            bill.billingRate = commitRate;
            bill.amount = commitRate * unit_price;
            bill.usedFallback = true;
        }
        return bill;
    }

    bool anyPortInsufficient = false;

    // Step 2: Calculate 95th percentile per port
    for (const auto& row : portResult) {
        std::string portId = row["port_id"].as<std::string>();

        auto port95 = calculatePort95th(
            subscription_id, portId, period_start, period_end);

        // Convert port95 result to JSON
        Json::Value portJson;
        portJson["port_id"] = portId;
        portJson["billing_rate"] = port95.billingRate;
        portJson["total_samples"] = static_cast<Json::Int64>(port95.totalSamples);
        portJson["rank_used"] = static_cast<Json::Int64>(port95.rankUsed);
        portJson["bytes_out_rate"] = port95.bytesOutRate;
        bill.portResults.append(portJson);

        // If insufficient samples for this port, mark for fallback
        if (port95.totalSamples < 20) {
            anyPortInsufficient = true;
            continue;
        }

        bill.billingRate += port95.billingRate;
        bill.totalSamples += port95.totalSamples;
    }

    // Step 3: Determine rank used (report highest rank across ports)
    bill.rankUsed = static_cast<int64_t>(std::floor(bill.totalSamples * 0.05)) + 1;

    // Step 4: Fallback for insufficient samples
    if (anyPortInsufficient || bill.totalSamples < 20) {
        double commitRate = getCommitRate(subscription_id);
        if (commitRate > 0.0) {
            bill.billingRate = commitRate;
            bill.amount = commitRate * unit_price;
            bill.usedFallback = true;
            return bill;
        }
    }

    // Step 5: Calculate amount
    bill.amount = bill.billingRate * unit_price;

    return bill;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public: get daily 95th percentile values for charting
// ═══════════════════════════════════════════════════════════════════════════

Json::Value BandwidthBillingService::getDaily95thPercentiles(
    int64_t subscription_id,
    const std::string& period_start,
    const std::string& period_end) {

    auto db = DbClient::getClient();

    // For each day in the range, calculate the 95th percentile of all samples
    // across all ports. We do this by grouping by day and using the same
    // FLOOR formula per day.
    auto rows = db->execSqlSync(
        "WITH daily_sorted AS ("
        "  SELECT "
        "    date_trunc('day', time) AS day,"
        "    bytes_out_rate,"
        "    ROW_NUMBER() OVER ("
        "      PARTITION BY date_trunc('day', time) "
        "      ORDER BY bytes_out_rate DESC"
        "    ) AS rn,"
        "    COUNT(*) OVER ("
        "      PARTITION BY date_trunc('day', time)"
        "    ) AS total"
        "  FROM bandwidth_samples"
        "  WHERE subscription_id = $1"
        "    AND time >= $2::timestamptz"
        "    AND time < $3::timestamptz"
        ")"
        "SELECT "
        "  day,"
        "  bytes_out_rate AS billing_rate,"
        "  total,"
        "  rn AS rank_used"
        "FROM daily_sorted "
        "WHERE rn = FLOOR(total * 0.05)::bigint + 1 "
        "ORDER BY day",
        subscription_id, period_start, period_end);

    Json::Value result(Json::arrayValue);
    for (const auto& row : rows) {
        Json::Value entry;
        entry["date"] = row["day"].as<std::string>();
        entry["billing_rate"] = row["billing_rate"].as<double>();
        entry["total_samples"] = static_cast<Json::Int64>(row["total"].as<int64_t>());
        entry["rank_used"] = static_cast<Json::Int64>(row["rank_used"].as<int64_t>());
        result.append(entry);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public: get raw time-series for charting
// ═══════════════════════════════════════════════════════════════════════════

Json::Value BandwidthBillingService::getTimeSeries(
    int64_t subscription_id,
    const std::string& period_start,
    const std::string& period_end) {

    auto db = DbClient::getClient();

    // Return hourly-averaged samples for charting (reduces data volume)
    auto rows = db->execSqlSync(
        "SELECT "
        "  date_trunc('hour', time) AS bucket,"
        "  port_id,"
        "  AVG(bytes_out_rate) AS avg_out_rate,"
        "  MAX(bytes_out_rate) AS max_out_rate,"
        "  COUNT(*) AS sample_count"
        "FROM bandwidth_samples"
        "WHERE subscription_id = $1"
        "  AND time >= $2::timestamptz"
        "  AND time < $3::timestamptz"
        "GROUP BY bucket, port_id"
        "ORDER BY bucket, port_id",
        subscription_id, period_start, period_end);

    Json::Value result(Json::arrayValue);
    for (const auto& row : rows) {
        Json::Value entry;
        entry["time"] = row["bucket"].as<std::string>();
        entry["port_id"] = row["port_id"].as<std::string>();
        entry["avg_out_rate"] = row["avg_out_rate"].as<double>();
        entry["max_out_rate"] = row["max_out_rate"].as<double>();
        entry["sample_count"] = static_cast<Json::Int64>(row["sample_count"].as<int64_t>());
        result.append(entry);
    }

    return result;
}

} // namespace idc
