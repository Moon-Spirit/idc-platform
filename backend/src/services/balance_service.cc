#include "balance_service.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Payment number generation (shared helper)
// ═══════════════════════════════════════════════════════════════════════════

std::string BalanceService::generatePaymentNo() {
    auto db = DbClient::getClient();
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::gmtime(&t);

    std::ostringstream prefix;
    prefix << "PAY" << std::put_time(tm, "%y%m%d") << "-";
    std::string datePrefix = prefix.str();

    auto result = db->execSqlSync(
        "SELECT COUNT(*) AS cnt FROM payments "
        "WHERE payment_no LIKE $1",
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
//  Get Balance
// ═══════════════════════════════════════════════════════════════════════════

Json::Value BalanceService::getBalance(int64_t distributorId) {
    auto db = DbClient::getClient();

    auto rows = db->execSqlSync(
        "SELECT id, name, balance, credit_limit, status "
        "FROM distributors WHERE id = $1",
        distributorId);

    if (rows.empty()) {
        return Json::nullValue;
    }

    auto row = rows[0];
    Json::Value result;
    result["distributor_id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
    result["distributor_name"] = row["name"].as<std::string>();
    result["balance"] = row["balance"].as<std::string>();
    result["credit_limit"] = row["credit_limit"].isNull()
        ? "0.00" : row["credit_limit"].as<std::string>();
    result["status"] = row["status"].as<int>() == 1 ? "active" : "disabled";

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Top-Up Balance
// ═══════════════════════════════════════════════════════════════════════════

Json::Value BalanceService::topUpBalance(int64_t distributorId,
                                          double amount,
                                          int64_t operatorId,
                                          const std::string& remark) {
    LOG_INFO << "[BalanceService] topUpBalance: distributor_id=" << distributorId
             << " amount=" << amount << " operator_id=" << operatorId;

    if (amount <= 0) {
        throw std::invalid_argument(
            "Top-up amount must be positive: " + std::to_string(amount));
    }

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // ── 1. Fetch and lock the distributor ───────────────────────────
        auto distRows = trans.execSqlSync(
            "SELECT id, name, balance FROM distributors "
            "WHERE id = $1 FOR UPDATE",
            distributorId);

        if (distRows.empty()) {
            throw std::invalid_argument("Distributor not found");
        }

        auto distRow = distRows[0];
        double currentBalance = distRow["balance"].as<double>();
        double newBalance = currentBalance + amount;

        std::ostringstream curStr, newStr, amtStr;
        curStr << std::fixed << std::setprecision(2) << currentBalance;
        newStr << std::fixed << std::setprecision(2) << newBalance;
        amtStr << std::fixed << std::setprecision(2) << amount;

        // ── 2. Update distributor balance ───────────────────────────────
        trans.execSqlSync(
            "UPDATE distributors SET balance = $1::DECIMAL(12,2), "
            "updated_at = NOW() WHERE id = $2",
            newStr.str(), distributorId);

        // ── 3. Create payment record ────────────────────────────────────
        std::string paymentNo = generatePaymentNo();
        trans.execSqlSync(R"(
            INSERT INTO payments
            (payment_no, invoice_id, distributor_id, method, amount,
             status, paid_at)
            VALUES ($1, NULL, $2, 'transfer', $3::DECIMAL(12,2),
                    'completed', NOW())
        )", paymentNo, distributorId, amtStr.str());

        // ── 4. Log operation ────────────────────────────────────────────
        Json::Value detail;
        detail["distributor_id"] = static_cast<Json::Int64>(distributorId);
        detail["payment_no"] = paymentNo;
        detail["amount"] = amtStr.str();
        detail["balance_before"] = curStr.str();
        detail["balance_after"] = newStr.str();
        detail["remark"] = remark;

        trans.execSqlSync(
            "INSERT INTO operation_logs "
            "(user_id, action, entity_type, entity_id, detail) "
            "VALUES ($1, 'balance.topup', 'distributor', $2, $3)",
            operatorId, distributorId, detail.toStyledString());

        // ── 5. Build result ─────────────────────────────────────────────
        result["distributor_id"] = static_cast<Json::Int64>(distributorId);
        result["distributor_name"] = distRow["name"].as<std::string>();
        result["balance_before"] = curStr.str();
        result["balance_after"] = newStr.str();
        result["topup_amount"] = amtStr.str();
        result["payment_no"] = paymentNo;

        LOG_INFO << "[BalanceService] Top-up completed: distributor_id="
                 << distributorId << " amount=" << amtStr.str()
                 << " balance: " << curStr.str() << " → " << newStr.str();
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Pay by Balance
// ═══════════════════════════════════════════════════════════════════════════

Json::Value BalanceService::payByBalance(int64_t invoiceId,
                                          int64_t distributorId,
                                          int64_t operatorId) {
    LOG_INFO << "[BalanceService] payByBalance: invoice_id=" << invoiceId
             << " distributor_id=" << distributorId;

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
        int64_t invDistId = invRow["distributor_id"].as<int64_t>();

        if (invDistId != distributorId) {
            throw std::invalid_argument("Invoice not found");
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
        std::string paymentNo = generatePaymentNo();
        trans.execSqlSync(R"(
            INSERT INTO payments
            (payment_no, invoice_id, distributor_id, method, amount,
             status, paid_at)
            VALUES ($1, $2, $3, 'balance', $4::DECIMAL(12,2),
                    'completed', NOW())
        )", paymentNo, invoiceId, distributorId, remainingStr.str());

        // ── 6. Log operation ────────────────────────────────────────────
        Json::Value detail;
        detail["invoice_id"] = static_cast<Json::Int64>(invoiceId);
        detail["payment_no"] = paymentNo;
        detail["amount"] = remainingStr.str();
        detail["method"] = "balance";
        detail["balance_before"] = balanceStr.str();
        detail["balance_after"] = newBalanceStr.str();

        trans.execSqlSync(
            "INSERT INTO operation_logs "
            "(user_id, action, entity_type, entity_id, detail) "
            "VALUES ($1, 'invoice.pay', 'invoice', $2, $3)",
            operatorId, invoiceId, detail.toStyledString());

        // ── 7. Build result ─────────────────────────────────────────────
        result["balance_before"] = balanceStr.str();
        result["balance_after"] = newBalanceStr.str();
        result["paid_amount"] = remainingStr.str();
        result["invoice_id"] = static_cast<Json::Int64>(invoiceId);
        result["payment_no"] = paymentNo;

        LOG_INFO << "[BalanceService] Invoice " << invoiceId
                 << " paid by balance: amount=" << remainingStr.str()
                 << " dist_id=" << distributorId;
    });

    return result;
}

} // namespace idc
