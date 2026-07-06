#include "pricing_service.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <json/json.h>
#include <cstdint>

#include <sstream>

namespace idc {

// ── Helpers ──────────────────────────────────────────────────────────────────

/// Convert a product_prices row to a price JSON object.
static Json::Value rowToPrice(const drogon::orm::Row& row,
                              const std::string& productName = "") {
    Json::Value p;
    p["product_id"] = static_cast<Json::Int64>(row["product_id"].as<int64_t>());
    if (!productName.empty()) {
        p["product_name"] = productName;
    }

    // DECIMAL values come as strings from the ORM; pass them through as strings
    // to avoid floating-point precision loss in JSON.
    auto val = [&](const char* col) -> Json::Value {
        if (row[col].isNull()) return Json::nullValue;
        return row[col].as<std::string>();
    };

    p["monthly_price"]      = val("monthly_price");
    p["yearly_price"]       = val("yearly_price");
    p["hourly_price"]       = val("hourly_price");
    p["bandwidth_95_price"] = val("bandwidth_95_price");
    p["setup_fee"]          = val("setup_fee");

    return p;
}

// ── PricingService implementation ────────────────────────────────────────────

Json::Value PricingService::getPriceForUser(int64_t userId, int64_t roleId,
                                             int64_t productId) {
    // Admin → system default price
    if (roleId == 1) {
        auto result = getSystemDefaultPrice(productId);
        if (!result.isNull()) {
            result["price_source"] = "system_default";
        }
        return result;
    }

    // Dealer → look up distributor_id from user record
    auto db = DbClient::getClient();
    auto userRow = db->execSqlSync(
        "SELECT distributor_id FROM users WHERE id = $1 AND status = 1", userId);

    if (userRow.empty() || userRow[0]["distributor_id"].isNull()) {
        // User has no distributor → fall back to system default
        auto result = getSystemDefaultPrice(productId);
        if (!result.isNull()) {
            result["price_source"] = "system_default";
        }
        return result;
    }

    int64_t distributorId = userRow[0]["distributor_id"].as<int64_t>();
    auto priceResult = getPriceForDistributor(distributorId, productId);

    if (!priceResult.priceData.isNull()) {
        priceResult.priceData["price_source"] = priceResult.source;
    }
    return priceResult.priceData;
}

PriceResult PricingService::getPriceForDistributor(int64_t distributorId,
                                                    int64_t productId) {
    auto db = DbClient::getClient();

    // Get the distributor's price_template_id
    auto distRow = db->execSqlSync(
        "SELECT price_template_id FROM distributors WHERE id = $1 AND status = 1",
        distributorId);

    if (distRow.empty() || distRow[0]["price_template_id"].isNull()) {
        // No template assigned → system default
        Json::Value def = getSystemDefaultPrice(productId);
        return {def, "system_default"};
    }

    int64_t templateId = distRow[0]["price_template_id"].as<int64_t>();

    // Walk the template chain (template → parent → grandparent → ...)
    // We use a loop based on the template's parent_id chain.
    int64_t currentId = templateId;
    int maxDepth = 20; // safety limit
    while (currentId != 0 && maxDepth-- > 0) {
        // Check if this template has a price for this product
        auto priceRow = db->execSqlSync(
            "SELECT pp.* FROM product_prices pp "
            "JOIN price_templates pt ON pp.price_template_id = pt.id "
            "WHERE pp.price_template_id = $1 AND pp.product_id = $2 "
            "AND pt.status = 1 "
            "AND (pt.effective_date IS NULL OR pt.effective_date <= CURRENT_DATE) "
            "AND (pt.expiry_date IS NULL OR pt.expiry_date >= CURRENT_DATE)",
            currentId, productId);

        if (!priceRow.empty()) {
            // Found a price in this template
            std::string source = "price_template:" + std::to_string(currentId);
            return {rowToPrice(priceRow[0]), source};
        }

        // Walk up to parent
        auto parentRow = db->execSqlSync(
            "SELECT parent_id FROM price_templates WHERE id = $1 AND status = 1",
            currentId);

        if (parentRow.empty() || parentRow[0]["parent_id"].isNull()) {
            currentId = 0; // no more parents
        } else {
            currentId = parentRow[0]["parent_id"].as<int64_t>();
        }
    }

    // No price found in the chain → system default
    Json::Value def = getSystemDefaultPrice(productId);
    return {def, "system_default"};
}

Json::Value PricingService::getSystemDefaultPrice(int64_t productId) {
    auto db = DbClient::getClient();

    // The system default template has distributor_id IS NULL
    auto rows = db->execSqlSync(
        "SELECT pp.* FROM product_prices pp "
        "JOIN price_templates pt ON pp.price_template_id = pt.id "
        "WHERE pt.distributor_id IS NULL AND pt.status = 1 "
        "AND pp.product_id = $1 "
        "AND (pt.effective_date IS NULL OR pt.effective_date <= CURRENT_DATE) "
        "AND (pt.expiry_date IS NULL OR pt.expiry_date >= CURRENT_DATE) "
        "LIMIT 1",
        productId);

    if (rows.empty()) {
        // No default price → return a skeleton with null values
        Json::Value p;
        p["product_id"]         = static_cast<Json::Int64>(productId);
        p["monthly_price"]      = Json::nullValue;
        p["yearly_price"]       = Json::nullValue;
        p["hourly_price"]       = Json::nullValue;
        p["bandwidth_95_price"] = Json::nullValue;
        p["setup_fee"]          = "0.00";
        return p;
    }

    return rowToPrice(rows[0]);
}

std::string PricingService::getProductName(int64_t productId) {
    auto db = DbClient::getClient();
    auto rows = db->execSqlSync(
        "SELECT name FROM products WHERE id = $1 AND status = 1", productId);
    if (rows.empty()) return "";
    return rows[0]["name"].as<std::string>();
}

} // namespace idc
