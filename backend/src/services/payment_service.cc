#include "payment_service.h"
#include "services/invoice_service.h"
#include "utils/ali_pay.h"
#include "utils/wechat_pay.h"
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
//  Payment number generation
// ═══════════════════════════════════════════════════════════════════════════

std::string PaymentService::generatePaymentNo() {
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
//  Idempotency check: dedup window = 7 days
// ═══════════════════════════════════════════════════════════════════════════

bool PaymentService::isTransactionDuplicate(const std::string& transactionId) {
    if (transactionId.empty()) return false;

    auto db = DbClient::getClient();
    auto result = db->execSqlSync(R"(
        SELECT COUNT(*) AS cnt FROM payments
        WHERE transaction_id = $1
          AND status = 'completed'
          AND created_at >= NOW() - INTERVAL '7 days'
    )", transactionId);

    if (!result.empty() && !result[0]["cnt"].isNull()) {
        return result[0]["cnt"].as<int64_t>() > 0;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Create Payment
// ═══════════════════════════════════════════════════════════════════════════

Json::Value PaymentService::createPayment(int64_t invoiceId,
                                           int64_t distributorId,
                                           const std::string& method) {
    LOG_INFO << "[PaymentService] createPayment: invoice_id=" << invoiceId
             << " method=" << method;

    if (method != "alipay" && method != "wechat") {
        throw std::invalid_argument(
            "Unsupported payment method: '" + method + "'. Must be 'alipay' or 'wechat'");
    }

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // ── 1. Fetch and lock the invoice ────────────────────────────────
        auto invRows = trans.execSqlSync(
            "SELECT * FROM invoices WHERE id = $1 FOR UPDATE", invoiceId);

        if (invRows.empty()) {
            throw std::invalid_argument("Invoice not found");
        }

        auto invRow = invRows[0];
        std::string status = invRow["status"].as<std::string>();
        int64_t invDistId = invRow["distributor_id"].as<int64_t>();

        // Verify distributor ownership
        if (invDistId != distributorId) {
            throw std::invalid_argument("Invoice not found");
        }

        if (status != "unpaid") {
            throw std::invalid_argument(
                "Invoice cannot be paid: status is '" + status + "'");
        }

        double totalAmount = invRow["total_amount"].as<double>();
        if (totalAmount <= 0) {
            throw std::invalid_argument("Invoice total amount is zero or negative");
        }

        // ── 2. Generate payment number ───────────────────────────────────
        std::string paymentNo = generatePaymentNo();
        std::ostringstream amountStr;
        amountStr << std::fixed << std::setprecision(2) << totalAmount;

        // ── 3. Create pending payment record ─────────────────────────────
        auto payResult = trans.execSqlSync(R"(
            INSERT INTO payments
            (payment_no, invoice_id, distributor_id, method, amount,
             status, gateway_response)
            VALUES ($1, $2, $3, $4, $5::DECIMAL(12,2),
                    'pending', '{}'::JSONB)
            RETURNING id, created_at
        )", paymentNo, invoiceId, distributorId, method, amountStr.str());

        if (payResult.empty()) {
            throw std::runtime_error("Failed to create payment record");
        }

        int64_t paymentId = payResult[0]["id"].as<int64_t>();

        // ── 4. Call gateway precreate (stub) ─────────────────────────────
        Json::Value gatewayResult;
        if (method == "alipay") {
            gatewayResult = AliPayUtil::precreate(
                paymentNo, amountStr.str(), "Invoice " + paymentNo);
        } else {
            // wechat — amount in cents
            std::string totalFee = WechatPayUtil::yuanToCents(amountStr.str());
            gatewayResult = WechatPayUtil::precreate(
                paymentNo, totalFee, "Invoice " + paymentNo);
        }

        std::string tradeNo = gatewayResult.get("trade_no", "").asString();
        if (tradeNo.empty()) {
            tradeNo = gatewayResult.get("prepay_id", "").asString();
        }

        // ── 5. Update payment with gateway response ─────────────────────
        std::string gatewayStr = gatewayResult.toStyledString();
        trans.execSqlSync(R"(
            UPDATE payments
            SET gateway_response = $1::JSONB,
                transaction_id = $2,
                updated_at = NOW()
            WHERE id = $3
        )", gatewayStr, tradeNo, paymentId);

        // ── 6. Log operation ─────────────────────────────────────────────
        Json::Value detail;
        detail["invoice_id"] = static_cast<Json::Int64>(invoiceId);
        detail["payment_no"] = paymentNo;
        detail["amount"] = amountStr.str();
        detail["method"] = method;
        detail["gateway_trade_no"] = tradeNo;

        trans.execSqlSync(
            "INSERT INTO operation_logs "
            "(user_id, action, entity_type, entity_id, detail) "
            "VALUES (0, 'payment.create', 'payment', $1, $2)",
            paymentId, detail.toStyledString());

        // ── 7. Build result ──────────────────────────────────────────────
        result["payment_id"] = static_cast<Json::Int64>(paymentId);
        result["payment_no"] = paymentNo;
        result["method"] = method;
        result["amount"] = amountStr.str();
        result["qr_code_url"] = gatewayResult.get("qr_code_url", "").asString();
        result["trade_no"] = tradeNo;

        LOG_INFO << "[PaymentService] Created payment " << paymentNo
                 << " for invoice_id=" << invoiceId
                 << " method=" << method
                 << " amount=" << amountStr.str();
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Process Payment Callback
// ═══════════════════════════════════════════════════════════════════════════

Json::Value PaymentService::processPaymentCallback(const std::string& gateway,
                                                    const Json::Value& data) {
    LOG_INFO << "[PaymentService] processPaymentCallback: gateway=" << gateway;

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // ── 1. Extract key fields from callback ─────────────────────────
        // Alipay sends: out_trade_no, trade_no, trade_status, total_amount, sign
        // WeChat sends: out_trade_no, transaction_id, result_code, total_fee, sign
        std::string outTradeNo = data.get("out_trade_no", "").asString();
        std::string transactionId = data.get("trade_no",
            data.get("transaction_id", "")).asString();
        std::string tradeStatus = data.get("trade_status",
            data.get("result_code", "")).asString();
        std::string callbackAmount = data.get("total_amount",
            data.get("total_fee", "")).asString();

        if (outTradeNo.empty()) {
            throw std::invalid_argument(
                "Missing out_trade_no in callback data");
        }

        // ── 2. Find payment record by payment_no ────────────────────────
        auto payRows = trans.execSqlSync(R"(
            SELECT p.*, i.total_amount AS invoice_total, i.status AS invoice_status,
                   i.distributor_id
            FROM payments p
            JOIN invoices i ON i.id = p.invoice_id
            WHERE p.payment_no = $1
            FOR UPDATE OF p
        )", outTradeNo);

        if (payRows.empty()) {
            throw std::invalid_argument(
                "Payment not found: " + outTradeNo);
        }

        auto payRow = payRows[0];
        int64_t paymentId = payRow["id"].as<int64_t>();
        int64_t invoiceId = payRow["invoice_id"].as<int64_t>();
        std::string payStatus = payRow["status"].as<std::string>();
        double invoiceTotal = payRow["invoice_total"].as<double>();

        // ── 3. Idempotency check: skip if already completed ─────────────
        if (payStatus == "completed") {
            LOG_INFO << "[PaymentService] Duplicate callback: payment "
                     << outTradeNo << " already completed, skipping";
            result["payment_id"] = static_cast<Json::Int64>(paymentId);
            result["invoice_id"] = static_cast<Json::Int64>(invoiceId);
            result["transaction_id"] = transactionId;
            result["duplicate"] = true;
            return;
        }

        // ── 4. Check trade status ───────────────────────────────────────
        bool tradeSuccess = false;
        if (gateway == "alipay") {
            tradeSuccess = (tradeStatus == AliPayUtil::kTradeSuccess);
        } else {
            tradeSuccess = (tradeStatus == WechatPayUtil::kResultSuccess);
        }

        if (!tradeSuccess) {
            LOG_WARN << "[PaymentService] Callback trade not successful: "
                     << "gateway=" << gateway
                     << " status=" << tradeStatus;

            // Log the failed callback to gateway_response
            std::string dataStr = data.toStyledString();
            trans.execSqlSync(R"(
                UPDATE payments
                SET gateway_response = $1::JSONB,
                    status = 'failed',
                    updated_at = NOW()
                WHERE id = $2
            )", dataStr, paymentId);

            throw std::invalid_argument(
                "Payment not successful: status=" + tradeStatus);
        }

        // ── 5. Idempotency check: dedup window on transaction_id ────────
        if (!transactionId.empty() && isTransactionDuplicate(transactionId)) {
            LOG_INFO << "[PaymentService] Duplicate transaction_id: "
                     << transactionId << " already processed within 7 days";
            // Still record the callback but don't double-charge
            std::string dataStr = data.toStyledString();
            trans.execSqlSync(R"(
                UPDATE payments
                SET gateway_response = payments.gateway_response || $1::JSONB,
                    status = 'completed',
                    updated_at = NOW()
                WHERE id = $2
            )", dataStr, paymentId);

            result["payment_id"] = static_cast<Json::Int64>(paymentId);
            result["invoice_id"] = static_cast<Json::Int64>(invoiceId);
            result["transaction_id"] = transactionId;
            result["duplicate"] = true;
            return;
        }

        // ── 6. Amount verification ──────────────────────────────────────
        double callbackAmountVal = 0.0;
        if (gateway == "alipay") {
            // Alipay sends total_amount as decimal string: "12.34"
            callbackAmountVal = std::stod(callbackAmount);
        } else {
            // WeChat sends total_fee in cents: "1234" = 12.34 yuan
            callbackAmountVal = std::stod(callbackAmount) / 100.0;
        }

        double diff = std::abs(callbackAmountVal - invoiceTotal);
        if (diff > 0.01) { // Allow 1-cent tolerance for rounding
            LOG_ERROR << "[PaymentService] Amount mismatch! "
                      << "callback=" << callbackAmountVal
                      << " invoice_total=" << invoiceTotal
                      << " payment_no=" << outTradeNo;

            // Log the failed verification
            std::string dataStr = data.toStyledString();
            trans.execSqlSync(R"(
                UPDATE payments
                SET gateway_response = $1::JSONB,
                    status = 'failed',
                    updated_at = NOW()
                WHERE id = $2
            )", dataStr, paymentId);

            throw std::invalid_argument(
                "Payment amount mismatch: callback=" +
                std::to_string(callbackAmountVal) +
                " invoice=" + std::to_string(invoiceTotal));
        }

        // ── 7. Verify gateway signature (Phase 1 stub) ──────────────────
        bool sigValid = false;
        if (gateway == "alipay") {
            std::string sign = data.get("sign", "").asString();
            sigValid = AliPayUtil::verify(data, sign);
        } else {
            std::string sign = data.get("sign", "").asString();
            sigValid = WechatPayUtil::verify(data, sign);
        }

        if (!sigValid) {
            throw std::invalid_argument(
                "Signature verification failed for payment " + outTradeNo);
        }

        // ── 8. Update payment record ────────────────────────────────────
        std::ostringstream paidStr, invoiceTotalStr;
        paidStr << std::fixed << std::setprecision(2) << callbackAmountVal;
        invoiceTotalStr << std::fixed << std::setprecision(2) << invoiceTotal;

        std::string dataStr = data.toStyledString();
        trans.execSqlSync(R"(
            UPDATE payments
            SET status = 'completed',
                transaction_id = $1,
                gateway_response = $2::JSONB,
                paid_at = NOW(),
                updated_at = NOW()
            WHERE id = $3
        )", transactionId, dataStr, paymentId);

        // ── 9. Update invoice to paid ───────────────────────────────────
        trans.execSqlSync(R"(
            UPDATE invoices
            SET status = 'paid',
                paid_amount = total_amount,
                paid_at = NOW(),
                updated_at = NOW()
            WHERE id = $1
        )", invoiceId);

        // ── 10. Log operation ───────────────────────────────────────────
        Json::Value detail;
        detail["payment_id"] = static_cast<Json::Int64>(paymentId);
        detail["invoice_id"] = static_cast<Json::Int64>(invoiceId);
        detail["transaction_id"] = transactionId;
        detail["amount"] = paidStr.str();
        detail["method"] = gateway;
        detail["gateway_callback"] = data;

        trans.execSqlSync(
            "INSERT INTO operation_logs "
            "(user_id, action, entity_type, entity_id, detail) "
            "VALUES (0, 'payment.complete', 'payment', $1, $2)",
            paymentId, detail.toStyledString());

        // ── 11. Build result ───────────────────────────────────────────
        result["payment_id"] = static_cast<Json::Int64>(paymentId);
        result["invoice_id"] = static_cast<Json::Int64>(invoiceId);
        result["transaction_id"] = transactionId;
        result["amount"] = paidStr.str();
        result["payment_no"] = outTradeNo;
        result["duplicate"] = false;

        LOG_INFO << "[PaymentService] Payment completed: payment_no="
                 << outTradeNo << " transaction_id=" << transactionId
                 << " amount=" << paidStr.str();
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Refund (Phase 1 stub)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value PaymentService::processRefund(int64_t paymentId) {
    LOG_WARN << "[PaymentService] Refund called but not implemented in Phase 1"
             << " — payment_id=" << paymentId;

    // Phase 1 stub: always throws not-implemented
    // Future implementation:
    //   1. Find payment by ID, verify it is completed
    //   2. Call Alipay/WeChat refund API
    //   3. Update payment record with refund_id and status
    throw std::runtime_error(
        "Refund is not yet implemented (Phase 1). "
        "Payment ID: " + std::to_string(paymentId));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Get Payment by ID
// ═══════════════════════════════════════════════════════════════════════════

Json::Value PaymentService::getPaymentById(int64_t paymentId) {
    auto db = DbClient::getClient();

    auto rows = db->execSqlSync(R"(
        SELECT p.*, i.invoice_no, i.total_amount AS invoice_total,
               i.status AS invoice_status
        FROM payments p
        LEFT JOIN invoices i ON i.id = p.invoice_id
        WHERE p.id = $1
    )", paymentId);

    if (rows.empty()) {
        return Json::nullValue;
    }

    auto row = rows[0];
    Json::Value result;
    result["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
    result["payment_no"] = row["payment_no"].as<std::string>();
    result["invoice_id"] = static_cast<Json::Int64>(row["invoice_id"].as<int64_t>());
    result["invoice_no"] = row["invoice_no"].isNull()
        ? "" : row["invoice_no"].as<std::string>();
    result["distributor_id"] = static_cast<Json::Int64>(row["distributor_id"].as<int64_t>());
    result["method"] = row["method"].as<std::string>();
    result["amount"] = row["amount"].as<std::string>();
    result["status"] = row["status"].as<std::string>();
    result["transaction_id"] = row["transaction_id"].isNull()
        ? "" : row["transaction_id"].as<std::string>();

    // Parse gateway_response JSONB
    if (!row["gateway_response"].isNull()) {
        std::string gwStr = row["gateway_response"].as<std::string>();
        Json::Reader reader;
        Json::Value gwValue;
        if (reader.parse(gwStr, gwValue)) {
            result["gateway_response"] = gwValue;
        }
    }

    result["paid_at"] = row["paid_at"].isNull()
        ? Json::nullValue : Json::Value(row["paid_at"].as<std::string>());
    result["created_at"] = row["created_at"].as<std::string>();
    result["updated_at"] = row["updated_at"].as<std::string>();
    result["invoice_status"] = row["invoice_status"].isNull()
        ? "" : row["invoice_status"].as<std::string>();

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  List Payments by Invoice
// ═══════════════════════════════════════════════════════════════════════════

Json::Value PaymentService::listPaymentsByInvoice(int64_t invoiceId) {
    auto db = DbClient::getClient();

    auto rows = db->execSqlSync(R"(
        SELECT id, payment_no, method, amount, status,
               transaction_id, paid_at, created_at
        FROM payments
        WHERE invoice_id = $1
        ORDER BY created_at DESC
    )", invoiceId);

    Json::Value items(Json::arrayValue);
    for (const auto& row : rows) {
        Json::Value item;
        item["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
        item["payment_no"] = row["payment_no"].as<std::string>();
        item["method"] = row["method"].as<std::string>();
        item["amount"] = row["amount"].as<std::string>();
        item["status"] = row["status"].as<std::string>();
        item["transaction_id"] = row["transaction_id"].isNull()
            ? "" : row["transaction_id"].as<std::string>();
        item["paid_at"] = row["paid_at"].isNull()
            ? Json::nullValue : Json::Value(row["paid_at"].as<std::string>());
        item["created_at"] = row["created_at"].as<std::string>();
        items.append(item);
    }

    return items;
}

} // namespace idc
