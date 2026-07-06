#pragma once

#include <json/json.h>
#include <string>

namespace idc {

/// Balance/prepaid account management for distributors.
///
/// All balance updates use PostgreSQL row-level locking (SELECT ... FOR UPDATE)
/// to prevent concurrent deduction races (optimistic locking via the database).
///
/// Responsibilities:
///   - getBalance: query current balance for a distributor
///   - topUpBalance: increase distributor balance (for future admin feature)
///   - payByBalance: deduct from distributor balance and mark invoice paid
///
/// Note: The existing InvoiceService::payByBalance performs the same function
/// and is used by the PUT /api/v1/invoices/{id}/pay endpoint. This service
/// provides an alternative entry point that can be called from the
/// BalanceController or other services.
class BalanceService {
public:
    // ── Balance queries ────────────────────────────────────────────────────

    /// Get the current balance and credit limit for a distributor.
    ///
    /// @param distributorId  Distributor ID
    /// @return JSON with { distributor_id, balance, credit_limit },
    ///         or Json::nullValue if distributor not found.
    static Json::Value getBalance(int64_t distributorId);

    // ── Top-up ─────────────────────────────────────────────────────────────

    /// Add funds to a distributor's balance.
    ///
    /// Intended for future admin top-up feature. Creates a payment record
    /// with method='transfer' and status='completed'.
    ///
    /// Uses SELECT ... FOR UPDATE to prevent race conditions.
    ///
    /// @param distributorId  Distributor to credit
    /// @param amount         Amount to add (positive decimal)
    /// @param operatorId     Admin user performing the top-up
    /// @param remark         Optional reason/note
    /// @return JSON with { balance_before, balance_after, topup_amount }
    /// @throws std::invalid_argument if distributor not found or amount <= 0.
    static Json::Value topUpBalance(int64_t distributorId,
                                     double amount,
                                     int64_t operatorId,
                                     const std::string& remark = "");

    // ── Pay by balance ─────────────────────────────────────────────────────

    /// Pay an invoice by deducting from the distributor's prepaid balance.
    ///
    /// Validates invoice is unpaid and distributor has sufficient balance.
    /// Creates a payment record with method='balance' and status='completed'.
    /// Updates invoice to 'paid'.
    ///
    /// Uses SELECT ... FOR UPDATE on both invoice and distributor rows to
    /// prevent concurrent deductions.
    ///
    /// @param invoiceId      Invoice to pay
    /// @param distributorId  Distributor making the payment
    /// @param operatorId     User performing the operation
    /// @return JSON with { balance_before, balance_after, paid_amount, invoice_id }
    /// @throws std::invalid_argument on validation failure.
    static Json::Value payByBalance(int64_t invoiceId,
                                     int64_t distributorId,
                                     int64_t operatorId);

private:
    /// Generate a payment number: PAY{YYMMDD}-{4-digit-seq}
    static std::string generatePaymentNo();
};

} // namespace idc
