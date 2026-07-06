#include "invoice_service.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Date / time helpers
// ═══════════════════════════════════════════════════════════════════════════

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

static std::tm parseDate(const std::string& dateStr) {
    std::tm tm = {};
    std::istringstream ss(dateStr);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) {
        throw std::invalid_argument("Invalid date format: " + dateStr);
    }
    return tm;
}

static std::string addDays(const std::string& dateStr, int days) {
    auto tm = parseDate(dateStr);
    std::time_t t = std::mktime(&tm);
    t += days * 86400;
    std::tm* newTm = std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(newTm, "%Y-%m-%d");
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: get user's distributor_id
// ═══════════════════════════════════════════════════════════════════════════

int64_t InvoiceService::getUserDistributorId(int64_t userId) {
    auto db = DbClient::getClient();
    auto result = db->execSqlSync(
        "SELECT distributor_id FROM users WHERE id = $1", userId);
    if (result.empty()) return 0;
    auto val = result[0]["distributor_id"];
    return val.isNull() ? 0 : val.as<int64_t>();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Invoice Number Generation: INV{YYMMDD}-{4-digit-seq}
// ═══════════════════════════════════════════════════════════════════════════

std::string InvoiceService::generateInvoiceNo() {
    auto db = DbClient::getClient();

    // Get current date prefix
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::gmtime(&t);
    std::ostringstream prefix;
    prefix << "INV" << std::put_time(tm, "%y%m%d") << "-";
    std::string datePrefix = prefix.str();

    // Count existing invoices with today's prefix
    auto result = db->execSqlSync(
        "SELECT COUNT(*) AS cnt FROM invoices "
        "WHERE invoice_no LIKE $1",
        datePrefix + "%");

    int64_t seq = 1;
    if (!result.empty() && !result[0]["cnt"].isNull()) {
        seq = result[0]["cnt"].as<int64_t>() + 1;
    }

    std::ostringstream oss;
    oss << datePrefix << std::setw(4) << std::setfill('0') << seq;
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Due Date Calculation
// ═══════════════════════════════════════════════════════════════════════════

std::string InvoiceService::calculateDueDate(const std::string& periodEnd) {
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "SELECT value FROM system_config WHERE key = 'billing.payment_term_days'");
        if (!result.empty() && !result[0][0].isNull()) {
            int termDays = std::stoi(result[0][0].as<std::string>());
            return addDays(periodEnd, termDays);
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[InvoiceService] Failed to get payment_term_days: " << e.what();
    }
    // Default: 30 days from period end
    return addDays(periodEnd, 30);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Generate Invoice for a single distributor
// ═══════════════════════════════════════════════════════════════════════════

Json::Value InvoiceService::generateInvoice(int64_t distributorId,
                                             const std::string& periodStart,
                                             const std::string& periodEnd,
                                             const std::string& dueDate) {
    LOG_INFO << "[InvoiceService] Generating invoice for distributor_id="
             << distributorId << " period=" << periodStart << "~" << periodEnd;

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // ── 1. Find un-invoiced billing_records for this distributor ────
        auto brRows = trans.execSqlSync(R"(
            SELECT br.id, br.subscription_id, br.type,
                   br.period_start, br.period_end,
                   br.amount, br.quantity, br.unit_price, br.details,
                   s.sub_no, p.name AS product_name
            FROM billing_records br
            JOIN subscriptions s ON s.id = br.subscription_id
            LEFT JOIN products p ON p.id = s.product_id
            WHERE s.distributor_id = $1
              AND br.period_start >= $2::DATE
              AND br.period_end <= $3::DATE
              AND br.invoice_id IS NULL
            ORDER BY br.type, s.sub_no
        )", distributorId, periodStart, periodEnd);

        if (brRows.empty()) {
            LOG_INFO << "[InvoiceService] No billing records found for distributor_id="
                     << distributorId;
            return; // No records — result stays null
        }

        // ── 2. Aggregate amounts by type ────────────────────────────────
        double totalAmount = 0.0;
        double totalDiscount = 0.0;

        // Collect items and bandwidth usage snapshot data
        Json::Value items(Json::arrayValue);
        Json::Value bwSnapshot;
        bool hasBandwidth = false;

        std::vector<std::string> itemTypes; // ordered type list
        Json::Value typeMap; // type → { description, amount }

        for (const auto& row : brRows) {
            std::string type = row["type"].as<std::string>();
            double amount = row["amount"].as<double>();
            std::string subNo = row["sub_no"].isNull() ? "" : row["sub_no"].as<std::string>();
            std::string prodName = row["product_name"].isNull() ? "" : row["product_name"].as<std::string>();

            // Build item description
            std::string description;
            if (!prodName.empty()) {
                description = prodName;
            } else if (!subNo.empty()) {
                description = subNo;
            } else {
                description = type;
            }

            // Check for prorata discount
            double discountAmount = 0.0;
            if (!row["details"].isNull()) {
                std::string detailsStr = row["details"].as<std::string>();
                Json::Reader reader;
                Json::Value details;
                if (reader.parse(detailsStr, details)) {
                    if (details.isMember("prorata_applied") &&
                        details["prorata_applied"].asBool() &&
                        details.isMember("original_amount") &&
                        details.isMember("calculated_amount")) {
                        double orig = std::stod(details["original_amount"].asString());
                        double calc = std::stod(details["calculated_amount"].asString());
                        discountAmount = orig - calc;
                        if (discountAmount < 0) discountAmount = 0;
                    }
                }
            }

            totalAmount += amount;
            totalDiscount += discountAmount;

            // Accumulate bandwidth 95th snapshot
            if (type == "bandwidth_95" && !row["details"].isNull()) {
                std::string detailsStr = row["details"].as<std::string>();
                Json::Reader reader;
                Json::Value details;
                if (reader.parse(detailsStr, details)) {
                    if (!hasBandwidth) {
                        bwSnapshot = details;
                        hasBandwidth = true;
                    } else {
                        // Merge — append to array if needed
                        if (bwSnapshot.isObject() && details.isObject()) {
                            for (const auto& key : details.getMemberNames()) {
                                bwSnapshot[key] = details[key];
                            }
                        }
                    }
                }
            }

            // Add invoice item entry
            Json::Value item;
            item["billing_record_id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
            item["subscription_id"] = static_cast<Json::Int64>(row["subscription_id"].as<int64_t>());
            item["type"] = type;
            item["description"] = description;
            item["amount"] = std::to_string(amount);
            items.append(item);

            // Track by type for aggregation
            if (!typeMap.isMember(type)) {
                Json::Value typeEntry;
                typeEntry["description"] = description;
                typeEntry["amount"] = 0.0;
                typeMap[type] = typeEntry;
                itemTypes.push_back(type);
            }
        }

        // Round totals
        totalAmount = std::round(totalAmount * 100.0) / 100.0;
        totalDiscount = std::round(totalDiscount * 100.0) / 100.0;

        if (totalAmount <= 0) {
            LOG_INFO << "[InvoiceService] Total amount is zero for distributor_id="
                     << distributorId << " — skipping invoice";
            return;
        }

        // ── 3. Generate invoice number ──────────────────────────────────
        std::string invoiceNo = generateInvoiceNo();

        // ── 4. Determine due date ───────────────────────────────────────
        std::string effDueDate = dueDate.empty()
            ? calculateDueDate(periodEnd)
            : dueDate;

        // Format amounts as strings for DECIMAL columns
        std::ostringstream totalStr, discountStr;
        totalStr << std::fixed << std::setprecision(2) << totalAmount;
        discountStr << std::fixed << std::setprecision(2) << totalDiscount;

        // ── 5. Insert invoice ───────────────────────────────────────────
        std::string bwSnapshotStr = hasBandwidth ? bwSnapshot.toStyledString() : "{}";

        auto invResult = trans.execSqlSync(R"(
            INSERT INTO invoices
            (invoice_no, distributor_id, status,
             period_start, period_end, due_date,
             total_amount, paid_amount, discount_amount,
             bandwidth_usage_snapshot, notes)
            VALUES ($1, $2, 'unpaid',
                    $3::DATE, $4::DATE, $5::DATE,
                    $6::DECIMAL(12,2), 0.00, $7::DECIMAL(12,2),
                    $8::JSONB, $9)
            RETURNING id, created_at
        )", invoiceNo, distributorId,
           periodStart, periodEnd, effDueDate,
           totalStr.str(), discountStr.str(),
           bwSnapshotStr,
           "Auto-generated from billing records");

        if (invResult.empty()) {
            throw std::runtime_error("Failed to create invoice record");
        }

        int64_t invoiceId = invResult[0]["id"].as<int64_t>();
        std::string createdAt = invResult[0]["created_at"].as<std::string>();

        // ── 6. Insert invoice_items and link billing_records ────────────
        for (const auto& item : items) {
            int64_t billingRecordId = static_cast<int64_t>(item["billing_record_id"].asInt64());
            int64_t subId = item["subscription_id"].isNull()
                ? 0 : static_cast<int64_t>(item["subscription_id"].asInt64());
            std::string type = item["type"].asString();
            std::string desc = item["description"].asString();
            std::string amt = item["amount"].asString();

            trans.execSqlSync(R"(
                INSERT INTO invoice_items
                (invoice_id, type, description, subscription_id,
                 billing_record_id, amount)
                VALUES ($1, $2, $3, $4, $5, $6::DECIMAL(12,2))
            )", invoiceId, type, desc,
               subId > 0 ? subId : Json::nullValue,
               billingRecordId, amt);

            // Link billing_record to invoice
            trans.execSqlSync(
                "UPDATE billing_records SET invoice_id = $1 "
                "WHERE id = $2",
                invoiceId, billingRecordId);
        }

        // ── 7. Build result ─────────────────────────────────────────────
        result["id"] = static_cast<Json::Int64>(invoiceId);
        result["invoice_no"] = invoiceNo;
        result["distributor_id"] = static_cast<Json::Int64>(distributorId);
        result["status"] = "unpaid";
        result["period_start"] = periodStart;
        result["period_end"] = periodEnd;
        result["due_date"] = effDueDate;
        result["total_amount"] = totalStr.str();
        result["paid_amount"] = "0.00";
        result["discount_amount"] = discountStr.str();
        result["item_count"] = static_cast<int>(items.size());
        result["created_at"] = createdAt;

        LOG_INFO << "[InvoiceService] Created invoice " << invoiceNo
                 << " for distributor_id=" << distributorId
                 << " amount=" << totalStr.str();
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Generate invoices for ALL distributors
// ═══════════════════════════════════════════════════════════════════════════

Json::Value InvoiceService::generateAllInvoices(const std::string& periodStart,
                                                  const std::string& periodEnd) {
    Json::Value summary;
    summary["period_start"] = periodStart;
    summary["period_end"] = periodEnd;
    summary["run_at"] = nowIso8601();
    summary["invoices_generated"] = 0;
    summary["total_amount"] = "0.00";
    summary["distributors_invoiced"] = 0;
    summary["errors"] = Json::arrayValue;

    try {
        auto db = DbClient::getClient();

        // Find all distributors with un-invoiced billing records in the period
        auto distRows = db->execSqlSync(R"(
            SELECT DISTINCT s.distributor_id, d.name AS distributor_name
            FROM billing_records br
            JOIN subscriptions s ON s.id = br.subscription_id
            JOIN distributors d ON d.id = s.distributor_id
            WHERE br.period_start >= $1::DATE
              AND br.period_end <= $2::DATE
              AND br.invoice_id IS NULL
            ORDER BY s.distributor_id
        )", periodStart, periodEnd);

        int totalInvoices = 0;
        double totalAmount = 0.0;

        for (const auto& distRow : distRows) {
            try {
                int64_t distId = distRow["distributor_id"].as<int64_t>();
                auto invoice = generateInvoice(distId, periodStart, periodEnd);

                if (!invoice.isNull()) {
                    totalInvoices++;
                    totalAmount += std::stod(invoice["total_amount"].asString());
                }
            } catch (const std::exception& e) {
                Json::Value err;
                err["distributor_id"] = static_cast<Json::Int64>(
                    distRow["distributor_id"].as<int64_t>());
                err["error"] = e.what();
                summary["errors"].append(err);
                LOG_ERROR << "[InvoiceService] Failed to generate invoice for dist_id="
                          << distRow["distributor_id"].as<int64_t>()
                          << ": " << e.what();
            }
        }

        totalAmount = std::round(totalAmount * 100.0) / 100.0;
        std::ostringstream totalStr;
        totalStr << std::fixed << std::setprecision(2) << totalAmount;

        summary["invoices_generated"] = totalInvoices;
        summary["distributors_invoiced"] = static_cast<int>(distRows.size());
        summary["total_amount"] = totalStr.str();

        LOG_INFO << "[InvoiceService] Generated " << totalInvoices
                 << " invoices for " << distRows.size() << " distributors"
                 << ", total=" << totalStr.str();

    } catch (const std::exception& e) {
        LOG_ERROR << "[InvoiceService] generateAllInvoices error: " << e.what();
        Json::Value err;
        err["message"] = e.what();
        summary["errors"].append(err);
    }

    return summary;
}

// ═══════════════════════════════════════════════════════════════════════════
//  List Invoices (paginated, filtered)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value InvoiceService::listInvoices(int64_t userId, int64_t roleId,
                                          const std::string& status,
                                          int64_t distributorId,
                                          int year, int month,
                                          int page, int perPage) {
    auto db = DbClient::getClient();

    // Permission filter: dealers only see their own invoices
    bool isAdmin = (roleId == 1);
    if (!isAdmin && distributorId <= 0) {
        distributorId = getUserDistributorId(userId);
    }

    // Build WHERE conditions
    std::vector<std::string> conditions;
    if (distributorId > 0) {
        conditions.push_back("i.distributor_id = " + std::to_string(distributorId));
    }
    if (!status.empty()) {
        conditions.push_back("i.status = '" + status + "'");
    }
    if (year > 0) {
        if (month > 0) {
            std::string monthStr = (month < 10 ? "0" : "") + std::to_string(month);
            conditions.push_back(
                "(i.period_start >= '" + std::to_string(year) + "-" + monthStr + "-01'::DATE "
                "AND i.period_start < ('" + std::to_string(year) + "-" + monthStr + "-01'::DATE + INTERVAL '1 month'))");
        } else {
            conditions.push_back(
                "EXTRACT(YEAR FROM i.period_start) = " + std::to_string(year));
        }
    }

    std::string whereClause;
    if (!conditions.empty()) {
        whereClause = " WHERE ";
        for (size_t i = 0; i < conditions.size(); ++i) {
            if (i > 0) whereClause += " AND ";
            whereClause += conditions[i];
        }
    }

    // Count total
    auto countResult = db->execSqlSync(
        "SELECT COUNT(*) FROM invoices i" + whereClause);
    int64_t total = countResult.empty() ? 0 : countResult[0][0].as<int64_t>();

    // Fetch page with item count
    int offset = (page - 1) * perPage;
    std::string querySql =
        "SELECT i.id, i.invoice_no, i.distributor_id, "
        "d.name AS distributor_name, "
        "i.status, "
        "i.period_start::TEXT AS period_start, "
        "i.period_end::TEXT AS period_end, "
        "i.due_date::TEXT AS due_date, "
        "i.total_amount, i.paid_amount, i.discount_amount, "
        "(SELECT COUNT(*) FROM invoice_items ii WHERE ii.invoice_id = i.id) AS item_count, "
        "i.paid_at, i.created_at "
        "FROM invoices i "
        "LEFT JOIN distributors d ON d.id = i.distributor_id" +
        whereClause +
        " ORDER BY i.created_at DESC "
        "LIMIT " + std::to_string(perPage) + " OFFSET " + std::to_string(offset);

    auto rows = db->execSqlSync(querySql);

    Json::Value items(Json::arrayValue);
    for (const auto& row : rows) {
        Json::Value item;
        item["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
        item["invoice_no"] = row["invoice_no"].as<std::string>();
        item["distributor_id"] = static_cast<Json::Int64>(row["distributor_id"].as<int64_t>());
        item["distributor_name"] = row["distributor_name"].isNull()
            ? "" : row["distributor_name"].as<std::string>();
        item["status"] = row["status"].as<std::string>();
        item["period_start"] = row["period_start"].as<std::string>();
        item["period_end"] = row["period_end"].as<std::string>();
        item["due_date"] = row["due_date"].as<std::string>();
        item["total_amount"] = row["total_amount"].as<std::string>();
        item["paid_amount"] = row["paid_amount"].as<std::string>();
        item["discount_amount"] = row["discount_amount"].as<std::string>();
        item["item_count"] = static_cast<int>(row["item_count"].as<int64_t>());
        item["paid_at"] = row["paid_at"].isNull()
            ? Json::nullValue : Json::Value(row["paid_at"].as<std::string>());
        item["created_at"] = row["created_at"].as<std::string>();
        items.append(item);
    }

    Json::Value data;
    data["items"] = items;
    data["total"] = static_cast<Json::Int64>(total);
    data["page"] = page;
    data["per_page"] = perPage;
    return data;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Get Invoice Detail
// ═══════════════════════════════════════════════════════════════════════════

Json::Value InvoiceService::getInvoiceById(int64_t invoiceId, int64_t userId,
                                            int64_t roleId) {
    auto db = DbClient::getClient();

    // Fetch invoice
    auto invRows = db->execSqlSync(R"(
        SELECT i.*, d.name AS distributor_name
        FROM invoices i
        LEFT JOIN distributors d ON d.id = i.distributor_id
        WHERE i.id = $1
    )", invoiceId);

    if (invRows.empty()) {
        return Json::nullValue;
    }

    auto row = invRows[0];

    // Permission check: dealers only see their own invoices
    bool isAdmin = (roleId == 1);
    if (!isAdmin) {
        int64_t userDistId = getUserDistributorId(userId);
        int64_t invDistId = row["distributor_id"].as<int64_t>();
        if (userDistId != invDistId) {
            return Json::nullValue;
        }
    }

    Json::Value invoice;
    invoice["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
    invoice["invoice_no"] = row["invoice_no"].as<std::string>();
    invoice["distributor_id"] = static_cast<Json::Int64>(row["distributor_id"].as<int64_t>());
    invoice["distributor_name"] = row["distributor_name"].isNull()
        ? "" : row["distributor_name"].as<std::string>();
    invoice["status"] = row["status"].as<std::string>();
    invoice["period_start"] = row["period_start"].as<std::string>();
    invoice["period_end"] = row["period_end"].as<std::string>();
    invoice["due_date"] = row["due_date"].as<std::string>();
    invoice["total_amount"] = row["total_amount"].as<std::string>();
    invoice["paid_amount"] = row["paid_amount"].as<std::string>();
    invoice["discount_amount"] = row["discount_amount"].as<std::string>();

    // Parse bandwidth_usage_snapshot
    if (!row["bandwidth_usage_snapshot"].isNull()) {
        std::string bwStr = row["bandwidth_usage_snapshot"].as<std::string>();
        Json::Reader reader;
        Json::Value bw;
        if (reader.parse(bwStr, bw)) {
            invoice["bandwidth_usage_snapshot"] = bw;
        }
    }

    invoice["notes"] = row["notes"].isNull() ? "" : row["notes"].as<std::string>();
    invoice["paid_at"] = row["paid_at"].isNull()
        ? Json::nullValue : Json::Value(row["paid_at"].as<std::string>());
    invoice["created_at"] = row["created_at"].as<std::string>();
    invoice["updated_at"] = row["updated_at"].as<std::string>();

    // Fetch invoice items
    auto itemRows = db->execSqlSync(R"(
        SELECT ii.*, p.name AS product_name, s.sub_no
        FROM invoice_items ii
        LEFT JOIN subscriptions s ON s.id = ii.subscription_id
        LEFT JOIN products p ON p.id = s.product_id
        WHERE ii.invoice_id = $1
        ORDER BY ii.id
    )", invoiceId);

    Json::Value items(Json::arrayValue);
    for (const auto& itemRow : itemRows) {
        Json::Value item;
        item["id"] = static_cast<Json::Int64>(itemRow["id"].as<int64_t>());
        item["type"] = itemRow["type"].as<std::string>();
        item["description"] = itemRow["description"].isNull()
            ? "" : itemRow["description"].as<std::string>();
        item["subscription_id"] = itemRow["subscription_id"].isNull()
            ? Json::nullValue
            : Json::Value(static_cast<Json::Int64>(itemRow["subscription_id"].as<int64_t>()));
        item["billing_record_id"] = itemRow["billing_record_id"].isNull()
            ? Json::nullValue
            : Json::Value(static_cast<Json::Int64>(itemRow["billing_record_id"].as<int64_t>()));
        item["amount"] = itemRow["amount"].as<std::string>();
        items.append(item);
    }
    invoice["items"] = items;

    return invoice;
}

Json::Value InvoiceService::getInvoiceByNo(const std::string& invoiceNo) {
    auto db = DbClient::getClient();

    auto rows = db->execSqlSync(
        "SELECT id FROM invoices WHERE invoice_no = $1", invoiceNo);

    if (rows.empty()) {
        return Json::nullValue;
    }

    int64_t id = rows[0]["id"].as<int64_t>();
    // Call the detail method with admin credentials (no user context available)
    // Return basic info since this is used internally
    Json::Value result;
    result["id"] = static_cast<Json::Int64>(id);
    result["invoice_no"] = invoiceNo;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Pay by Balance
// ═══════════════════════════════════════════════════════════════════════════

Json::Value InvoiceService::payByBalance(int64_t invoiceId,
                                          int64_t userId,
                                          int64_t roleId) {
    bool isAdmin = (roleId == 1);

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // ── 1. Fetch and lock the invoice ───────────────────────────────
        auto invRows = trans.execSqlSync(
            "SELECT * FROM invoices WHERE id = $1 FOR UPDATE", invoiceId);

        if (invRows.empty()) {
            throw std::invalid_argument("Invoice not found");
        }

        auto invRow = invRows[0];
        std::string status = invRow["status"].as<std::string>();
        int64_t distributorId = invRow["distributor_id"].as<int64_t>();

        // Permission: admin can pay any invoice, dealers only their own
        if (!isAdmin) {
            int64_t userDistId = getUserDistributorId(userId);
            if (userDistId != distributorId) {
                throw std::invalid_argument("Invoice not found");
            }
        }

        if (status != "unpaid") {
            throw std::invalid_argument(
                "Invoice cannot be paid: status is '" + status + "'");
        }

        double totalAmount = invRow["total_amount"].as<double>();
        double paidAmount = invRow["paid_amount"].as<double>();
        double remaining = totalAmount - paidAmount;

        if (remaining <= 0) {
            throw std::invalid_argument("Invoice is already fully paid");
        }

        // ── 2. Fetch and lock the distributor ───────────────────────────
        auto distRows = trans.execSqlSync(
            "SELECT balance, credit_limit FROM distributors "
            "WHERE id = $1 FOR UPDATE", distributorId);

        if (distRows.empty()) {
            throw std::invalid_argument("Distributor not found");
        }

        double balance = distRows[0]["balance"].as<double>();

        if (balance < remaining) {
            throw std::invalid_argument(
                "Insufficient balance: available=" +
                std::to_string(balance) + ", required=" +
                std::to_string(remaining));
        }

        // ── 3. Deduct balance ───────────────────────────────────────────
        double newBalance = balance - remaining;
        std::ostringstream balanceStr, newBalanceStr, remainingStr;
        balanceStr << std::fixed << std::setprecision(2) << balance;
        newBalanceStr << std::fixed << std::setprecision(2) << newBalance;
        remainingStr << std::fixed << std::setprecision(2) << remaining;

        trans.execSqlSync(
            "UPDATE distributors SET balance = $1::DECIMAL(12,2), "
            "updated_at = NOW() WHERE id = $2",
            newBalanceStr.str(), distributorId);

        // ── 4. Update invoice ──────────────────────────────────────────
        trans.execSqlSync(
            "UPDATE invoices SET status = 'paid', "
            "paid_amount = total_amount, "
            "paid_at = NOW(), updated_at = NOW() "
            "WHERE id = $1",
            invoiceId);

        // ── 5. Create payment record ────────────────────────────────────
        // Payment number: PAY{YYMMDD}-{random}
        std::time_t t = std::time(nullptr);
        std::tm* tm = std::gmtime(&t);
        std::ostringstream payNo;
        payNo << "PAY" << std::put_time(tm, "%y%m%d")
              << "-" << std::setw(4) << std::setfill('0')
              << (std::rand() % 10000);

        trans.execSqlSync(R"(
            INSERT INTO payments
            (payment_no, invoice_id, distributor_id, method, amount,
             status, paid_at)
            VALUES ($1, $2, $3, 'balance', $4::DECIMAL(12,2),
                    'completed', NOW())
        )", payNo.str(), invoiceId, distributorId, remainingStr.str());

        // ── 6. Log operation ────────────────────────────────────────────
        Json::Value detail;
        detail["invoice_id"] = static_cast<Json::Int64>(invoiceId);
        detail["payment_no"] = payNo.str();
        detail["amount"] = remainingStr.str();
        detail["method"] = "balance";

        trans.execSqlSync(
            "INSERT INTO operation_logs "
            "(user_id, action, entity_type, entity_id, detail) "
            "VALUES ($1, 'invoice.pay', 'invoice', $2, $3)",
            userId, invoiceId, detail.toStyledString());

        // ── 7. Build result ─────────────────────────────────────────────
        result["balance_before"] = balanceStr.str();
        result["balance_after"] = newBalanceStr.str();
        result["paid_amount"] = remainingStr.str();
        result["invoice_id"] = static_cast<Json::Int64>(invoiceId);
        result["payment_no"] = payNo.str();

        LOG_INFO << "[InvoiceService] Invoice " << invoiceId
                 << " paid by balance: amount=" << remainingStr.str()
                 << " dist_id=" << distributorId;
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  PDF helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string InvoiceService::getPdfPath(const std::string& invoiceNo) {
    return "invoices/" + invoiceNo + ".pdf";
}

bool InvoiceService::hasPdfFile(const std::string& invoiceNo) {
    std::string path = getPdfPath(invoiceNo);
    std::ifstream f(path.c_str());
    return f.good();
}

} // namespace idc
