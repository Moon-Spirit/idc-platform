#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace idc {

/// PDF generation service via Gotenberg Chromium renderer.
///
/// Gotenberg is available at http://gotenberg:3000 inside the Docker network.
/// Converts HTML invoice templates to PDF using the Chromium engine built into
/// the Gotenberg container. Chinese characters (CJK) are supported out of the
/// box because Gotenberg ships with a Chromium that includes CJK font support.
///
/// Usage:
///   auto pdfBytes = PdfService::generateInvoicePdf(invoiceJson);
///   // pdfBytes contains the raw PDF binary from Gotenberg
class PdfService {
public:
    /// Gotenberg service URL.
    static constexpr const char* kGotenbergUrl = "http://gotenberg:3000";

    /// PDF output directory (relative to app working directory).
    static constexpr const char* kPdfDir = "invoices/";

    /// Generate PDF for an invoice by building an HTML template and sending
    /// it to the Gotenberg Chromium conversion endpoint.
    ///
    /// @param invoiceJson  Invoice data JSON (from getInvoiceById or generateInvoice)
    /// @return Raw PDF bytes as a string
    /// @throws std::runtime_error if Gotenberg is unreachable or returns an error
    static std::vector<char> generateInvoicePdf(const Json::Value& invoiceJson);

    /// Save PDF bytes to disk at the configured path.
    ///
    /// @param invoiceNo  Invoice number (used as filename)
    /// @param pdfBytes   Raw PDF data
    /// @return Full file path where PDF was saved
    /// @throws std::runtime_error if file write fails
    static std::string savePdf(const std::string& invoiceNo,
                                const std::vector<char>& pdfBytes);

    /// Read a PDF file from disk.
    ///
    /// @param invoiceNo  Invoice number (used as filename)
    /// @return PDF bytes, or empty vector if file not found
    static std::vector<char> readPdf(const std::string& invoiceNo);

private:
    // ── HTML template generation ─────────────────────────────────────────

    /// Build the HTML invoice template string with embedded CSS.
    /// Includes: company logo placeholder, invoice_no, dates, items table,
    /// totals, QR code placeholder.
    static std::string buildInvoiceHtml(const Json::Value& invoiceJson);

    /// Build the items table HTML rows from invoice items JSON array.
    static std::string buildItemsTableRows(const Json::Value& items);
};

} // namespace idc
