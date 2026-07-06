#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Invoice management API controller.
///
/// All endpoints require JWT authentication.
/// Dealers only see their own invoices (RBAC enforced in service layer).
///
/// Endpoints:
///   GET   /api/v1/invoices               — list invoices (paginated, filtered)
///   GET   /api/v1/invoices/{id}           — invoice detail with items
///   GET   /api/v1/invoices/{id}/pdf       — download invoice PDF
///   PUT   /api/v1/invoices/{id}/pay       — pay by balance
///
/// Admin-only endpoints:
///   POST  /api/v1/invoices/generate       — manually trigger invoice generation (admin: invoice:generate)
class InvoiceController
    : public drogon::HttpController<InvoiceController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(InvoiceController::listInvoices,
                      "/api/v1/invoices", drogon::Get,
                      "JWTFilter", "RBACFilter(invoice:list)");
        ADD_METHOD_TO(InvoiceController::getInvoice,
                      "/api/v1/invoices/{id}", drogon::Get,
                      "JWTFilter", "RBACFilter(invoice:read)");
        ADD_METHOD_TO(InvoiceController::downloadPdf,
                      "/api/v1/invoices/{id}/pdf", drogon::Get,
                      "JWTFilter", "RBACFilter(invoice:read)");
        ADD_METHOD_TO(InvoiceController::payByBalance,
                      "/api/v1/invoices/{id}/pay", drogon::Put,
                      "JWTFilter", "RBACFilter(invoice:pay)");
        ADD_METHOD_TO(InvoiceController::generateInvoices,
                      "/api/v1/invoices/generate", drogon::Post,
                      "JWTFilter", "RBACFilter(invoice:generate)");
    METHOD_LIST_END

    /// GET /api/v1/invoices
    /// Query: ?status=unpaid&distributor_id=1&year=2026&month=7&page=1&per_page=20
    static void listInvoices(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/invoices/{id}
    static void getInvoice(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// GET /api/v1/invoices/{id}/pdf
    /// Returns the PDF file for download (or generates it on the fly if not cached).
    static void downloadPdf(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// PUT /api/v1/invoices/{id}/pay
    /// Pay by distributor balance.
    static void payByBalance(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int64_t id);

    /// POST /api/v1/invoices/generate
    /// Manually trigger invoice generation for a given period (admin only).
    /// Body: { "period_start": "2026-08-01", "period_end": "2026-08-31", "distributor_id": null }
    /// If distributor_id is null, generates for ALL distributors.
    static void generateInvoices(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace idc
