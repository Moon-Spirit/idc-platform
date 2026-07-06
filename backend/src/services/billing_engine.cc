#include "billing_engine.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════════════════

// Advisory lock IDs: deterministic 64-bit hash of the billing cycle name.
// PostgreSQL pg_advisory_xact_lock takes a single int8 (bigint).
// We derive a stable ID via std::hash<std::string>.
static constexpr const char* kLockKeyMonthly = "billing_monthly";
static constexpr const char* kLockKeyYearly  = "billing_yearly";

static std::string todayDateStr() {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d");
    return oss.str();
}

static std::string nowIso8601() {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/// Parse a "YYYY-MM-DD" string into a tm struct.
static std::tm parseDate(const std::string& dateStr) {
    std::tm tm = {};
    std::istringstream ss(dateStr);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) {
        throw std::invalid_argument("Invalid date format: " + dateStr);
    }
    return tm;
}

/// Return the last day of the month for a given date.
static std::string monthEndDate(const std::string& dateStr) {
    auto tm = parseDate(dateStr);
    tm.tm_year += (tm.tm_mon == 11) ? 1 : 0;
    tm.tm_mon  = (tm.tm_mon + 1) % 12;
    tm.tm_mday = 1;
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = -1;
    std::time_t next = std::mktime(&tm);
    std::tm* endTm = std::gmtime(&next);
    std::ostringstream oss;
    oss << std::put_time(endTm, "%Y-%m-%d");
    return oss.str();
}

/// Return the date N months from now.
static std::string addMonths(const std::string& dateStr, int months) {
    auto tm = parseDate(dateStr);
    tm.tm_mon += months;
    tm.tm_mday = 1; // avoid overflow (e.g. Jan 31 + 1 month → Feb 28)
    std::time_t t = std::mktime(&tm);
    std::tm* newTm = std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(newTm, "%Y-%m-%d");
    return oss.str();
}

/// Return the date 1 year from now.
static std::string addYear(const std::string& dateStr) {
    auto tm = parseDate(dateStr);
    tm.tm_year += 1;
    std::time_t t = std::mktime(&tm);
    std::tm* newTm = std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(newTm, "%Y-%m-%d");
    return oss.str();
}

/// Compute the number of days between two dates (inclusive count for billing).
/// Returns period_end - period_start + 1.
static int daysBetween(const std::string& startStr, const std::string& endStr) {
    auto startTm = parseDate(startStr);
    auto endTm   = parseDate(endStr);
    std::time_t startT = std::mktime(&startTm);
    std::time_t endT   = std::mktime(&endTm);
    double diffSec = std::difftime(endT, startT);
    int days = static_cast<int>(diffSec / 86400.0 + 0.5);
    return days + 1; // inclusive
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════════════

Json::Value BillingEngine::runMonthlyBilling() {
    LOG_INFO << "[BillingEngine] Starting monthly billing run";
    return runBillingForCycle("monthly");
}

Json::Value BillingEngine::runYearlyBilling() {
    LOG_INFO << "[BillingEngine] Starting yearly billing run";
    return runBillingForCycle("yearly");
}

Json::Value BillingEngine::getLastRunStatus() {
    Json::Value result;

    try {
        auto db = DbClient::getClient();
        auto rows = db->execSqlSync(
            "SELECT value FROM system_config WHERE key = 'billing.last_run_summary'");
        if (!rows.empty() && !rows[0][0].isNull()) {
            std::string summaryStr = rows[0][0].as<std::string>();
            Json::Reader reader;
            Json::Value summary;
            if (reader.parse(summaryStr, summary)) {
                result = summary;
            }
        }

        auto timeRows = db->execSqlSync(
            "SELECT value FROM system_config WHERE key = 'billing.last_run_at'");
        if (!timeRows.empty() && !timeRows[0][0].isNull()) {
            result["last_run_at"] = timeRows[0][0].as<std::string>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[BillingEngine] getLastRunStatus failed: " << e.what();
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Core billing logic
// ═══════════════════════════════════════════════════════════════════════════

Json::Value BillingEngine::runBillingForCycle(const std::string& billingCycle) {
    Json::Value summary;
    summary["billing_cycle"]   = billingCycle;
    summary["run_at"]          = nowIso8601();
    summary["subscriptions_billed"] = 0;
    summary["total_amount"]    = "0.00";
    summary["errors"]          = Json::arrayValue;
    summary["details"]         = Json::arrayValue;

    // ── 1. Attempt advisory lock ─────────────────────────────────────────
    if (!tryAdvisoryLock(billingCycle)) {
        Json::Value err;
        err["message"] = "Another billing run is already in progress for " + billingCycle;
        summary["errors"].append(err);
        LOG_WARN << "[BillingEngine] Advisory lock failed for " << billingCycle
                 << " — another run in progress";
        return summary;
    }

    // ── 2. Execute billing in REPEATABLE READ transaction ────────────────
    try {
        DbClient::transaction([&](drogon::orm::Transaction& trans) {
            // Set REPEATABLE READ isolation level for consistent snapshot
            trans.execSqlSync("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ");

            // Determine the billing date: today
            std::string billingDate = todayDateStr();
            bool isMonthly = (billingCycle == "monthly");

            // ── 3. Find active subscriptions needing billing ────────────
            //   - status = 'active'
            //   - billing_cycle = monthly/yearly
            //   - next_billing_date IS NOT NULL
            //   - next_billing_date <= end of current billing period
            //
            // For monthly: period starts at next_billing_date,
            //              ends at last day of that month.
            // For yearly:  period starts at next_billing_date,
            //              ends at same day next year - 1 day.
            //
            // Join with product_prices (via distributor's price template)
            // and order_items to get pricing info.
            std::string sql = R"(
                WITH sub_data AS (
                    SELECT
                        s.id,
                        s.sub_no,
                        s.product_id,
                        s.distributor_id,
                        s.billing_cycle,
                        s.start_date::TEXT AS start_date,
                        s.next_billing_date::TEXT AS next_billing_date,
                        COALESCE(pp.monthly_price, 0)::numeric AS monthly_price,
                        COALESCE(pp.yearly_price, 0)::numeric AS yearly_price,
                        COALESCE(oi.quantity, 1) AS quantity
                    FROM subscriptions s
                    LEFT JOIN order_items oi ON oi.id = s.order_item_id
                    LEFT JOIN distributors d ON d.id = s.distributor_id
                    LEFT JOIN product_prices pp
                        ON pp.product_id = s.product_id
                        AND pp.price_template_id = d.price_template_id
                    WHERE s.status = 'active'
                      AND s.billing_cycle = $1
                      AND s.next_billing_date IS NOT NULL
                      AND s.next_billing_date <= $2::DATE
                )
                SELECT * FROM sub_data
            )";

            // For monthly: billing date threshold is the end of the current month
            std::string periodEnd;
            std::string dateThreshold;

            if (isMonthly) {
                // Bill subscriptions whose next_billing_date is up to
                // the end of the current month
                dateThreshold = monthEndDate(billingDate);
            } else {
                // For yearly, use end of year from the billing date
                // next_billing_date is typically the anniversary date
                dateThreshold = billingDate;
            }

            // Use the billing cycle as the date threshold for simplicity
            // (each subscription's next_billing_date tells us when it's due)
            // Actually, let's pass billingDate and let the query use next_billing_date
            auto subRows = trans.execSqlSync(sql, billingCycle, billingDate);

            int billedCount = 0;
            double totalAmount = 0.0;

            for (const auto& row : subRows) {
                try {
                    int64_t subId           = row["id"].as<int64_t>();
                    std::string subNo       = row["sub_no"].as<std::string>();
                    std::string startDate   = row["start_date"].isNull() ? "" : row["start_date"].as<std::string>();
                    std::string nextBilling = row["next_billing_date"].as<std::string>();
                    double monthlyPrice     = row["monthly_price"].as<double>();
                    double yearlyPrice      = row["yearly_price"].as<double>();
                    int quantity            = static_cast<int>(row["quantity"].as<int64_t>());

                    // Determine the billing period
                    std::string periodStart = nextBilling;
                    std::string periodEnd;

                    if (isMonthly) {
                        periodEnd = monthEndDate(periodStart);
                    } else {
                        // Yearly: period ends the day before next year's anniversary
                        periodEnd = addYear(periodStart);
                    }

                    // ── 4. Idempotency check ────────────────────────────
                    if (isAlreadyBilled(billingCycle, subId, periodStart, periodEnd)) {
                        LOG_DEBUG << "[BillingEngine] Already billed: sub="
                                  << subId << " period=" << periodStart
                                  << " ~ " << periodEnd;
                        continue;
                    }

                    // ── 5. Calculate amount ─────────────────────────────
                    double fullPrice = isMonthly ? monthlyPrice : yearlyPrice;
                    if (fullPrice <= 0) {
                        Json::Value err;
                        err["subscription_id"] = static_cast<Json::Int64>(subId);
                        err["error"] = "Invalid price (zero or negative) for subscription";
                        summary["errors"].append(err);
                        LOG_ERROR << "[BillingEngine] Zero price for sub_id=" << subId;
                        continue;
                    }

                    double amount = fullPrice * quantity;

                    // Apply prorata if subscription started mid-cycle
                    double prorataAmount = 0;
                    double prorataFactor = 1.0;
                    int prorataDays = 0;
                    int daysInPeriod = 0;

                    if (!startDate.empty() && startDate > periodStart) {
                        prorataAmount = calculateProrata(
                            amount, startDate, periodStart, periodEnd);
                        prorataFactor = (amount > 0) ? (prorataAmount / amount) : 1.0;
                        // Calculate for details
                        daysInPeriod = daysBetween(periodStart, periodEnd);
                        auto startTm = parseDate(startDate);
                        auto periodEndTm = parseDate(periodEnd);
                        std::time_t startT = std::mktime(&startTm);
                        std::time_t endT = std::mktime(&periodEndTm);
                        double diffSec = std::difftime(endT, startT);
                        prorataDays = static_cast<int>(diffSec / 86400.0 + 0.5) + 1;

                        amount = prorataAmount;
                    }

                    // Round to 2 decimal places
                    amount = std::round(amount * 100.0) / 100.0;

                    // ── 6. Build details JSONB ──────────────────────────
                    Json::Value details;
                    details["subscription_id"] = static_cast<Json::Int64>(subId);
                    details["subscription_no"] = subNo;
                    details["billing_cycle"]   = billingCycle;
                    details["period_start"]    = periodStart;
                    details["period_end"]      = periodEnd;
                    details["full_price"]      = std::to_string(fullPrice);
                    details["quantity"]        = quantity;
                    details["original_amount"] = std::to_string(fullPrice * quantity);
                    details["prorata_applied"] = (prorataFactor < 1.0);
                    if (prorataFactor < 1.0) {
                        details["prorata_days"]           = prorataDays;
                        details["prorata_days_in_period"] = daysInPeriod;
                        details["prorata_factor"]         = prorataFactor;
                    }
                    details["calculated_amount"] = std::to_string(amount);

                    // Format amount as string for the DB (DECIMAL)
                    std::ostringstream amountStr;
                    amountStr << std::fixed << std::setprecision(2) << amount;

                    // ── 7. Insert billing_record ────────────────────────
                    trans.execSqlSync(
                        "INSERT INTO billing_records "
                        "(subscription_id, type, period_start, period_end, "
                        " amount, quantity, unit_price, details) "
                        "VALUES ($1, $2, $3::DATE, $4::DATE, "
                        " $5::DECIMAL(12,2), $6, $7, $8)",
                        subId,
                        billingCycle,           // type: 'monthly' or 'yearly'
                        periodStart,
                        periodEnd,
                        amountStr.str(),        // amount
                        quantity,               // quantity
                        fullPrice,              // unit_price
                        details.toStyledString() // details JSONB
                    );

                    // ── 8. Update next_billing_date ─────────────────────
                    std::string newNextBilling;
                    if (isMonthly) {
                        newNextBilling = addMonths(nextBilling, 1);
                    } else {
                        newNextBilling = addYear(nextBilling);
                    }

                    trans.execSqlSync(
                        "UPDATE subscriptions SET "
                        "next_billing_date = $1::DATE, "
                        "updated_at = NOW() "
                        "WHERE id = $2",
                        newNextBilling, subId);

                    // ── 9. Add to summary ───────────────────────────────
                    Json::Value detail;
                    detail["subscription_id"]   = static_cast<Json::Int64>(subId);
                    detail["subscription_no"]   = subNo;
                    detail["period_start"]      = periodStart;
                    detail["period_end"]        = periodEnd;
                    detail["amount"]            = amountStr.str();
                    detail["prorata_applied"]   = (prorataFactor < 1.0);
                    summary["details"].append(detail);

                    billedCount++;
                    totalAmount += amount;

                    LOG_DEBUG << "[BillingEngine] Billed sub_id=" << subId
                              << " amount=" << amountStr.str()
                              << " period=" << periodStart << "~" << periodEnd;

                } catch (const std::exception& e) {
                    Json::Value err;
                    err["subscription_id"] = static_cast<Json::Int64>(
                        row["id"].as<int64_t>());
                    err["error"] = e.what();
                    summary["errors"].append(err);

                    LOG_ERROR << "[BillingEngine] Failed for sub_id="
                              << row["id"].as<int64_t>()
                              << ": " << e.what();
                }
            }

            // ── 10. Populate summary ────────────────────────────────────
            std::ostringstream totalStr;
            totalStr << std::fixed << std::setprecision(2) << totalAmount;

            summary["subscriptions_billed"] = billedCount;
            summary["total_amount"]         = totalStr.str();
            summary["period_end"]           = dateThreshold;
        });

    } catch (const drogon::orm::DrogonDbException& e) {
        std::string errMsg = e.base().what();
        LOG_ERROR << "[BillingEngine] DB error in " << billingCycle
                  << " billing: " << errMsg;
        Json::Value err;
        err["message"] = "Database error: " + errMsg;
        summary["errors"].append(err);
    } catch (const std::exception& e) {
        LOG_ERROR << "[BillingEngine] Error in " << billingCycle
                  << " billing: " << e.what();
        Json::Value err;
        err["message"] = e.what();
        summary["errors"].append(err);
    }

    // ── 11. Log and persist status ──────────────────────────────────────
    logBillingRun(summary, billingCycle);
    persistRunStatus(summary, billingCycle, summary["run_at"].asString());

    LOG_INFO << "[BillingEngine] " << billingCycle
             << " billing complete: billed=" << summary["subscriptions_billed"].asInt()
             << " total=" << summary["total_amount"].asString();

    return summary;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Advisory Lock
// ═══════════════════════════════════════════════════════════════════════════

bool BillingEngine::tryAdvisoryLock(const std::string& billingCycle) {
    try {
        auto db = DbClient::getClient();
        int64_t lockId = static_cast<int64_t>(
            std::hash<std::string>{}(billingCycle));

        auto result = db->execSqlSync(
            "SELECT pg_try_advisory_xact_lock($1) AS locked",
            lockId);

        if (result.empty() || result[0]["locked"].isNull()) {
            return false;
        }

        return result[0]["locked"].as<bool>();
    } catch (const std::exception& e) {
        LOG_ERROR << "[BillingEngine] Advisory lock error: " << e.what();
        return false;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Prorata Calculation
// ═══════════════════════════════════════════════════════════════════════════

double BillingEngine::calculateProrata(double fullPrice,
                                        const std::string& startDate,
                                        const std::string& periodStart,
                                        const std::string& periodEnd) {
    if (startDate <= periodStart) {
        return fullPrice; // no prorata needed
    }

    // days_remaining = period_end - start_date + 1
    int daysRemaining = daysBetween(startDate, periodEnd);
    int daysInPeriod  = daysBetween(periodStart, periodEnd);

    if (daysInPeriod <= 0) {
        return fullPrice; // safeguard
    }

    double prorataFactor = static_cast<double>(daysRemaining) /
                            static_cast<double>(daysInPeriod);
    double amount = fullPrice * prorataFactor;

    LOG_DEBUG << "[BillingEngine] Prorata: days_remaining=" << daysRemaining
              << " days_in_period=" << daysInPeriod
              << " factor=" << prorataFactor
              << " amount=" << amount;

    return amount;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Idempotency Check
// ═══════════════════════════════════════════════════════════════════════════

bool BillingEngine::isAlreadyBilled(const std::string& billingCycle,
                                     int64_t subscriptionId,
                                     const std::string& periodStart,
                                     const std::string& periodEnd) {
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "SELECT COUNT(*) AS cnt FROM billing_records "
            "WHERE subscription_id = $1 "
            "  AND type = $2 "
            "  AND period_start = $3::DATE "
            "  AND period_end = $4::DATE",
            subscriptionId, billingCycle, periodStart, periodEnd);

        if (!result.empty()) {
            int64_t count = result[0]["cnt"].as<int64_t>();
            return count > 0;
        }
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR << "[BillingEngine] Idempotency check error: " << e.what();
        return false; // treat as not billed on error (will be caught by transaction)
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Logging & Persistence
// ═══════════════════════════════════════════════════════════════════════════

void BillingEngine::logBillingRun(const Json::Value& summary,
                                   const std::string& billingCycle) {
    try {
        auto db = DbClient::getClient();

        Json::Value detail;
        detail["billing_cycle"]          = billingCycle;
        detail["subscriptions_billed"]   = summary["subscriptions_billed"];
        detail["total_amount"]           = summary["total_amount"];
        detail["errors"]                 = summary["errors"];
        detail["run_at"]                 = summary["run_at"];

        db->execSqlSync(
            "INSERT INTO operation_logs "
            "(user_id, action, entity_type, entity_id, detail) "
            "VALUES (0, 'billing.run', 'billing', 0, $1)",
            detail.toStyledString());
    } catch (const std::exception& e) {
        LOG_ERROR << "[BillingEngine] Failed to log billing run: " << e.what();
    }
}

void BillingEngine::persistRunStatus(const Json::Value& summary,
                                      const std::string& billingCycle,
                                      const std::string& runAt) {
    try {
        auto db = DbClient::getClient();

        // Store last run time
        db->execSqlSync(
            "INSERT INTO system_config (key, value, description) "
            "VALUES ('billing.last_run_at', $1, '上次计费执行时间（ISO8601）') "
            "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value, "
            "updated_at = NOW()",
            runAt);

        // Store full summary
        Json::Value storedSummary;
        storedSummary["billing_cycle"]        = billingCycle;
        storedSummary["subscriptions_billed"] = summary["subscriptions_billed"];
        storedSummary["total_amount"]         = summary["total_amount"];
        storedSummary["error_count"]          = summary["errors"].size();

        db->execSqlSync(
            "INSERT INTO system_config (key, value, description) "
            "VALUES ('billing.last_run_summary', $1, '上次计费执行摘要JSON') "
            "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value, "
            "updated_at = NOW()",
            storedSummary.toStyledString());
    } catch (const std::exception& e) {
        LOG_ERROR << "[BillingEngine] Failed to persist run status: " << e.what();
    }
}

} // namespace idc
