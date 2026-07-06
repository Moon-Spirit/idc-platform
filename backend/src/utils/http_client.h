#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <json/json.h>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace idc {

/// Result of an HTTP request with retry logic.
struct HttpResult {
    bool     ok    = false;
    int      httpStatus = 0;        ///< HTTP status code (0 if connection failed)
    Json::Value body;               ///< Parsed JSON body (may be null)
    std::string rawBody;            ///< Raw response body string
    std::string error;              ///< Error message if !ok
    int         attemptCount = 0;   ///< How many attempts were made
};

/// HTTP client wrapper with retry, timeout, and mock mode support.
///
/// Retry strategy:
///   - 3 attempts max
///   - Exponential backoff: 1s, 2s, 4s
///   - Retries on: connection failures, 5xx, timeout
///   - Does NOT retry on: 4xx (client errors)
///
/// Timeout:
///   - Connect timeout: 5 seconds
///   - Read timeout: 30 seconds
///
/// Mock mode:
///   - When Config::zjmf.mock = true, requests to ZJMF APIs are intercepted
///     and return mock responses from a configurable directory.
///   - This allows frontend and integration testing without a live ZJMF instance.
class HttpClient {
public:
    static constexpr int kMaxRetries    = 3;
    static constexpr int kConnectTimeoutSec = 5;
    static constexpr int kReadTimeoutSec    = 30;

    static constexpr int kBackoffMs[] = { 1000, 2000, 4000 };

    /// @param baseUrl   Base URL (e.g. https://dcim.example.com/api)
    ///                  The path portion is stripped for Drogon's HttpClient;
    ///                  request paths are appended as full ZJMF API paths.
    explicit HttpClient(std::string baseUrl,
                        std::string apiKey,
                        std::string apiSecret,
                        bool mockMode = false);

    /// GET request. Path is the full API path (e.g. "/api/v1/products").
    HttpResult get(const std::string& path,
                   const std::vector<std::pair<std::string, std::string>>& queryParams = {});

    /// POST request with JSON body.
    HttpResult post(const std::string& path, const Json::Value& body);

    /// PUT request with JSON body.
    HttpResult put(const std::string& path, const Json::Value& body);

    /// DELETE request.
    HttpResult del(const std::string& path);

    /// Mock mode control.
    bool isMockMode() const { return mockMode_; }
    void setMockResponse(const std::string& method,
                         const std::string& path,
                         Json::Value response,
                         int httpStatus = 200);

private:
    std::string hostOnly_;  ///< scheme://host:port (no path)
    std::string apiKey_;
    std::string apiSecret_;
    bool mockMode_;

    struct MockEntry {
        Json::Value body;
        int httpStatus;
    };
    std::unordered_map<std::string, MockEntry> mockResponses_;

    void applyAuth(drogon::HttpRequestPtr req) const;
    HttpResult executeSingle(const drogon::HttpRequestPtr& req);
    HttpResult executeWithRetry(const drogon::HttpRequestPtr& req);
    std::optional<HttpResult> tryMockResponse(const drogon::HttpRequestPtr& req);
    static std::string mockKey(const drogon::HttpRequestPtr& req);
};

} // namespace idc
