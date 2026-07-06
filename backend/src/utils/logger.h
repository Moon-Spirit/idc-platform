#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/utils/Utilities.h>
#include <string>
#include <string_view>

namespace idc {

/// Request-aware logger that injects a unique request ID into every log line.
///
/// Usage in controllers:
///   auto reqId = Logger::setRequestId(req, Logger::generateId());
///   LOG_INFO << Logger::tag(req) << "Processing order creation";
///
/// Or use the convenience macros LOG_IDC_* which do this automatically.
class Logger {
public:
    /// Attach a request ID to the request for traceability.
    static std::string setRequestId(const drogon::HttpRequestPtr& req) {
        auto id = generateId();
        req->setAttribute("request_id", id);
        return id;
    }

    /// Attach an explicit request ID.
    static void setRequestId(const drogon::HttpRequestPtr& req,
                             const std::string& id) {
        req->setAttribute("request_id", id);
    }

    /// Retrieve the request ID from the request attributes.
    static std::string getRequestId(const drogon::HttpRequestPtr& req) {
        if (req) {
            auto attr = req->getAttribute("request_id");
            if (attr) {
                return attr->as<std::string>();
            }
        }
        return "-";
    }

    /// Generate a unique request ID (UUID v4).
    static std::string generateId() {
        return drogon::utils::getUuid();
    }

    /// Return a log tag string like "[req-123e4567] " for embedding in log
    /// messages.
    static std::string tag(const drogon::HttpRequestPtr& req) {
        return "[" + getRequestId(req) + "] ";
    }
};

// ── Convenience macros (include request ID automatically) ─────────────────

#define LOG_IDC_TRACE(req, msg) \
    LOG_TRACE << idc::Logger::tag(req) << msg

#define LOG_IDC_DEBUG(req, msg) \
    LOG_DEBUG << idc::Logger::tag(req) << msg

#define LOG_IDC_INFO(req, msg) \
    LOG_INFO << idc::Logger::tag(req) << msg

#define LOG_IDC_WARN(req, msg) \
    LOG_WARN << idc::Logger::tag(req) << msg

#define LOG_IDC_ERROR(req, msg) \
    LOG_ERROR << idc::Logger::tag(req) << msg

}  // namespace idc
