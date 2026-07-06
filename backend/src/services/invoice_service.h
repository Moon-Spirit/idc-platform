#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace drogon {
namespace orm {
class Transaction;
} // namespace orm
} // namespace drogon

namespace idc {

/// Invoice generation and management service.
///
/// Responsibilities:
///   - Generate invoices by consolidating billing_records into invoice_items
///   - List / detail invoices with pagination and filters
///   - Pay invoices by deducting from distributor balance
///   - Invoice numbering: INV{YYMMDD}-{4-digit-seq}
///   - Handles empty invoices (no records = no invoice generated)
///   - Prorata discount amount calculation and storage
class InvoiceService {
public:
    // ── Invoice generation ────────────────────────────────────────────────

    /// Generate an invoice for a distributor for a given billing period.
    ///
    /// Finds all un-invoiced billing_records for the distributor in the period,
    /// consolidates them into invoice_items, creates the invoice record.
    /// Skips if no billing_records are found (empty invoice).
    ///
    /// @param distributorId  Distributor to invoice
    /// @param periodStart    Period start date (YYYY-MM-DD)
    /// @param periodEnd      Period end date (YYYY-MM-DD)
    /// @param dueDate        Due date (YYYY-MM-DD), defaults to periodEnd + payment_term
    /// @return Created invoice JSON, or Json::nullValue if no records
    /// @throws std::runtime_error on DB failure
    static Json::Value generateInvoice(int64_t distributorId,
                                        const std::string& periodStart,
                                        const std::string& periodEnd,
                                        const std::string& dueDate = "");

    /// Generate invoices for ALL distributors that have un-invoiced records
    /// in the given period. Used by the billing day cron.
    ///
    /// @param periodStart  Period start date (YYYY-MM-DD)
    /// @param periodEnd    Period end date (YYYY-MM-DD)
    /// @return Summary JSON with { distributors_invoiced, total_amount, errors }
    static Json::Value generateAllInvoices(const std::string& periodStart,
                                            const std::string& periodEnd);

    // ── Invoice queries ──────────────────────────────────────────────────

    /// Paginated invoice list with optional status, distributor, period filters.
    ///
    /// If `userId` is a dealer (role != admin), results are filtered to their
    /// distributor_id. Admin sees all (optionally filtered by ?distributor_id=).
    static Json::Value listInvoices(int64_t userId, int64_t roleId,
                                     const std::string& status,
                                     int64_t distributorId,
                                     int year, int month,
                                     int page, int perPage);

    /// Get invoice detail by id, including items.
    /// Returns null value if not found.
    static Json::Value getInvoiceById(int64_t invoiceId, int64_t userId,
                                       int64_t roleId);

    /// Get invoice by invoice_no.
    static Json::Value getInvoiceByNo(const std::string& invoiceNo);

    // ── Payment ──────────────────────────────────────────────────────────

    /// Pay an invoice by deducting from distributor balance.
    ///
    /// Validates invoice is unpaid and distributor has sufficient balance.
    /// Deducts from distributor.balance, updates invoice status.
    ///
    /// @param invoiceId      Invoice ID
    /// @param userId         User performing the payment
    /// @param roleId         Role ID for permission check
    /// @return JSON with { balance_before, balance_after, paid_amount }
    /// @throws std::invalid_argument if invoice not found, already paid, or insufficient balance
    static Json::Value payByBalance(int64_t invoiceId,
                                     int64_t userId,
                                     int64_t roleId);

    // ── Invoice PDF ──────────────────────────────────────────────────────

    /// Get the stored PDF file path for an invoice.
    static std::string getPdfPath(const std::string& invoiceNo);

    /// Check if an invoice has a PDF file on disk.
    static bool hasPdfFile(const std::string& invoiceNo);

private:
    // ── Internal helpers ──────────────────────────────────────────────────

    /// Generate the next invoice number in sequence: INV{YYMMDD}-{4-digit-seq}
    static std::string generateInvoiceNo();

    /// Get the distributor_id for a given user. Returns 0 if none or admin.
    static int64_t getUserDistributorId(int64_t userId);

    /// Calculate due date: period_end + payment_term from system_config
    static std::string calculateDueDate(const std::string& periodEnd);
};

} // namespace idc
