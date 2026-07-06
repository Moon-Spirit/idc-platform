#include "cart_controller.h"
#include "services/product_service.h"
#include "services/pricing_service.h"
#include "utils/response.h"
#include "utils/redis_client.h"
#include "utils/logger.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <iomanip>
#include <sstream>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: resolve distributor ID from request
// ═══════════════════════════════════════════════════════════════════════════

int64_t CartController::resolveDistributorId(
    const drogon::HttpRequestPtr& req) {
    auto attrs = req->getAttributes();
    int64_t roleId = attrs->get<int64_t>("role_id");
    int64_t userId = attrs->get<int64_t>("user_id");

    // Admin can pass ?distributor_id= to manage another's cart
    if (roleId == 1) {
        auto distId = req->getOptionalParameter<int64_t>("distributor_id");
        if (distId.has_value()) {
            return distId.value();
        }
    }

    // Get distributor_id from user record
    auto db = drogon::app().getDbClient("idc_db");
    auto result = db->execSqlSync(
        "SELECT distributor_id FROM users WHERE id = $1", userId);
    if (result.empty() || result[0]["distributor_id"].isNull()) {
        return 0;
    }
    return result[0]["distributor_id"].as<int64_t>();
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/cart/items
// ═══════════════════════════════════════════════════════════════════════════

void CartController::addItem(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        // Validate required fields
        if (!body->isMember("product_id")) {
            callback(JsonResponse::error(400, "Missing required field: product_id"));
            return;
        }

        int64_t productId = (*body)["product_id"].asInt64();
        int quantity = body->get("quantity", 1).asInt();
        int periodMonths = body->get("period_months", 1).asInt();
        Json::Value config = body->get("config", Json::objectValue);

        if (quantity < 1) {
            callback(JsonResponse::error(400, "quantity must be >= 1"));
            return;
        }
        if (periodMonths < 1) {
            callback(JsonResponse::error(400, "period_months must be >= 1"));
            return;
        }

        // Validate product exists
        auto product = ProductService::getProductById(productId);
        if (product.isNull()) {
            callback(JsonResponse::error(400, "Product not found"));
            return;
        }

        // Resolve distributor
        int64_t distributorId = resolveDistributorId(req);
        if (distributorId <= 0) {
            callback(JsonResponse::error(400, "User has no distributor assigned"));
            return;
        }

        // Build cart item JSON
        Json::Value cartItem;
        cartItem["product_id"] = static_cast<Json::Int64>(productId);
        cartItem["product_name"] = product["name"].asString();
        cartItem["product_type"] = product["type"].asString();
        cartItem["quantity"] = quantity;
        cartItem["period_months"] = periodMonths;
        cartItem["config"] = config;
        cartItem["created_at"] = Json::Value::Int64(std::time(nullptr));

        // Store in Redis hash
        std::string key = cartKey(distributorId);
        auto redis = RedisClient::getClient();

        // Generate sequential item ID using INCR
        std::string seqKey = key + ":seq";
        int64_t itemId = redis->execCommandSync<int64_t>(
            [](const drogon::nosql::RedisResult& r) -> int64_t {
                return r.asInteger();
            },
            "INCR %s", seqKey.c_str());

        // Store the item as JSON string in the hash
        std::string field = std::to_string(itemId);
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        std::string value = Json::writeString(writer, cartItem);

        redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult&) -> std::string { return "OK"; },
            "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());

        // Set TTL on cart key (7 days)
        redis->execCommandSync<int64_t>(
            [](const drogon::nosql::RedisResult&) { return 0; },
            "EXPIRE %s %d", key.c_str(), 604800);

        // Build response
        Json::Value data;
        data["id"] = static_cast<Json::Int64>(itemId);
        data["product_id"] = static_cast<Json::Int64>(productId);
        data["product_name"] = product["name"].asString();
        data["product_type"] = product["type"].asString();
        data["quantity"] = quantity;
        data["period_months"] = periodMonths;
        data["config"] = config;

        callback(JsonResponse::ok(data));
    } catch (const std::invalid_argument& e) {
        callback(JsonResponse::error(400, e.what()));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Cart] addItem error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/cart
// ═══════════════════════════════════════════════════════════════════════════

void CartController::listCart(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        int64_t distributorId = resolveDistributorId(req);
        if (distributorId <= 0) {
            callback(JsonResponse::error(400, "User has no distributor assigned"));
            return;
        }

        auto attrs = req->getAttributes();
        int64_t roleId = attrs->get<int64_t>("role_id");
        int64_t userId = attrs->get<int64_t>("user_id");

        std::string key = cartKey(distributorId);
        auto redis = RedisClient::getClient();

        // HGETALL to get all items
        auto redisResult = redis->execCommandSync<Json::Value>(
            [](const drogon::nosql::RedisResult& r) -> Json::Value {
                Json::Value items(Json::arrayValue);
                if (r.isNil()) return items;
                auto arr = r.asArray();
                for (size_t i = 0; i + 1 < arr.size(); i += 2) {
                    Json::Reader reader;
                    Json::Value parsed;
                    if (reader.parse(arr[i + 1].asString(), parsed)) {
                        parsed["id"] = static_cast<Json::Int64>(
                            std::stoll(arr[i].asString()));
                        items.append(parsed);
                    }
                }
                return items;
            },
            "HGETALL %s", key.c_str());

        // Enrich each item with current prices and compute subtotals
        double totalAmount = 0.0;
        for (auto& item : redisResult) {
            int64_t productId = item["product_id"].asInt64();
            int quantity = item.get("quantity", 1).asInt();
            int periodMonths = item.get("period_months", 1).asInt();

            // Get current price
            Json::Value price;
            if (roleId == 1) {
                price = PricingService::getSystemDefaultPrice(productId);
            } else {
                auto priceResult = PricingService::getPriceForDistributor(
                    distributorId, productId);
                price = priceResult.priceData;
            }

            if (!price.isNull()) {
                // Default to monthly price
                std::string unitPrice = price.get("monthly_price", "0.00").asString();
                if (price.isMember("yearly_price")) {
                    // We show monthly as the base unit price
                    unitPrice = price["monthly_price"].asString();
                }

                item["unit_price"] = unitPrice;

                // Compute subtotal: unit_price * quantity * period_months
                double unitPriceVal = std::stod(unitPrice);
                double subtotal = unitPriceVal * quantity * periodMonths;
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(2) << subtotal;
                item["subtotal"] = ss.str();
                totalAmount += subtotal;
            } else {
                item["unit_price"] = "0.00";
                item["subtotal"] = "0.00";
            }
        }

        Json::Value data;
        data["items"] = redisResult;
        std::ostringstream totalSs;
        totalSs << std::fixed << std::setprecision(2) << totalAmount;
        data["total_amount"] = totalSs.str();

        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Cart] listCart error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUT /api/v1/cart/items/{id}
// ═══════════════════════════════════════════════════════════════════════════

void CartController::updateItem(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        int64_t distributorId = resolveDistributorId(req);
        if (distributorId <= 0) {
            callback(JsonResponse::error(400, "User has no distributor assigned"));
            return;
        }

        std::string key = cartKey(distributorId);
        auto redis = RedisClient::getClient();

        // Check item exists
        bool exists = redis->execCommandSync<int64_t>(
            [](const drogon::nosql::RedisResult& r) { return r.asInteger(); },
            "HEXISTS %s %s", key.c_str(), id.c_str()) > 0;

        if (!exists) {
            callback(JsonResponse::notFound("Cart item not found"));
            return;
        }

        // Get current item
        auto currentStr = redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult& r) -> std::string {
                if (r.isNil()) return {};
                return r.asString();
            },
            "HGET %s %s", key.c_str(), id.c_str());

        Json::Reader reader;
        Json::Value cartItem;
        if (!reader.parse(currentStr, cartItem)) {
            callback(JsonResponse::serverError("Failed to parse cart item"));
            return;
        }

        // Update fields
        if (body->isMember("quantity")) {
            int qty = (*body)["quantity"].asInt();
            if (qty < 1) {
                callback(JsonResponse::error(400, "quantity must be >= 1"));
                return;
            }
            cartItem["quantity"] = qty;
        }
        if (body->isMember("period_months")) {
            int months = (*body)["period_months"].asInt();
            if (months < 1) {
                callback(JsonResponse::error(400, "period_months must be >= 1"));
                return;
            }
            cartItem["period_months"] = months;
        }
        if (body->isMember("config")) {
            cartItem["config"] = (*body)["config"];
        }

        // Write back
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        std::string value = Json::writeString(writer, cartItem);

        redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult&) -> std::string { return "OK"; },
            "HSET %s %s %s", key.c_str(), id.c_str(), value.c_str());

        // Build response
        Json::Value data;
        data["id"] = static_cast<Json::Int64>(std::stoll(id));
        data["product_id"] = cartItem["product_id"];
        data["product_name"] = cartItem["product_name"];
        data["product_type"] = cartItem["product_type"];
        data["quantity"] = cartItem["quantity"];
        data["period_months"] = cartItem["period_months"];
        data["config"] = cartItem["config"];

        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Cart] updateItem error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  DELETE /api/v1/cart/items/{id}
// ═══════════════════════════════════════════════════════════════════════════

void CartController::removeItem(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {
    try {
        int64_t distributorId = resolveDistributorId(req);
        if (distributorId <= 0) {
            callback(JsonResponse::error(400, "User has no distributor assigned"));
            return;
        }

        std::string key = cartKey(distributorId);
        auto redis = RedisClient::getClient();

        // Check and remove
        int64_t removed = redis->execCommandSync<int64_t>(
            [](const drogon::nosql::RedisResult& r) { return r.asInteger(); },
            "HDEL %s %s", key.c_str(), id.c_str());

        if (removed == 0) {
            callback(JsonResponse::notFound("Cart item not found"));
            return;
        }

        callback(JsonResponse::ok(std::string("Item removed from cart")));
    } catch (const std::exception& e) {
        LOG_IDC_ERROR(req, "[Cart] removeItem error: " << e.what());
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
