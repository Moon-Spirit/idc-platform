#include "invoice_controller.h"
#include "services/invoice_service.h"
#include "services/pdf_service.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: extract JWT attributes from request
// ═══════════════════════════════════════════════════════════════════════════

namespace {
    int64_t getUserId(const drogon::HttpRequestPtr& req) {
        return req->getAttributes()->get<int64_t>("user_id");
    }

    int64_t getRoleId(const drogon::HttpRequestPtr& req) {
        return req->getAttributes()->get<int64_t>("role_id");
    }

    std::string getUsername(const drogon::HttpRequestPtr& req) {
        return req->getAttributes()->get<std::string>("username");
    }
} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/invoices
// ═══════════════════════════════════════════════════════════════════════════

void InvoiceController::listInvoices(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);

        // Query parameters
        int page = req->getOptionalParameter<int>("page").value_or(1);
        int perPage = req->getOptionalParameter<int>("per_page").value_or(20);
        std::string status = req->getOptionalParameter<std::string>("status")
                                .value_or("");
        int64_t distributorId = static_cast<int64_t>(
            req->getOptionalParameter<int64_t>("distributor_id").value_or(-1));
        int year = req->getOptionalParameter<int>("year").value_or(0);
        int month = req->getOptionalParameter<int>("month").value_or(0);

        // Clamp pagination
        if (page < 1) page = 1;
        if (perPage < 1) perPage = 20;
        if (perPage > 100) perPage = 100;

        auto data = InvoiceService::listInvoices(
            userId, roleId, status, distributorId,
            year, month, page, perPage);

        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Invoice] listInvoices error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/invoices/{id}
// ═══════════════════════════════════════════════════════════════════════════

void InvoiceController::getInvoice(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);

        auto invoice = InvoiceService::getInvoiceById(id, userId, roleId);
        if (invoice.isNull()) {
            callback(JsonResponse::notFound("Invoice not found"));
            return;
        }

        callback(JsonResponse::ok(invoice));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Invoice] getInvoice error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/invoices/{id}/pdf
// ═══════════════════════════════════════════════════════════════════════════

void InvoiceController::downloadPdf(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);

        auto invoice = InvoiceService::getInvoiceById(id, userId, roleId);
        if (invoice.isNull()) {
            callback(JsonResponse::notFound("Invoice not found"));
            return;
        }

        std::string invoiceNo = invoice["invoice_no"].asString();

        // Check if PDF already exists on disk
        std::vector<char> pdfBytes;
        if (InvoiceService::hasPdfFile(invoiceNo)) {
            pdfBytes = PdfService::readPdf(invoiceNo);
        }

        // If not cached, generate via Gotenberg and save
        if (pdfBytes.empty()) {
            LOG_IDC_INFO(req, "[Invoice] Generating PDF for " << invoiceNo);
            pdfBytes = PdfService::generateInvoicePdf(invoice);
            PdfService::savePdf(invoiceNo, pdfBytes);
        }

        if (pdfBytes.empty()) {
            callback(JsonResponse::serverError("Failed to generate PDF"));
            return;
        }

        // Return PDF as download response
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k200OK);
        resp->setContentTypeCode(drogon::CT_APPLICATION_OCTET_STREAM);
        resp->addHeader("Content-Disposition",
                        "attachment; filename=\"" + invoiceNo + ".pdf\"");
        resp->addHeader("Content-Length",
                        std::to_string(pdfBytes.size()));

        std::string bodyStr(pdfBytes.begin(), pdfBytes.end());
        resp->setBody(bodyStr);

        callback(resp);
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Invoice] downloadPdf error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/invoices/{id}/pay
// ═══════════════════════════════════════════════════════════════════════════

void InvoiceController::payByBalance(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto result = InvoiceService::payByBalance(
            id, getUserId(req), getRoleId(req));

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Invoice] payByBalance error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/invoices/generate
// ═══════════════════════════════════════════════════════════════════════════

void InvoiceController::generateInvoices(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("period_start") || !body->isMember("period_end")) {
            callback(JsonResponse::error(400,
                "Missing required fields: period_start, period_end"));
            return;
        }

        std::string periodStart = (*body)["period_start"].asString();
        std::string periodEnd = (*body)["period_end"].asString();
        std::string dueDate = body->get("due_date", "").asString();

        Json::Value result;

        // If distributor_id specified, generate for one distributor
        if (body->isMember("distributor_id") && !(*body)["distributor_id"].isNull()) {
            int64_t distributorId = static_cast<int64_t>(
                (*body)["distributor_id"].asInt64());
            auto invoice = InvoiceService::generateInvoice(
                distributorId, periodStart, periodEnd, dueDate);
            if (invoice.isNull()) {
                result["message"] = "No billing records found for distributor";
                result["invoice"] = Json::nullValue;
            } else {
                result["message"] = "Invoice generated";
                result["invoice"] = invoice;

                // Automatically generate PDF
                try {
                    auto invoiceDetail = InvoiceService::getInvoiceById(
                        invoice["id"].asInt64(), 0, 1);
                    if (!invoiceDetail.isNull()) {
                        auto pdfBytes = PdfService::generateInvoicePdf(invoiceDetail);
                        PdfService::savePdf(
                            invoice["invoice_no"].asString(), pdfBytes);
                        result["pdf_generated"] = true;
                    }
                } catch (const std::exception& pdfErr) {
                    LOG_IDC_WARN(req, "[Invoice] PDF generation failed: " << pdfErr.what());
                    result["pdf_generated"] = false;
                    result["pdf_error"] = pdfErr.what();
                }
            }
        } else {
            // Generate for ALL distributors
            result = InvoiceService::generateAllInvoices(periodStart, periodEnd);
        }

        callback(JsonResponse::ok(result));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Invoice] generateInvoices error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
