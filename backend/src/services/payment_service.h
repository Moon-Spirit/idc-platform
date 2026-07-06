#pragma once

#include <json/json.h>
#include <string>

namespace idc {

/// Payment processing service for online payments (Alipay / WeChat Pay).
///
/// Responsibilities:
///   - createPayment: create a pending payment record, call gateway precreate,
///                     return qr_code + trade_no for frontend display
///   - processPaymentCallback: verify gateway signature, check idempotency
///                             (7-day dedup window on transaction_id),
///                             compare callback amount vs invoice total,
///                             update invoice and payment status
///   - processRefund: stub for Phase 1 (not yet implemented)
///
/// All gateway interactions are stubbed in Phase 1 — no real HTTP calls or
/// cryptographic key loading. See AliPayUtil / WechatPayUtil for details.
class PaymentService {
public:
    // ── Payment creation ───────────────────────────────────────────────────

    /// Create an online payment for an invoice.
    ///
    /// Validates the invoice exists, is unpaid, and belongs to the specified
    /// distributor. Creates a `payments` record with status `pending`, then
    /// calls the appropriate gateway precreate stub to obtain a QR code URL.
    ///
    /// @param invoiceId      Invoice to pay
    /// @param distributorId  Distributor making the payment
    /// @param method         "alipay" or "wechat"
    /// @return JSON with:
    ///         { payment_id, payment_no, qr_code_url, trade_no, method, amount }
    /// @throws std::invalid_argument if invoice not found, not unpaid, or
    ///         method is unsupported.
    /// @throws std::runtime_error on DB failure.
    static Json::Value createPayment(int64_t invoiceId,
                                      int64_t distributorId,
                                      const std::string& method);

    // ── Callback processing ────────────────────────────────────────────────

    /// Process an asynchronous payment notification from Alipay or WeChat.
    ///
    /// Flow:
    ///   1. Find payment record by out_trade_no (payment_no)
    ///   2. **Idempotency check**: if transaction_id already exists in the
    ///      payments table with status=completed and created_at within 7 days,
    ///      skip processing (return success to avoid duplicate updates)
    ///   3. **Amount verification**: compare the callback's paid amount with
    ///      the invoice's total_amount; reject if they don't match
    ///   4. Verify gateway signature (stub in Phase 1 — always returns true)
    ///   5. Update payment: status=completed, transaction_id, gateway_response
    ///   6. Update invoice: status=paid, paid_amount=total, paid_at=NOW()
    ///   7. Log the callback data to payment.gateway_response JSONB field
    ///
    /// @param gateway  "alipay" or "wechat"
    /// @param data     Callback parameters (form-urlencoded decoded to JSON)
    /// @return JSON with { payment_id, invoice_id, transaction_id, amount }
    /// @throws std::invalid_argument on verification failure or invalid state.
    static Json::Value processPaymentCallback(const std::string& gateway,
                                               const Json::Value& data);

    // ── Refund (Phase 1 stub) ─────────────────────────────────────────────

    /// Process a refund for a completed payment.
    ///
    /// Phase 1: this is a stub that returns an error indicating refund is
    /// not yet implemented. The endpoint exists for future integration.
    ///
    /// @param paymentId  Payment record ID
    /// @return JSON with refund info (stub).
    /// @throws std::runtime_error always (not implemented in Phase 1).
    static Json::Value processRefund(int64_t paymentId);

    // ── Payment queries ────────────────────────────────────────────────────

    /// Get payment details by payment ID.
    ///
    /// @param paymentId  Payment record ID
    /// @return Payment JSON, or Json::nullValue if not found.
    static Json::Value getPaymentById(int64_t paymentId);

    /// List payments for an invoice (ordered by created_at DESC).
    static Json::Value listPaymentsByInvoice(int64_t invoiceId);

private:
    // ── Internal helpers ──────────────────────────────────────────────────

    /// Generate a payment number: PAY{YYMMDD}-{4-digit-seq}
    static std::string generatePaymentNo();

    /// Check if a transaction_id already exists within the dedup window (7 days).
    /// Returns true if the transaction_id was already processed successfully.
    static bool isTransactionDuplicate(const std::string& transactionId);
};

} // namespace idc
