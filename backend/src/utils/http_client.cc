#include "http_client.h"
#include "logger.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>

#include <chrono>
#include <thread>

namespace idc {

// Out-of-class definition for ODR-used static constexpr array member
constexpr int HttpClient::kBackoffMs[];

// ============================================================================
// ── Constructor ───────────────────────────────────────────────────────────────
// ============================================================================

HttpClient::HttpClient(std::string baseUrl,
                       std::string apiKey,
                       std::string apiSecret,
                       bool mockMode)
    : apiKey_(std::move(apiKey))
    , apiSecret_(std::move(apiSecret))
    , mockMode_(mockMode) {
    // Drogon's HttpClient only uses scheme+host+port. Strip any path.
    // The full API path (e.g. /api/v1/products) is set on individual requests.
    size_t schemeEnd = baseUrl.find("://");
    if (schemeEnd == std::string::npos) {
        baseUrl = "https://" + baseUrl;
        schemeEnd = baseUrl.find("://");
    }
    size_t pathStart = baseUrl.find('/', schemeEnd + 3);
    if (pathStart != std::string::npos) {
        hostOnly_ = baseUrl.substr(0, pathStart);
    } else {
        hostOnly_ = baseUrl;
    }
    LOG_DEBUG << "[HttpClient] Base=" << baseUrl << " Host=" << hostOnly_;
}

// ============================================================================
// ── Public HTTP methods ───────────────────────────────────────────────────────
// ============================================================================

HttpResult HttpClient::get(
    const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& queryParams) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);

    // Build query string
    std::string queryString;
    if (!queryParams.empty()) {
        for (size_t i = 0; i < queryParams.size(); ++i) {
            if (i > 0) queryString += "&";
            queryString += queryParams[i].first + "=" +
                           drogon::utils::urlEncode(queryParams[i].second);
        }
    }
    if (!queryString.empty()) {
        req->setPath(path + "?" + queryString);
    } else {
        req->setPath(path);
    }
    applyAuth(req);
    return executeWithRetry(req);
}

HttpResult HttpClient::post(const std::string& path, const Json::Value& body) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath(path);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    req->setBody(Json::FastWriter().write(body));
    applyAuth(req);
    return executeWithRetry(req);
}

HttpResult HttpClient::put(const std::string& path, const Json::Value& body) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Put);
    req->setPath(path);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    req->setBody(Json::FastWriter().write(body));
    applyAuth(req);
    return executeWithRetry(req);
}

HttpResult HttpClient::del(const std::string& path) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Delete);
    req->setPath(path);
    applyAuth(req);
    return executeWithRetry(req);
}

// ============================================================================
// ── Mock mode ────────────────────────────────────────────────────────────────
// ============================================================================

void HttpClient::setMockResponse(const std::string& method,
                                  const std::string& path,
                                  Json::Value response,
                                  int httpStatus) {
    std::string key = method + " " + path;
    mockResponses_[key] = { std::move(response), httpStatus };
    LOG_INFO << "[HttpClient] Mock response set for " << key;
}

// ============================================================================
// ── Private helpers ───────────────────────────────────────────────────────────
// ============================================================================

void HttpClient::applyAuth(drogon::HttpRequestPtr req) const {
    req->addHeader("API-Key", apiKey_);
    if (!apiSecret_.empty()) {
        req->addHeader("API-Secret", apiSecret_);
    }
    req->addHeader("Content-Type", "application/json");
    req->addHeader("Accept", "application/json");
}

HttpResult HttpClient::executeSingle(const drogon::HttpRequestPtr& req) {
    HttpResult result;

    // Check mock mode first
    if (mockMode_) {
        auto mock = tryMockResponse(req);
        if (mock.has_value()) {
            return *mock;
        }
    }

    try {
        auto client = drogon::HttpClient::newHttpClient(
            hostOnly_, kConnectTimeoutSec, kReadTimeoutSec);

        auto [reqResult, response] = client->sendRequest(req, kReadTimeoutSec);

        if (reqResult != drogon::ReqResult::Ok) {
            result.error = "Request failed: " + std::to_string(static_cast<int>(reqResult));
            LOG_WARN << "[HttpClient] sendRequest error: " << static_cast<int>(reqResult);
            return result;
        }

        result.httpStatus = response->getStatusCode();
        result.rawBody    = response->body();
        result.ok         = (result.httpStatus >= 200 && result.httpStatus < 300);

        // Try to parse JSON body
        if (!result.rawBody.empty()) {
            Json::Reader reader;
            Json::Value parsed;
            if (reader.parse(result.rawBody, parsed)) {
                result.body = parsed;
            } else {
                // Non-JSON response (e.g. plain text error)
                result.body = Json::nullValue;
            }
        }

        if (!result.ok) {
            result.error = "HTTP " + std::to_string(result.httpStatus) + ": " +
                           result.rawBody.substr(0, 512); // truncate for logs
            LOG_WARN << "[HttpClient] Request failed: "
                     << req->methodString() << " " << req->path()
                     << " → " << result.httpStatus;
        }
    } catch (const std::exception& e) {
        result.error = "Connection error: ";
        result.error += e.what();
        LOG_WARN << "[HttpClient] Exception: " << result.error;
    }

    return result;
}

HttpResult HttpClient::executeWithRetry(const drogon::HttpRequestPtr& req) {
    for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
        auto result = executeSingle(req);
        result.attemptCount = attempt;

        // Success — return immediately
        if (result.ok) {
            LOG_DEBUG << "[HttpClient] " << req->methodString()
                      << " " << req->path() << " succeeded (attempt "
                      << attempt << "/" << kMaxRetries << ")";
            return result;
        }

        // Do NOT retry on 4xx client errors (400-499, except 429)
        if (result.httpStatus >= 400 && result.httpStatus < 500 &&
            result.httpStatus != 429) {
            LOG_WARN << "[HttpClient] " << req->methodString()
                     << " " << req->path() << " → " << result.httpStatus
                     << " (not retrying — client error)";
            return result;
        }

        // Last attempt — return the failure
        if (attempt >= kMaxRetries) {
            LOG_ERROR << "[HttpClient] " << req->methodString()
                      << " " << req->path() << " failed after "
                      << kMaxRetries << " attempts: " << result.error;
            return result;
        }

        // Exponential backoff
        int delayMs = kBackoffMs[attempt - 1];
        LOG_INFO << "[HttpClient] Retrying " << req->methodString()
                 << " " << req->path() << " in " << delayMs << "ms "
                 << "(attempt " << (attempt + 1) << "/" << kMaxRetries << ")";
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    // Should never reach here
    HttpResult fallback;
    fallback.error = "Unknown retry error";
    fallback.attemptCount = kMaxRetries;
    return fallback;
}

std::optional<HttpResult> HttpClient::tryMockResponse(
    const drogon::HttpRequestPtr& req) {
    std::string key = mockKey(req);
    auto it = mockResponses_.find(key);
    if (it == mockResponses_.end()) {
        return std::nullopt;
    }

    HttpResult result;
    result.ok        = (it->second.httpStatus >= 200 && it->second.httpStatus < 300);
    result.httpStatus = it->second.httpStatus;
    result.body      = it->second.body;
    result.rawBody   = Json::FastWriter().write(it->second.body);
    result.attemptCount = 1;
    LOG_DEBUG << "[HttpClient] Mock response for " << key;
    return result;
}

std::string HttpClient::mockKey(const drogon::HttpRequestPtr& req) {
    return std::string(req->methodString()) + " " + req->path();
}

} // namespace idc
