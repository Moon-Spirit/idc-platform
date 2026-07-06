#include "addon_billing.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <stdexcept>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Valid addon types
// ═══════════════════════════════════════════════════════════════════════════

bool AddonBillingService::isValidAddonType(const std::string& type) {
    return type == "extra_ips" ||
           type == "ddos_protection" ||
           type == "backup_storage";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Date helpers
// ═══════════════════════════════════════════════════════════════════════════

int AddonBillingService::daysInMonth(const std::string& dateStr) {
    // Parse YYYY-MM-DD
    int year = 0, month = 0, day = 0;
    if (sscanf(dateStr.c_str(), "%d-%d-%d", &year, &month, &day) < 2) {
        return 30; // fallback
    }

    // Array of days per month
    static const int daysPerMonth[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) return 30;

    int days = daysPerMonth[month - 1];
    // Leap year check for February
    if (month == 2) {
        bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if (leap) days = 29;
    }
    return days;
}

int AddonBillingService::daysBetween(const std::string& start,
                                      const std::string& end) {
    // Parse both dates
    int sy = 0, sm = 0, sd = 0;
    int ey = 0, em = 0, ed = 0;
    if (sscanf(start.c_str(), "%d-%d-%d", &sy, &sm, &sd) < 3) return 0;
    if (sscanf(end.c_str(),   "%d-%d-%d", &ey, &em, &ed) < 3) return 0;

    // Convert to struct tm for timegm
    struct tm startTm = {};
    startTm.tm_year  = sy - 1900;
    startTm.tm_mon   = sm - 1;
    startTm.tm_mday  = sd;
    startTm.tm_hour  = 0;
    startTm.tm_min   = 0;
    startTm.tm_sec   = 0;
    startTm.tm_isdst = -1;

    struct tm endTm = {};
    endTm.tm_year   = ey - 1900;
    endTm.tm_mon    = em - 1;
    endTm.tm_mday   = ed;
    endTm.tm_hour   = 0;
    endTm.tm_min    = 0;
    endTm.tm_sec    = 0;
    endTm.tm_isdst  = -1;

    time_t startTime = timegm(&startTm);
    time_t endTime   = timegm(&endTm);

    if (endTime < startTime) return 0;

    double diffSec = difftime(endTime, startTime);
    int days = static_cast<int>(diffSec / 86400.0) + 1; // inclusive
    return days;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Calculate Addon Prorata
// ═══════════════════════════════════════════════════════════════════════════

Json::Value AddonBillingService::calculateAddonProrata(
    int64_t subId,
    const Json::Value& addonItem,
    const std::string& periodStart,
    const std::string& periodEnd) {

    // ── 1. Validate input ──────────────────────────────────────────
    if (addonItem.isNull() || !addonItem.isObject()) {
        throw std::invalid_argument("addonItem must be a JSON object");
    }

    if (!addonItem.isMember("addon_type") || !addonItem.isMember("monthly_price")) {
        throw std::invalid_argument(
            "addonItem must contain 'addon_type' and 'monthly_price'");
    }

    std::string addonType = addonItem["addon_type"].asString();
    if (!isValidAddonType(addonType)) {
        throw std::invalid_argument("Invalid addon_type: " + addonType +
                                    " (valid: extra_ips, ddos_protection, backup_storage)");
    }

    // Parse monthly_price (could be string or number in JSON)
    double monthlyPrice = 0.0;
    if (addonItem["monthly_price"].isNumeric()) {
        monthlyPrice = addonItem["monthly_price"].asDouble();
    } else if (addonItem["monthly_price"].isString()) {
        monthlyPrice = std::stod(addonItem["monthly_price"].asString());
    } else {
        throw std::invalid_argument("monthly_price must be a number or string");
    }

    if (monthlyPrice <= 0) {
        throw std::invalid_argument("monthly_price must be positive");
    }

    // ── 2. Determine charge window ─────────────────────────────────
    //     An addon may start mid-cycle → charge from start_date to period_end
    //     An addon may end   mid-cycle → charge from period_start to end_date
    std::string chargeStart = periodStart;
    std::string chargeEnd   = periodEnd;

    if (addonItem.isMember("start_date") && !addonItem["start_date"].isNull()) {
        std::string addonStart = addonItem["start_date"].asString();
        if (addonStart > chargeStart) {
            chargeStart = addonStart;  // addon started after period began
        }
    }

    if (addonItem.isMember("end_date") && !addonItem["end_date"].isNull()) {
        std::string addonEnd = addonItem["end_date"].asString();
        if (addonEnd < chargeEnd) {
            chargeEnd = addonEnd;  // addon ended before period ends
        }
    }

    // ── 3. Calculate prorata ───────────────────────────────────────
    int dim = daysInMonth(periodStart);  // days in the billing month
    if (dim <= 0) dim = 30;

    int daysUsed = daysBetween(chargeStart, chargeEnd);
    if (daysUsed > dim) daysUsed = dim;  // cap at full month
    if (daysUsed < 0) daysUsed = 0;

    double dailyRate = monthlyPrice / static_cast<double>(dim);
    double amount    = dailyRate * static_cast<double>(daysUsed);
    amount = std::round(amount * 100.0) / 100.0;

    // ── 4. Build result ────────────────────────────────────────────
    Json::Value details;
    details["addon_type"]       = addonType;
    details["monthly_price"]    = std::round(monthlyPrice * 100.0) / 100.0;
    details["days_in_month"]    = dim;
    details["charge_start"]     = chargeStart;
    details["charge_end"]       = chargeEnd;
    details["days_used"]        = daysUsed;
    details["daily_rate"]       = std::round(dailyRate * 10000.0) / 10000.0;
    details["calculation"]      = std::to_string(monthlyPrice) + " 元/月 ÷ " +
                                  std::to_string(dim) + " 天 × " +
                                  std::to_string(daysUsed) + " 天";

    Json::Value result;
    result["addon_type"]    = addonType;
    result["monthly_price"] = std::round(monthlyPrice * 100.0) / 100.0;
    result["days_in_month"] = dim;
    result["days_used"]     = daysUsed;
    result["daily_rate"]    = std::round(dailyRate * 10000.0) / 10000.0;
    result["amount"]        = amount;
    result["details"]       = details;

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Create Addon
// ═══════════════════════════════════════════════════════════════════════════

Json::Value AddonBillingService::createAddon(int64_t subId,
                                               const std::string& addonType,
                                               double monthlyPrice,
                                               const std::string& startDate,
                                               const Json::Value& specs) {
    if (!isValidAddonType(addonType)) {
        throw std::invalid_argument("Invalid addon_type: " + addonType);
    }
    if (monthlyPrice <= 0) {
        throw std::invalid_argument("monthly_price must be positive");
    }
    if (startDate.empty()) {
        throw std::invalid_argument("start_date is required");
    }

    auto db = DbClient::getClient();

    // Verify subscription exists
    auto subRows = db->execSqlSync(
        "SELECT id FROM subscriptions WHERE id = $1", subId);
    if (subRows.empty()) {
        throw std::invalid_argument("Subscription not found: " +
                                    std::to_string(subId));
    }

    // If specs is not an object, default to empty
    Json::Value effectiveSpecs = specs.isObject() ? specs : Json::objectValue;

    auto rows = db->execSqlSync(
        "INSERT INTO addon_services "
        "(subscription_id, addon_type, monthly_price, start_date, specs, status) "
        "VALUES ($1, $2, $3, $4::date, $5::jsonb, 'active') "
        "RETURNING id, subscription_id, addon_type, monthly_price, "
        "          start_date, end_date, status, specs, version, "
        "          created_at, updated_at",
        subId, addonType, monthlyPrice, startDate,
        effectiveSpecs.toStyledString());

    if (rows.empty()) {
        throw std::runtime_error("Failed to create addon service");
    }

    auto row = rows[0];

    Json::Value item;
    item["id"]              = static_cast<Json::Int64>(row["id"].as<int64_t>());
    item["subscription_id"] = static_cast<Json::Int64>(
        row["subscription_id"].as<int64_t>());
    item["addon_type"]      = row["addon_type"].as<std::string>();
    item["monthly_price"]   = row["monthly_price"].as<double>();
    item["start_date"]      = row["start_date"].as<std::string>();
    item["end_date"]        = row["end_date"].isNull()
                                  ? Json::nullValue
                                  : Json::Value(row["end_date"].as<std::string>());
    item["status"]          = row["status"].as<std::string>();

    // Parse specs JSONB
    std::string specsStr = row["specs"].as<std::string>();
    Json::Reader reader;
    Json::Value parsedSpecs;
    if (reader.parse(specsStr, parsedSpecs)) {
        item["specs"] = parsedSpecs;
    } else {
        item["specs"] = Json::objectValue;
    }

    item["version"]    = row["version"].as<int>();
    item["created_at"] = row["created_at"].as<std::string>();
    item["updated_at"] = row["updated_at"].as<std::string>();

    return item;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Cancel Addon
// ═══════════════════════════════════════════════════════════════════════════

Json::Value AddonBillingService::cancelAddon(int64_t addonId,
                                              const std::string& endDate) {
    if (endDate.empty()) {
        throw std::invalid_argument("end_date is required");
    }

    auto db = DbClient::getClient();

    // Fetch current state with lock
    auto rows = db->execSqlSync(
        "SELECT * FROM addon_services WHERE id = $1 FOR UPDATE", addonId);

    if (rows.empty()) {
        throw std::invalid_argument("Addon service not found: " +
                                    std::to_string(addonId));
    }

    auto row = rows[0];
    std::string status = row["status"].as<std::string>();

    if (status != "active") {
        throw std::invalid_argument(
            "Addon service is not active (status=" + status + ")");
    }

    // Update
    auto updatedRows = db->execSqlSync(
        "UPDATE addon_services SET "
        "  end_date = $1::date, "
        "  status = 'cancelled', "
        "  version = version + 1, "
        "  updated_at = NOW() "
        "WHERE id = $2 AND version = $3 "
        "RETURNING id, subscription_id, addon_type, monthly_price, "
        "          start_date, end_date, status, specs, version, "
        "          created_at, updated_at",
        endDate, addonId, row["version"].as<int>());

    if (updatedRows.empty()) {
        throw std::runtime_error(
            "Concurrent modification detected for addon_services id=" +
            std::to_string(addonId) +
            ". Please retry.");
    }

    auto updatedRow = updatedRows[0];

    Json::Value item;
    item["id"]              = static_cast<Json::Int64>(updatedRow["id"].as<int64_t>());
    item["subscription_id"] = static_cast<Json::Int64>(
        updatedRow["subscription_id"].as<int64_t>());
    item["addon_type"]      = updatedRow["addon_type"].as<std::string>();
    item["monthly_price"]   = updatedRow["monthly_price"].as<double>();
    item["start_date"]      = updatedRow["start_date"].as<std::string>();
    item["end_date"]        = updatedRow["end_date"].as<std::string>();
    item["status"]          = updatedRow["status"].as<std::string>();

    std::string specsStr = updatedRow["specs"].as<std::string>();
    Json::Reader reader;
    Json::Value parsedSpecs;
    if (reader.parse(specsStr, parsedSpecs)) {
        item["specs"] = parsedSpecs;
    } else {
        item["specs"] = Json::objectValue;
    }

    item["version"]    = updatedRow["version"].as<int>();
    item["created_at"] = updatedRow["created_at"].as<std::string>();
    item["updated_at"] = updatedRow["updated_at"].as<std::string>();

    return item;
}

// ═══════════════════════════════════════════════════════════════════════════
//  List Addons for a Subscription
// ═══════════════════════════════════════════════════════════════════════════

Json::Value AddonBillingService::listAddons(int64_t subId) {
    auto db = DbClient::getClient();

    auto rows = db->execSqlSync(
        "SELECT id, subscription_id, addon_type, monthly_price, "
        "       start_date, end_date, status, specs, version, "
        "       created_at, updated_at "
        "FROM addon_services "
        "WHERE subscription_id = $1 "
        "ORDER BY created_at DESC",
        subId);

    Json::Value items(Json::arrayValue);

    for (const auto& row : rows) {
        Json::Value item;
        item["id"]              = static_cast<Json::Int64>(row["id"].as<int64_t>());
        item["subscription_id"] = static_cast<Json::Int64>(
            row["subscription_id"].as<int64_t>());
        item["addon_type"]      = row["addon_type"].as<std::string>();
        item["monthly_price"]   = row["monthly_price"].as<double>();
        item["start_date"]      = row["start_date"].as<std::string>();
        item["end_date"]        = row["end_date"].isNull()
                                      ? Json::nullValue
                                      : Json::Value(row["end_date"].as<std::string>());
        item["status"]          = row["status"].as<std::string>();

        std::string specsStr = row["specs"].as<std::string>();
        Json::Reader reader;
        Json::Value parsedSpecs;
        if (reader.parse(specsStr, parsedSpecs)) {
            item["specs"] = parsedSpecs;
        } else {
            item["specs"] = Json::objectValue;
        }

        item["version"]    = row["version"].as<int>();
        item["created_at"] = row["created_at"].as<std::string>();
        item["updated_at"] = row["updated_at"].as<std::string>();

        items.append(item);
    }

    Json::Value result;
    result["items"] = items;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Get Addon by ID
// ═══════════════════════════════════════════════════════════════════════════

Json::Value AddonBillingService::getAddonById(int64_t addonId) {
    auto db = DbClient::getClient();

    auto rows = db->execSqlSync(
        "SELECT id, subscription_id, addon_type, monthly_price, "
        "       start_date, end_date, status, specs, version, "
        "       created_at, updated_at "
        "FROM addon_services WHERE id = $1",
        addonId);

    if (rows.empty()) {
        return Json::nullValue;
    }

    auto row = rows[0];

    Json::Value item;
    item["id"]              = static_cast<Json::Int64>(row["id"].as<int64_t>());
    item["subscription_id"] = static_cast<Json::Int64>(
        row["subscription_id"].as<int64_t>());
    item["addon_type"]      = row["addon_type"].as<std::string>();
    item["monthly_price"]   = row["monthly_price"].as<double>();
    item["start_date"]      = row["start_date"].as<std::string>();
    item["end_date"]        = row["end_date"].isNull()
                                  ? Json::nullValue
                                  : Json::Value(row["end_date"].as<std::string>());
    item["status"]          = row["status"].as<std::string>();

    std::string specsStr = row["specs"].as<std::string>();
    Json::Reader reader;
    Json::Value parsedSpecs;
    if (reader.parse(specsStr, parsedSpecs)) {
        item["specs"] = parsedSpecs;
    } else {
        item["specs"] = Json::objectValue;
    }

    item["version"]    = row["version"].as<int>();
    item["created_at"] = row["created_at"].as<std::string>();
    item["updated_at"] = row["updated_at"].as<std::string>();

    return item;
}

} // namespace idc
