#include "pdf_service.h"
#include "utils/logger.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <vector>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Generate Invoice PDF via Gotenberg
// ═══════════════════════════════════════════════════════════════════════════

std::vector<char> PdfService::generateInvoicePdf(const Json::Value& invoiceJson) {
    // ── 1. Build HTML from invoice data ────────────────────────────────
    std::string html = buildInvoiceHtml(invoiceJson);

    // ── 2. POST HTML to Gotenberg ──────────────────────────────────────
    auto client = drogon::HttpClient::newHttpClient(kGotenbergUrl);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/forms/chromium/convert/html");

    // Build multipart form body with the HTML file
    // Gotenberg expects a file field named "index.html" in the form
    std::string boundary = "----GotenbergBoundary" +
                           std::to_string(std::time(nullptr));
    std::string body;

    // Add the HTML file part
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"index.html\"; filename=\"index.html\"\r\n";
    body += "Content-Type: text/html; charset=utf-8\r\n\r\n";
    body += html;
    body += "\r\n";

    // Optional: paper size A4
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"paperWidth\"\r\n\r\n";
    body += "8.27\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"paperHeight\"\r\n\r\n";
    body += "11.69\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"marginTop\"\r\n\r\n";
    body += "0.5\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"marginBottom\"\r\n\r\n";
    body += "0.5\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"marginLeft\"\r\n\r\n";
    body += "0.5\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"marginRight\"\r\n\r\n";
    body += "0.5\r\n";

    // Close boundary
    body += "--" + boundary + "--\r\n";

    req->setBody(body);
    req->addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    // ── 3. Send request synchronously ─────────────────────────────────
    // Drogon HttpClient::sendRequest returns pair<ReqResult, HttpResponsePtr>
    auto [result, resp] = client->sendRequest(req, 30);

    if (result != drogon::ReqResult::Ok || !resp) {
        throw std::runtime_error(
            "Gotenberg request failed: no response received (result=" +
            std::to_string(static_cast<int>(result)) + ")");
    }

    if (resp->getStatusCode() != drogon::k200OK) {
        auto errBodyView = resp->getBody();
        std::string errBody(errBodyView.data(), errBodyView.size());
        if (errBody.length() > 200) errBody = errBody.substr(0, 200);
        throw std::runtime_error(
            "Gotenberg returned HTTP " +
            std::to_string(static_cast<int>(resp->getStatusCode())) +
            ": " + errBody);
    }

    // ── 4. Extract PDF binary from response ────────────────────────────
    auto bodyView = resp->getBody();
    std::string bodyStr(bodyView.data(), bodyView.size());
    std::vector<char> pdfBytes(bodyStr.begin(), bodyStr.end());

    LOG_INFO << "[PdfService] Generated PDF for invoice "
             << invoiceJson["invoice_no"].asString()
             << " (" << pdfBytes.size() << " bytes)";

    return pdfBytes;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Save PDF to disk
// ═══════════════════════════════════════════════════════════════════════════

std::string PdfService::savePdf(const std::string& invoiceNo,
                                 const std::vector<char>& pdfBytes) {
    // Ensure invoices directory exists
    ::mkdir(kPdfDir, 0755);

    std::string filePath = std::string(kPdfDir) + invoiceNo + ".pdf";

    std::ofstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open PDF file for writing: " + filePath);
    }

    file.write(pdfBytes.data(), pdfBytes.size());
    file.close();

    LOG_INFO << "[PdfService] Saved PDF to " << filePath
             << " (" << pdfBytes.size() << " bytes)";

    return filePath;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Read PDF from disk
// ═══════════════════════════════════════════════════════════════════════════

std::vector<char> PdfService::readPdf(const std::string& invoiceNo) {
    std::string filePath = std::string(kPdfDir) + invoiceNo + ".pdf";

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        LOG_WARN << "[PdfService] PDF not found: " << filePath;
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (file.read(buffer.data(), size)) {
        return buffer;
    }

    LOG_ERROR << "[PdfService] Failed to read PDF: " << filePath;
    return {};
}

// ═══════════════════════════════════════════════════════════════════════════
//  Build Invoice HTML Template
// ═══════════════════════════════════════════════════════════════════════════

std::string PdfService::buildInvoiceHtml(const Json::Value& inv) {
    std::string invoiceNo = inv.get("invoice_no", "").asString();
    std::string periodStart = inv.get("period_start", "").asString();
    std::string periodEnd = inv.get("period_end", "").asString();
    std::string dueDate = inv.get("due_date", "").asString();
    std::string status = inv.get("status", "").asString();
    std::string totalAmount = inv.get("total_amount", "0.00").asString();
    std::string paidAmount = inv.get("paid_amount", "0.00").asString();
    std::string discountAmount = inv.get("discount_amount", "0.00").asString();
    std::string distributorName = inv.get("distributor_name", "").asString();
    std::string createdAt = inv.get("created_at", "").asString();

    // If created_at is ISO8601, extract just the date part
    std::string createdDate = createdAt.substr(0, 10);

    // Determine status label in Chinese
    std::string statusLabel;
    if (status == "paid") statusLabel = "已支付";
    else if (status == "unpaid") statusLabel = "未支付";
    else if (status == "overdue") statusLabel = "已逾期";
    else if (status == "cancelled") statusLabel = "已作废";
    else statusLabel = status;

    // Build items table rows
    std::string itemsRows = buildItemsTableRows(inv["items"]);

    // Build the complete HTML document
    std::ostringstream html;
    html << R"(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<style>
    @page { margin: 0.5in; }
    body {
        font-family: "Noto Sans SC", "PingFang SC", "Microsoft YaHei", "SimHei", sans-serif;
        font-size: 12px;
        color: #333;
        line-height: 1.6;
        padding: 20px;
    }
    .header {
        display: flex;
        justify-content: space-between;
        align-items: flex-start;
        border-bottom: 2px solid #1a73e8;
        padding-bottom: 20px;
        margin-bottom: 20px;
    }
    .logo-area {
        width: 150px;
        height: 60px;
        background: #f0f4ff;
        border: 1px dashed #ccc;
        display: flex;
        align-items: center;
        justify-content: center;
        color: #999;
        font-size: 11px;
    }
    .title-area {
        text-align: right;
    }
    .title-area h1 {
        font-size: 22px;
        color: #1a73e8;
        margin: 0 0 5px 0;
    }
    .title-area .invoice-no {
        font-size: 14px;
        color: #666;
    }
    .info-section {
        display: flex;
        justify-content: space-between;
        margin-bottom: 20px;
    }
    .info-table {
        width: 48%;
    }
    .info-table td {
        padding: 3px 8px;
        font-size: 12px;
    }
    .info-table .label {
        color: #888;
        width: 80px;
    }
    .info-table .value {
        color: #333;
    }
    .status-badge {
        display: inline-block;
        padding: 3px 12px;
        border-radius: 4px;
        font-size: 12px;
        font-weight: bold;
    }
    .status-unpaid { background: #fff3e0; color: #e65100; }
    .status-paid { background: #e8f5e9; color: #2e7d32; }
    .status-overdue { background: #ffebee; color: #c62828; }
    table.items {
        width: 100%;
        border-collapse: collapse;
        margin-bottom: 20px;
    }
    table.items thead th {
        background: #1a73e8;
        color: white;
        padding: 10px 12px;
        text-align: left;
        font-size: 12px;
    }
    table.items tbody td {
        padding: 10px 12px;
        border-bottom: 1px solid #e0e0e0;
        font-size: 12px;
    }
    table.items tbody tr:nth-child(even) {
        background: #f8f9fa;
    }
    table.items .amount {
        text-align: right;
        font-family: "Courier New", monospace;
    }
    .totals {
        width: 300px;
        margin-left: auto;
        margin-top: 10px;
    }
    .totals td {
        padding: 5px 12px;
        font-size: 12px;
    }
    .totals .label {
        color: #888;
        text-align: right;
    }
    .totals .amount {
        text-align: right;
        font-family: "Courier New", monospace;
    }
    .totals .grand-total {
        font-size: 16px;
        font-weight: bold;
        color: #1a73e8;
    }
    .totals .grand-total td {
        border-top: 2px solid #333;
        padding-top: 8px;
    }
    .footer {
        margin-top: 40px;
        padding-top: 20px;
        border-top: 1px solid #e0e0e0;
        text-align: center;
        font-size: 11px;
        color: #999;
    }
    .qr-placeholder {
        display: inline-block;
        width: 80px;
        height: 80px;
        background: #f5f5f5;
        border: 1px dashed #ccc;
        text-align: center;
        line-height: 80px;
        font-size: 10px;
        color: #999;
    }
</style>
</head>
<body>

<div class="header">
    <div class="logo-area">[公司 Logo]</div>
    <div class="title-area">
        <h1>账单 / INVOICE</h1>
        <div class="invoice-no">编号: )" << invoiceNo << R"(</div>
    </div>
</div>

<div class="info-section">
    <table class="info-table">
        <tr><td class="label">客户名称</td><td class="value">)" << distributorName << R"(</td></tr>
        <tr><td class="label">账单期间</td><td class="value">)" << periodStart << " ~ " << periodEnd << R"(</td></tr>
        <tr><td class="label">账单日期</td><td class="value">)" << createdDate << R"(</td></tr>
    </table>
    <table class="info-table">
        <tr><td class="label">到期日期</td><td class="value">)" << dueDate << R"(</td></tr>
        <tr><td class="label">账单状态</td><td class="value"><span class="status-badge status-)" << status << R"(">)" << statusLabel << R"(</span></td></tr>
    </table>
</div>

<h3 style="color:#333; margin-bottom:10px;">费用明细</h3>
<table class="items">
    <thead>
        <tr>
            <th style="width:15%;">类型</th>
            <th>描述</th>
            <th style="width:20%;" class="amount">金额</th>
        </tr>
    </thead>
    <tbody>
)" << itemsRows << R"(
    </tbody>
</table>

<table class="totals">
    <tr>
        <td class="label">合计金额</td>
        <td class="amount">¥ )" << totalAmount << R"(</td>
    </tr>
    <tr>
        <td class="label">折扣金额</td>
        <td class="amount">¥ )" << discountAmount << R"(</td>
    </tr>
    <tr>
        <td class="label">已付金额</td>
        <td class="amount">¥ )" << paidAmount << R"(</td>
    </tr>
    <tr class="grand-total">
        <td class="label">应付金额</td>
        <td class="amount">¥ )" << totalAmount << R"(</td>
    </tr>
</table>

<div class="footer">
    <p>IDC Platform - 自动生成的账单</p>
    <p>如有疑问请联系客服 support@idcplatform.com | 电话: 400-000-0000</p>
    <div class="qr-placeholder">[二维码]</div>
</div>

</body>
</html>)";

    return html.str();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Build Items Table Rows
// ═══════════════════════════════════════════════════════════════════════════

std::string PdfService::buildItemsTableRows(const Json::Value& items) {
    if (items.isNull() || !items.isArray()) {
        return R"(<tr><td colspan="3" style="text-align:center;color:#999;">无明细项</td></tr>)";
    }

    std::ostringstream rows;
    for (const auto& item : items) {
        std::string type = item.get("type", "").asString();
        std::string description = item.get("description", "").asString();
        std::string amount = item.get("amount", "0.00").asString();

        // Map type to Chinese label
        std::string typeLabel;
        if (type == "monthly" || type == "yearly") typeLabel = "订阅费用";
        else if (type == "bandwidth_95") typeLabel = "95带宽";
        else if (type == "hourly") typeLabel = "按量计费";
        else if (type == "addon") typeLabel = "附加服务";
        else if (type == "setup") typeLabel = "设置费";
        else typeLabel = type;

        rows << R"(<tr><td>)" << typeLabel
             << R"(</td><td>)" << description
             << R"(</td><td class="amount">¥ )" << amount << R"(</td></tr>
)";
    }

    return rows.str();
}

} // namespace idc
