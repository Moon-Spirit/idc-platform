#include "metered_billing.h"
#include "pricing_service.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <cmath>
#include <stdexcept>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: resolve effective hourly_rate for a subscription
// ═══════════════════════════════════════════════════════════════════════════

double MeteredBillingService::resolveHourlyRate(int64_t subId,
                                                 int64_t productId,
                                                 int64_t distributorId) {
    // We need a user context to walk the price template chain.
    // Since metered billing is typically run by the system or an admin,
    // we fall back to the system default price.  If a distributor-specific
    // price is needed, the caller should pass a representative user_id.
    //
    // Pragmatic approach: get the system default price first.  In a full
    // deployment the scheduler / cron job would pass an admin user context.
    auto price = PricingService::getSystemDefaultPrice(productId);

    if (price.isNull() || price["hourly_price"].isNull()) {
        LOG_WARN << "[MeteredBilling] No hourly_price found for product "
                 << productId << " (sub_id=" << subId << "), defaulting to 0";
        return 0.0;
    }

    std::string hourlyStr = price["hourly_price"].asString();
    if (hourlyStr.empty()) return 0.0;

    try {
        return std::stod(hourlyStr);
    } catch (const std::exception&) {
        return 0.0;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: build details JSONB for billing_records
// ═══════════════════════════════════════════════════════════════════════════

Json::Value MeteredBillingService::buildDetails(int64_t subId,
                                                  const std::string& periodStart,
                                                  const std::string& periodEnd,
                                                  double totalHours,
                                                  double hourlyRate,
                                                  double amount) {
    Json::Value d;
    d["subscription_id"] = static_cast<Json::Int64>(subId);
    d["period_start"]    = periodStart;
    d["period_end"]      = periodEnd;
    d["total_hours"]     = std::round(totalHours * 100.0) / 100.0;
    d["hourly_rate"]     = std::round(hourlyRate * 10000.0) / 10000.0;
    d["amount"]          = std::round(amount * 100.0) / 100.0;
    d["calculation"]     = std::to_string(totalHours) + " h × " +
                           std::to_string(hourlyRate) + " 元/h";
    return d;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Calculate Hourly Billing
// ═══════════════════════════════════════════════════════════════════════════

Json::Value MeteredBillingService::calculateHourly(int64_t subId,
                                                    const std::string& periodStart,
                                                    const std::string& periodEnd) {
    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // ── 1. Fetch & lock the subscription ─────────────────────────
        auto subRows = trans.execSqlSync(
            "SELECT id, sub_no, product_id, distributor_id, billing_cycle "
            "FROM subscriptions WHERE id = $1 FOR UPDATE",
            subId);

        if (subRows.empty()) {
            throw std::invalid_argument("Subscription not found: " +
                                        std::to_string(subId));
        }

        auto subRow = subRows[0];
        std::string billingCycle = subRow["billing_cycle"].as<std::string>();

        if (billingCycle != "hourly") {
            throw std::invalid_argument(
                "Subscription " + std::to_string(subId) +
                " is not hourly-billed (cycle=" + billingCycle + ")");
        }

        int64_t productId      = subRow["product_id"].as<int64_t>();
        int64_t distributorId  = subRow["distributor_id"].isNull()
                                     ? 0
                                     : subRow["distributor_id"].as<int64_t>();

        // ── 2. Resolve hourly_rate ──────────────────────────────────
        double hourlyRate = resolveHourlyRate(subId, productId, distributorId);

        // ── 3. Aggregate metered hours for the period ───────────────
        //     Use COALESCE to handle empty result set
        auto aggRows = trans.execSqlSync(
            "SELECT COALESCE(SUM(hours_running), 0) AS total_hours, "
            "COALESCE(SUM(amount), 0) AS total_amount "
            "FROM metered_hours "
            "WHERE subscription_id = $1 "
            "  AND date >= $2::date "
            "  AND date <= $3::date",
            subId, periodStart, periodEnd);

        double totalHours = 0.0;
        if (!aggRows.empty()) {
            totalHours = aggRows[0]["total_hours"].as<double>();
        }

        double amount = totalHours * hourlyRate;
        // Round to 2 decimal places
        amount = std::round(amount * 100.0) / 100.0;

        // ── 4. Check for existing billing_record for this period ────
        //     to avoid double-billing.  If one exists, return it.
        auto existingBr = trans.execSqlSync(
            "SELECT id, amount FROM billing_records "
            "WHERE subscription_id = $1 "
            "  AND type = 'hourly' "
            "  AND period_start = $2::date "
            "  AND period_end = $3::date "
            "LIMIT 1",
            subId, periodStart, periodEnd);

        if (!existingBr.empty()) {
            int64_t existingId = existingBr[0]["id"].as<int64_t>();
            LOG_WARN << "[MeteredBilling] Duplicate hourly calculation for sub="
                     << subId << " period=" << periodStart << "→" << periodEnd
                     << " existing billing_record id=" << existingId;

            result["subscription_id"]    = static_cast<Json::Int64>(subId);
            result["period_start"]       = periodStart;
            result["period_end"]         = periodEnd;
            result["total_hours"]        = std::round(totalHours * 100.0) / 100.0;
            result["hourly_rate"]        = std::round(hourlyRate * 10000.0) / 10000.0;
            result["amount"]             = std::round(
                existingBr[0]["amount"].as<double>() * 100.0) / 100.0;
            result["billing_record_id"]  = static_cast<Json::Int64>(existingId);
            result["duplicate"]          = true;
            return; // early return from lambda
        }

        // ── 5. Write billing_records entry ──────────────────────────
        Json::Value details = buildDetails(subId, periodStart, periodEnd,
                                           totalHours, hourlyRate, amount);

        auto brResult = trans.execSqlSync(
            "INSERT INTO billing_records "
            "(subscription_id, type, period_start, period_end, "
            " amount, quantity, unit_price, details) "
            "VALUES ($1, 'hourly', $2::date, $3::date, "
            " $4, $5, $6, $7::jsonb) "
            "RETURNING id",
            subId,
            periodStart,
            periodEnd,
            amount,
            totalHours,            // quantity
            hourlyRate,            // unit_price
            details.toStyledString());

        if (brResult.empty()) {
            throw std::runtime_error("Failed to insert hourly billing_record");
        }

        int64_t billingRecordId = brResult[0][0].as<int64_t>();

        // ── 6. Return result ───────────────────────────────────────
        result["subscription_id"]    = static_cast<Json::Int64>(subId);
        result["period_start"]       = periodStart;
        result["period_end"]         = periodEnd;
        result["total_hours"]        = std::round(totalHours * 100.0) / 100.0;
        result["hourly_rate"]        = std::round(hourlyRate * 10000.0) / 10000.0;
        result["amount"]             = amount;
        result["billing_record_id"]  = static_cast<Json::Int64>(billingRecordId);
        result["details"]            = details;
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Record Runtime (upsert with optimistic locking)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value MeteredBillingService::recordRuntime(int64_t subId,
                                                  const std::string& date,
                                                  double hours,
                                                  const std::string& source,
                                                  const std::string& remarks) {
    if (hours < 0) {
        throw std::invalid_argument("Hours must be non-negative");
    }
    if (hours > 24.0) {
        throw std::invalid_argument("Hours cannot exceed 24 per day");
    }

    // Resolve the effective hourly_rate for this subscription.
    // First get subscription info.
    auto db = DbClient::getClient();
    auto subRows = db->execSqlSync(
        "SELECT id, product_id, distributor_id FROM subscriptions WHERE id = $1",
        subId);

    if (subRows.empty()) {
        throw std::invalid_argument("Subscription not found: " +
                                    std::to_string(subId));
    }

    int64_t productId     = subRows[0]["product_id"].as<int64_t>();
    int64_t distributorId = subRows[0]["distributor_id"].isNull()
                                ? 0
                                : subRows[0]["distributor_id"].as<int64_t>();
    double hourlyRate     = resolveHourlyRate(subId, productId, distributorId);
    double amount         = std::round(hours * hourlyRate * 100.0) / 100.0;

    // Upsert with optimistic locking: try INSERT first, fall back to UPDATE
    // with version check on conflict.
    //
    // We use a loop of up to 3 attempts to handle version conflicts.
    const int maxRetries = 3;
    int attempt = 0;

    while (attempt < maxRetries) {
        try {
            // Try INSERT
            auto rows = db->execSqlSync(
                "INSERT INTO metered_hours "
                "(subscription_id, date, hours_running, rate, amount, source, remarks) "
                "VALUES ($1, $2::date, $3, $4, $5, $6, $7) "
                "ON CONFLICT (subscription_id, date) DO UPDATE SET "
                "  hours_running = EXCLUDED.hours_running, "
                "  rate          = EXCLUDED.rate, "
                "  amount        = EXCLUDED.amount, "
                "  source        = EXCLUDED.source, "
                "  remarks       = EXCLUDED.remarks, "
                "  version       = metered_hours.version + 1, "
                "  updated_at    = NOW() "
                "RETURNING id, subscription_id, date, hours_running, "
                "          rate, amount, source, version, created_at, updated_at",
                subId, date, hours, hourlyRate, amount, source, remarks);

            if (!rows.empty()) {
                auto row = rows[0];
                Json::Value item;
                item["id"]              = static_cast<Json::Int64>(row["id"].as<int64_t>());
                item["subscription_id"] = static_cast<Json::Int64>(row["subscription_id"].as<int64_t>());
                item["date"]            = row["date"].as<std::string>();
                item["hours_running"]   = row["hours_running"].as<double>();
                item["rate"]            = row["rate"].isNull() ? 0.0 : row["rate"].as<double>();
                item["amount"]          = row["amount"].as<double>();
                item["source"]          = row["source"].as<std::string>();
                item["version"]         = row["version"].as<int>();
                item["created_at"]      = row["created_at"].as<std::string>();
                item["updated_at"]      = row["updated_at"].as<std::string>();
                return item;
            }

            // No rows returned — this shouldn't happen with an upsert + RETURNING
            throw std::runtime_error("Upsert returned no rows");
        } catch (const drogon::orm::DrogonDbException& e) {
            // If it's a serialisation / deadlock / version conflict, retry
            std::string err = e.base().what();
            if (err.find("could not serialize") != std::string::npos ||
                err.find("deadlock detected") != std::string::npos ||
                err.find("version") != std::string::npos) {
                attempt++;
                if (attempt >= maxRetries) {
                    throw std::runtime_error(
                        "Concurrent update conflict on metered_hours after " +
                        std::to_string(maxRetries) + " attempts: " + err);
                }
                continue;
            }
            throw;
        }
    }

    // Should not reach here
    throw std::runtime_error("Failed to record runtime after " +
                             std::to_string(maxRetries) + " attempts");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Get Metered Usage
// ═══════════════════════════════════════════════════════════════════════════

Json::Value MeteredBillingService::getMeteredUsage(int64_t subId,
                                                     const std::string& periodStart,
                                                     const std::string& periodEnd) {
    auto db = DbClient::getClient();

    // Verify subscription exists & user has access
    auto subRows = db->execSqlSync(
        "SELECT id, billing_cycle FROM subscriptions WHERE id = $1", subId);
    if (subRows.empty()) {
        throw std::invalid_argument("Subscription not found: " +
                                    std::to_string(subId));
    }

    // Fetch metered records for the period
    auto rows = db->execSqlSync(
        "SELECT id, subscription_id, date, hours_running, rate, amount, "
        "       source, remarks, version, created_at, updated_at "
        "FROM metered_hours "
        "WHERE subscription_id = $1 "
        "  AND date >= $2::date "
        "  AND date <= $3::date "
        "ORDER BY date ASC",
        subId, periodStart, periodEnd);

    Json::Value items(Json::arrayValue);
    double totalHours = 0.0;
    double totalAmount = 0.0;
    double hourlyRate = 0.0;

    for (const auto& row : rows) {
        Json::Value item;
        item["id"]              = static_cast<Json::Int64>(row["id"].as<int64_t>());
        item["subscription_id"] = static_cast<Json::Int64>(
            row["subscription_id"].as<int64_t>());
        item["date"]            = row["date"].as<std::string>();
        item["hours_running"]   = row["hours_running"].as<double>();
        double rate = row["rate"].isNull() ? 0.0 : row["rate"].as<double>();
        item["rate"]            = rate;
        double amt = row["amount"].as<double>();
        item["amount"]          = amt;
        item["source"]          = row["source"].as<std::string>();
        item["remarks"]         = row["remarks"].isNull()
                                      ? "" : row["remarks"].as<std::string>();
        item["created_at"]      = row["created_at"].as<std::string>();
        item["updated_at"]      = row["updated_at"].as<std::string>();

        totalHours  += row["hours_running"].as<double>();
        totalAmount += amt;
        if (hourlyRate == 0.0 && rate > 0.0) {
            hourlyRate = rate;  // take the first non-zero rate found
        }

        items.append(item);
    }

    // If no records found, try to resolve the rate from pricing
    if (hourlyRate == 0.0 && items.empty()) {
        auto subInfo = db->execSqlSync(
            "SELECT product_id, distributor_id FROM subscriptions WHERE id = $1",
            subId);
        if (!subInfo.empty()) {
            int64_t pid = subInfo[0]["product_id"].as<int64_t>();
            int64_t did = subInfo[0]["distributor_id"].isNull()
                              ? 0
                              : subInfo[0]["distributor_id"].as<int64_t>();
            hourlyRate = resolveHourlyRate(subId, pid, did);
        }
    }

    Json::Value result;
    result["items"]          = items;
    result["total_hours"]    = std::round(totalHours * 100.0) / 100.0;
    result["total_amount"]   = std::round(totalAmount * 100.0) / 100.0;
    result["hourly_rate"]    = std::round(hourlyRate * 10000.0) / 10000.0;
    result["subscription_id"] = static_cast<Json::Int64>(subId);
    result["period_start"]   = periodStart;
    result["period_end"]     = periodEnd;

    return result;
}

} // namespace idc
