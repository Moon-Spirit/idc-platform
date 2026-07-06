#include "price_template_controller.h"
#include "services/product_service.h"
#include "services/pricing_service.h"
#include "utils/db_client.h"
#include "utils/response.h"

#include <drogon/HttpRequest.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <json/json.h>
#include <cstdint>

#include <sstream>
#include <stdexcept>

namespace idc {

static int64_t getUserId(const drogon::HttpRequestPtr& req) {
    auto attrs = req->getAttributes();
    return attrs->find("user_id") ? attrs->get<int64_t>("user_id") : 0;
}

static int64_t getRoleId(const drogon::HttpRequestPtr& req) {
    auto attrs = req->getAttributes();
    return attrs->find("role_id") ? attrs->get<int64_t>("role_id") : 0;
}

static int64_t getUserDistributorId(const drogon::HttpRequestPtr& req) {
    auto db = DbClient::getClient();
    int64_t userId = getUserId(req);
    auto rows = db->execSqlSync(
        "SELECT distributor_id FROM users WHERE id = $1", userId);
    if (rows.empty() || rows[0]["distributor_id"].isNull()) return 0;
    return rows[0]["distributor_id"].as<int64_t>();
}

// ── Template row → JSON ──────────────────────────────────────────────────────

static Json::Value rowToTemplate(const drogon::orm::Row& row) {
    Json::Value t;
    t["id"]              = static_cast<Json::Int64>(row["id"].as<int64_t>());
    t["name"]            = row["name"].as<std::string>();

    if (!row["distributor_id"].isNull())
        t["distributor_id"] = static_cast<Json::Int64>(row["distributor_id"].as<int64_t>());
    if (!row["parent_id"].isNull())
        t["parent_id"]      = static_cast<Json::Int64>(row["parent_id"].as<int64_t>());

    t["effective_date"]  = row["effective_date"].as<std::string>();
    if (!row["expiry_date"].isNull())
        t["expiry_date"]    = row["expiry_date"].as<std::string>();

    t["status"]          = row["status"].as<int16_t>();

    auto created = row["created_at"].as<std::string>();
    auto updated = row["updated_at"].as<std::string>();
    t["created_at"] = created.substr(0, 19) + "Z";
    t["updated_at"] = updated.substr(0, 19) + "Z";

    return t;
}

// ── Handlers ─────────────────────────────────────────────────────────────────

void PriceTemplateController::listTemplates(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        int page    = req->getOptionalParameter<int>("page").value_or(1);
        int perPage = req->getOptionalParameter<int>("per_page").value_or(20);

        if (page < 1) page = 1;
        if (perPage < 1) perPage = 20;
        if (perPage > 100) perPage = 100;

        auto db = DbClient::getClient();

        std::string whereClause;

        if (getRoleId(req) == 1) {
            // Admin sees all templates
            whereClause = "WHERE pt.status = 1";
        } else {
            // Dealer sees templates where distributor_id matches their own
            int64_t distId = getUserDistributorId(req);
            if (distId == 0) {
                // No distributor → no templates visible
                Json::Value data;
                data["items"]    = Json::arrayValue;
                data["total"]    = 0;
                data["page"]     = page;
                data["per_page"] = perPage;
                callback(JsonResponse::ok(data));
                return;
            }
            whereClause = "WHERE pt.status = 1 AND pt.distributor_id = " +
                          std::to_string(distId);
        }

        // Count
        std::string countSql = "SELECT COUNT(*) FROM price_templates pt " + whereClause;
        auto countResult = db->execSqlSync(countSql);
        int64_t total = countResult[0][0].as<int64_t>();

        // Fetch
        int offset = (page - 1) * perPage;
        std::string dataSql =
            "SELECT pt.* FROM price_templates pt " + whereClause +
            " ORDER BY pt.id DESC LIMIT $" +
            std::to_string(perPage) + " OFFSET $" +
            std::to_string(offset);

        // Need to pass params... simpler approach: rebuild without placeholders
        // Since distributor_id is inlined, we use a direct approach.
        dataSql = "SELECT pt.* FROM price_templates pt " + whereClause +
                  " ORDER BY pt.id DESC LIMIT " + std::to_string(perPage) +
                  " OFFSET " + std::to_string(offset);

        auto dataResult = db->execSqlSync(dataSql);

        Json::Value items(Json::arrayValue);
        for (const auto& row : dataResult) {
            items.append(rowToTemplate(row));
        }

        Json::Value data;
        data["items"]    = items;
        data["total"]    = static_cast<Json::Int64>(total);
        data["page"]     = page;
        data["per_page"] = perPage;

        callback(JsonResponse::ok(data));
    } catch (const std::exception& e) {
        LOG_ERROR << "listTemplates error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void PriceTemplateController::createTemplate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        if (!body->isMember("name")) {
            callback(JsonResponse::error(400, "Missing required field: name"));
            return;
        }

        std::string name          = (*body)["name"].asString();
        std::string effectiveDate = body->isMember("effective_date")
                                        ? (*body)["effective_date"].asString() : "";
        std::string expiryDate    = body->isMember("expiry_date")
                                        ? (*body)["expiry_date"].asString() : "";

        // Validate dates
        if (effectiveDate.empty()) {
            // Default to today
            effectiveDate = "CURRENT_DATE";
        } else {
            effectiveDate = "'" + effectiveDate + "'::date";
        }

        if (!expiryDate.empty()) {
            // Check that expiry > effective
            if (!body->isMember("effective_date")) {
                callback(JsonResponse::error(400,
                    "expiry_date requires effective_date to compare"));
                return;
            }
        }

        std::string distIdCol = body->isMember("distributor_id")
                                    ? std::to_string((*body)["distributor_id"].asInt64())
                                    : "NULL";
        std::string parentIdCol = body->isMember("parent_id")
                                      ? std::to_string((*body)["parent_id"].asInt64())
                                      : "NULL";
        std::string expiryCol = expiryDate.empty() ? "NULL" : "'" + expiryDate + "'::date";

        auto db = DbClient::getClient();
        std::string sql =
            "INSERT INTO price_templates (name, distributor_id, parent_id, "
            "effective_date, expiry_date) VALUES ($1, " +
            distIdCol + ", " + parentIdCol + ", " + effectiveDate +
            ", " + expiryCol + ") RETURNING id";

        auto result = db->execSqlSync(sql, name);
        int64_t newId = result[0]["id"].as<int64_t>();

        Json::Value data;
        data["id"] = static_cast<Json::Int64>(newId);
        callback(JsonResponse::ok(data));
    } catch (const drogon::orm::DrogonDbException& e) {
        std::string msg = e.base().what();
        LOG_ERROR << "createTemplate DB error: " << msg;
        callback(JsonResponse::error(400, "Database error: " + msg));
    } catch (const std::exception& e) {
        LOG_ERROR << "createTemplate error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void PriceTemplateController::getTemplate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto db = DbClient::getClient();
        auto rows = db->execSqlSync(
            "SELECT * FROM price_templates WHERE id = $1 AND status = 1", id);

        if (rows.empty()) {
            callback(JsonResponse::notFound("Price template not found"));
            return;
        }

        auto tmpl = rowToTemplate(rows[0]);

        // Also fetch product prices for this template
        auto priceRows = db->execSqlSync(
            "SELECT pp.*, p.name as product_name FROM product_prices pp "
            "JOIN products p ON pp.product_id = p.id "
            "WHERE pp.price_template_id = $1 AND p.status = 1 "
            "ORDER BY p.sort_order ASC, p.id ASC",
            id);

        Json::Value prices(Json::arrayValue);
        for (const auto& row : priceRows) {
            Json::Value pr;
            pr["product_id"]   = static_cast<Json::Int64>(row["product_id"].as<int64_t>());
            pr["product_name"] = row["product_name"].as<std::string>();

            auto val = [&](const char* col) -> Json::Value {
                if (row[col].isNull()) return Json::nullValue;
                return row[col].as<std::string>();
            };
            pr["monthly_price"]      = val("monthly_price");
            pr["yearly_price"]       = val("yearly_price");
            pr["hourly_price"]       = val("hourly_price");
            pr["bandwidth_95_price"] = val("bandwidth_95_price");
            pr["setup_fee"]          = val("setup_fee");

            prices.append(pr);
        }

        tmpl["product_prices"] = prices;
        callback(JsonResponse::ok(tmpl));
    } catch (const std::exception& e) {
        LOG_ERROR << "getTemplate error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void PriceTemplateController::updateTemplate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        auto db = DbClient::getClient();

        // Verify existence
        auto existing = db->execSqlSync(
            "SELECT id FROM price_templates WHERE id = $1 AND status = 1", id);
        if (existing.empty()) {
            callback(JsonResponse::notFound("Price template not found"));
            return;
        }

        // Build dynamic UPDATE
        std::vector<std::string> setClauses;
        std::vector<const char*> paramValues; // not used with inlined values
        int paramIdx = 0;

        auto addStr = [&](const char* col, const std::string& val) {
            setClauses.push_back(std::string(col) + " = $" + std::to_string(++paramIdx));
        };

        // For simplicity, use parameterized SQL with direct binding
        std::string name          = body->isMember("name") ? (*body)["name"].asString() : "";
        Json::Value effDate       = body->isMember("effective_date") ? (*body)["effective_date"] : Json::nullValue;
        Json::Value expDate       = body->isMember("expiry_date") ? (*body)["expiry_date"] : Json::nullValue;
        Json::Value distId        = body->isMember("distributor_id") ? (*body)["distributor_id"] : Json::nullValue;
        Json::Value parentId      = body->isMember("parent_id") ? (*body)["parent_id"] : Json::nullValue;
        Json::Value statusVal     = body->isMember("status") ? (*body)["status"] : Json::nullValue;

        // Build SET clause manually since Drogon ORM doesn't support dynamic updates easily
        std::string setSql;
        if (!name.empty()) {
            setSql += "name = '" + name + "'";
        }

        if (!effDate.isNull()) {
            if (!setSql.empty()) setSql += ", ";
            setSql += "effective_date = '" + effDate.asString() + "'::date";
        }

        if (!expDate.isNull()) {
            if (!setSql.empty()) setSql += ", ";
            if (expDate.isNull()) {
                setSql += "expiry_date = NULL";
            } else {
                setSql += "expiry_date = '" + expDate.asString() + "'::date";
            }
        }

        if (!distId.isNull()) {
            if (!setSql.empty()) setSql += ", ";
            if (distId.isNull()) {
                setSql += "distributor_id = NULL";
            } else {
                setSql += "distributor_id = " + std::to_string(distId.asInt64());
            }
        }

        if (!parentId.isNull()) {
            if (!setSql.empty()) setSql += ", ";
            if (parentId.isNull()) {
                setSql += "parent_id = NULL";
            } else {
                setSql += "parent_id = " + std::to_string(parentId.asInt64());
            }
        }

        if (!statusVal.isNull()) {
            if (!setSql.empty()) setSql += ", ";
            setSql += "status = " + std::to_string(statusVal.asInt());
        }

        if (setSql.empty()) {
            callback(JsonResponse::error(400, "No fields to update"));
            return;
        }

        setSql += ", updated_at = NOW()";
        std::string sql = "UPDATE price_templates SET " + setSql + " WHERE id = " +
                          std::to_string(id);

        db->execSqlSync(sql);
        callback(JsonResponse::ok(std::string("Price template updated")));
    } catch (const drogon::orm::DrogonDbException& e) {
        std::string msg = e.base().what();
        LOG_ERROR << "updateTemplate DB error: " << msg;
        callback(JsonResponse::error(400, "Database error: " + msg));
    } catch (const std::exception& e) {
        LOG_ERROR << "updateTemplate error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void PriceTemplateController::deleteTemplate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "UPDATE price_templates SET status = 0, updated_at = NOW() "
            "WHERE id = $1 AND status = 1 RETURNING id",
            id);

        if (result.empty()) {
            callback(JsonResponse::notFound("Price template not found"));
            return;
        }

        callback(JsonResponse::ok(std::string("Price template deleted")));
    } catch (const std::exception& e) {
        LOG_ERROR << "deleteTemplate error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void PriceTemplateController::getTemplateProducts(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id) {
    try {
        auto db = DbClient::getClient();

        // Verify template exists
        auto tmplRows = db->execSqlSync(
            "SELECT id, distributor_id, parent_id FROM price_templates "
            "WHERE id = $1 AND status = 1", id);
        if (tmplRows.empty()) {
            callback(JsonResponse::notFound("Price template not found"));
            return;
        }

        // Get all active products
        auto productRows = db->execSqlSync(
            "SELECT id, name, type FROM products WHERE status = 1 "
            "ORDER BY sort_order ASC, id ASC");

        // For each product, check if this template has a price (direct or inherited)
        Json::Value items(Json::arrayValue);
        for (const auto& prod : productRows) {
            int64_t prodId = prod["id"].as<int64_t>();

            // Check direct price in this template
            auto directPrice = db->execSqlSync(
                "SELECT * FROM product_prices WHERE price_template_id = $1 "
                "AND product_id = $2",
                id, prodId);

            bool inherited = directPrice.empty();
            Json::Value pr;

            if (!inherited) {
                // Direct price
                auto val = [&](const char* col) -> Json::Value {
                    if (directPrice[0][col].isNull()) return Json::nullValue;
                    return directPrice[0][col].as<std::string>();
                };
                pr["product_id"]            = static_cast<Json::Int64>(prodId);
                pr["product_name"]          = prod["name"].as<std::string>();
                pr["inherited"]             = false;
                pr["monthly_price"]         = val("monthly_price");
                pr["yearly_price"]          = val("yearly_price");
                pr["hourly_price"]          = val("hourly_price");
                pr["bandwidth_95_price"]    = val("bandwidth_95_price");
                pr["setup_fee"]             = val("setup_fee");
            } else {
                // Walk inheritance for this product
                pr["product_id"]   = static_cast<Json::Int64>(prodId);
                pr["product_name"] = prod["name"].as<std::string>();
                pr["inherited"]    = true;

                // Try to find inherited price
                int64_t tmplId = id;
                bool found = false;
                int maxDepth = 20;
                while (tmplId != 0 && maxDepth-- > 0) {
                    auto parentRows = db->execSqlSync(
                        "SELECT parent_id FROM price_templates WHERE id = $1 AND status = 1",
                        tmplId);

                    int64_t parentId = 0;
                    if (!parentRows.empty() && !parentRows[0]["parent_id"].isNull()) {
                        parentId = parentRows[0]["parent_id"].as<int64_t>();
                    } else {
                        break;
                    }

                    // Check parent template for price
                    auto parentPrice = db->execSqlSync(
                        "SELECT * FROM product_prices WHERE price_template_id = $1 "
                        "AND product_id = $2",
                        parentId, prodId);

                    if (!parentPrice.empty()) {
                        auto val = [&](const char* col) -> Json::Value {
                            if (parentPrice[0][col].isNull()) return Json::nullValue;
                            return parentPrice[0][col].as<std::string>();
                        };
                        pr["parent_monthly_price"]      = val("monthly_price");
                        pr["parent_yearly_price"]       = val("yearly_price");
                        pr["parent_hourly_price"]       = val("hourly_price");
                        pr["parent_bandwidth_95_price"] = val("bandwidth_95_price");
                        pr["parent_setup_fee"]          = val("setup_fee");
                        found = true;
                        break;
                    }

                    tmplId = parentId;
                }

                if (!found) {
                    // Try system default
                    auto def = PricingService::getSystemDefaultPrice(prodId);
                    if (!def.isNull()) {
                        pr["parent_monthly_price"]      = def["monthly_price"];
                        pr["parent_yearly_price"]       = def["yearly_price"];
                        pr["parent_hourly_price"]       = def["hourly_price"];
                        pr["parent_bandwidth_95_price"] = def["bandwidth_95_price"];
                        pr["parent_setup_fee"]          = def["setup_fee"];
                    }
                }
            }

            items.append(pr);
        }

        callback(JsonResponse::ok(items));
    } catch (const std::exception& e) {
        LOG_ERROR << "getTemplateProducts error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

void PriceTemplateController::setProductPrice(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    int64_t id, int64_t productId) {
    try {
        auto body = req->getJsonObject();
        if (!body) {
            callback(JsonResponse::error(400, "Request body must be valid JSON"));
            return;
        }

        auto db = DbClient::getClient();

        // Verify template exists
        auto tmplCheck = db->execSqlSync(
            "SELECT id FROM price_templates WHERE id = $1 AND status = 1", id);
        if (tmplCheck.empty()) {
            callback(JsonResponse::notFound("Price template not found"));
            return;
        }

        // Verify product exists
        auto prodCheck = db->execSqlSync(
            "SELECT id FROM products WHERE id = $1 AND status = 1", productId);
        if (prodCheck.empty()) {
            callback(JsonResponse::notFound("Product not found"));
            return;
        }

        // Extract price values from body
        auto valStr = [&](const char* key) -> std::string {
            if (!body->isMember(key) || (*body)[key].isNull()) return "NULL";
            return "'" + (*body)[key].asString() + "'";
        };

        std::string monthlyPrice      = valStr("monthly_price");
        std::string yearlyPrice       = valStr("yearly_price");
        std::string hourlyPrice       = valStr("hourly_price");
        std::string bandwidth95Price  = valStr("bandwidth_95_price");
        std::string setupFee          = valStr("setup_fee");

        // UPSERT: insert or update
        std::string sql =
            "INSERT INTO product_prices (price_template_id, product_id, "
            "monthly_price, yearly_price, hourly_price, bandwidth_95_price, setup_fee) "
            "VALUES (" + std::to_string(id) + ", " + std::to_string(productId) + ", " +
            monthlyPrice + ", " + yearlyPrice + ", " + hourlyPrice + ", " +
            bandwidth95Price + ", " + setupFee + ") "
            "ON CONFLICT (price_template_id, product_id) DO UPDATE SET "
            "monthly_price = EXCLUDED.monthly_price, "
            "yearly_price = EXCLUDED.yearly_price, "
            "hourly_price = EXCLUDED.hourly_price, "
            "bandwidth_95_price = EXCLUDED.bandwidth_95_price, "
            "setup_fee = EXCLUDED.setup_fee";

        db->execSqlSync(sql);
        callback(JsonResponse::ok(std::string("Product price set")));
    } catch (const drogon::orm::DrogonDbException& e) {
        std::string msg = e.base().what();
        LOG_ERROR << "setProductPrice DB error: " << msg;
        callback(JsonResponse::error(400, "Database error: " + msg));
    } catch (const std::exception& e) {
        LOG_ERROR << "setProductPrice error: " << e.what();
        callback(JsonResponse::serverError(e.what()));
    }
}

} // namespace idc
