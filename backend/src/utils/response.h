#pragma once

#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <string>

namespace idc {

/// Unified API response helper.
/// Every API response uses this format:
///   {"code":0, "message":"success", "data":...}
///   {"code":N, "message":"error",   "data":null}
class JsonResponse {
public:
    /// Create a success response with data payload.
    static drogon::HttpResponsePtr ok(const Json::Value& data) {
        Json::Value body;
        body["code"]    = 0;
        body["message"] = "success";
        body["data"]    = data;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(drogon::k200OK);
        return resp;
    }

    /// Create a success response with a string message and optional data.
    static drogon::HttpResponsePtr ok(const std::string& message,
                                      const Json::Value& data = Json::Value()) {
        Json::Value body;
        body["code"]    = 0;
        body["message"] = message;
        body["data"]    = data.isNull() ? Json::Value(Json::objectValue) : data;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(drogon::k200OK);
        return resp;
    }

    /// Create an error response.
    /// @param code  Application-level error code (non-zero).
    /// @param msg   Human-readable error description.
    /// @param httpStatus  HTTP status code (default 400 Bad Request).
    static drogon::HttpResponsePtr error(
        int code,
        const std::string& msg,
        drogon::HttpStatusCode httpStatus = drogon::k400BadRequest) {
        Json::Value body;
        body["code"]    = code;
        body["message"] = msg;
        body["data"]    = Json::nullValue;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(httpStatus);
        return resp;
    }

    /// Convenience: 401 Unauthorized.
    static drogon::HttpResponsePtr unauthorized(const std::string& msg = "Unauthorized") {
        return error(401, msg, drogon::k401Unauthorized);
    }

    /// Convenience: 403 Forbidden.
    static drogon::HttpResponsePtr forbidden(const std::string& msg = "Forbidden") {
        return error(403, msg, drogon::k403Forbidden);
    }

    /// Convenience: 404 Not Found.
    static drogon::HttpResponsePtr notFound(const std::string& msg = "Not Found") {
        return error(404, msg, drogon::k404NotFound);
    }

    /// Convenience: 500 Internal Server Error.
    static drogon::HttpResponsePtr serverError(const std::string& msg = "Internal Server Error") {
        return error(500, msg, drogon::k500InternalServerError);
    }
};

}  // namespace idc
