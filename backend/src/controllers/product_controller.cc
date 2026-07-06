#include "product_controller.h"
#include "services/product_service.h"
#include "services/pricing_service.h"
#include "utils/response.h"

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include <string>

namespace idc {

static int64_t getUserId(const drogon::HttpRequestPtr& req) {
    auto attrs = req->getAttributes();
    return attrs->find("user_id") ? attrs->get<int64_t>("user_id") : 0;
}

static int64_t getRoleId(const drogon::HttpRequestPtr& req) {
    auto attrs = req->getAttributes();
    return attrs->find("role_id") ? attrs->get<int64_t>("role_id") : 0;
}

void ProductController::listProducts(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        std::string type    = req->getOptionalParameter<std::string>("type").value_or("");
        std::string keyword = req->getOptionalParameter<std::string>("keyword").value_or("");
        int page     = req->getOptionalParameter<int>("page").value_or(1);
        int perPage  = req->getOptionalParameter<int>("per_page").value_or(20);

        if (page < 1) page = 1;
        if (perPage < 1) perPage = 20;
        if (perPage > 100) perPage = 100;

        auto data = ProductService::listProducts(type, keyword, page, perPage);
        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_ERROR << "listProducts error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void ProductController::getProductTypes(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto data = ProductService::getProductTypes();
        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_ERROR << "getProductTypes error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void ProductController::getProduct(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto product = ProductService::getProductById(id);
        if (product.isNull()) {
            callback(JsonResponse::notFound("Product not found"));
            return;
        }
        callback(JsonResponse::ok(product));
    } catch (const std::exception& e) {
        LOG_ERROR << "getProduct error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void ProductController::createProduct(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("name") || !body->isMember("type")) {
            callback(JsonResponse::error(400, "Missing required fields: name, type"));
            return;
        }

        std::string name        = (*body)["name"].asString();
        std::string type        = (*body)["type"].asString();
        std::string description = body->isMember("description") ? (*body)["description"].asString() : "";
        Json::Value specs       = body->isMember("specs") ? (*body)["specs"] : Json::objectValue;
        int sortOrder           = body->isMember("sort_order") ? (*body)["sort_order"].asInt() : 0;

        int64_t newId = ProductService::createProduct(name, type, description, specs, sortOrder);

        Json::Value data;
        data["id"] = static_cast<Json::Int64>(newId);
        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR << "createProduct error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void ProductController::updateProduct(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("name") || !body->isMember("type")) {
            callback(JsonResponse::error(400, "Missing required fields: name, type"));
            return;
        }

        std::string name        = (*body)["name"].asString();
        std::string type        = (*body)["type"].asString();
        std::string description = body->isMember("description") ? (*body)["description"].asString() : "";
        Json::Value specs       = body->isMember("specs") ? (*body)["specs"] : Json::objectValue;
        int status              = body->isMember("status") ? (*body)["status"].asInt() : 1;
        int sortOrder           = body->isMember("sort_order") ? (*body)["sort_order"].asInt() : 0;

        ProductService::updateProduct(id, name, type, description, specs,
                                       static_cast<int16_t>(status), sortOrder);
        callback(JsonResponse::ok(std::string("Product updated")));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR << "updateProduct error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void ProductController::deleteProduct(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        ProductService::deleteProduct(id);
        callback(JsonResponse::ok(std::string("Product deleted")));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::notFound(e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR << "deleteProduct error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void ProductController::getProductPrice(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto product = ProductService::getProductById(id);
        if (product.isNull()) {
            callback(JsonResponse::notFound("Product not found"));
            return;
        }

        int64_t userId = getUserId(req);
        int64_t roleId = getRoleId(req);

        auto price = PricingService::getPriceForUser(userId, roleId, id);
        price["product_name"] = product["name"];

        callback(JsonResponse::ok(price));
    } catch (const std::exception& e) {
        LOG_ERROR << "getProductPrice error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
