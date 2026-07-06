#include "order_service.h"
#include "models/order_state_machine.h"
#include "services/pricing_service.h"
#include "services/product_service.h"
#include "utils/db_client.h"
#include "utils/redis_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <cmath>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Order Number Generation
// ═══════════════════════════════════════════════════════════════════════════

std::string OrderService::generateOrderNo(drogon::orm::Transaction& trans) {
    int seq = getDailySequence(trans, "ORD");
    auto now = std::time(nullptr);
    auto tm = *std::gmtime(&now);
    std::ostringstream oss;
    oss << "ORD"
        << std::setfill('0') << std::setw(2) << (tm.tm_year % 100)
        << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1)
        << std::setfill('0') << std::setw(2) << tm.tm_mday
        << "-"
        << std::setfill('0') << std::setw(4) << seq;
    return oss.str();
}

std::string OrderService::generateSubNo(drogon::orm::Transaction& trans) {
    int seq = getDailySequence(trans, "SUB");
    auto now = std::time(nullptr);
    auto tm = *std::gmtime(&now);
    std::ostringstream oss;
    oss << "SUB"
        << std::setfill('0') << std::setw(2) << (tm.tm_year % 100)
        << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1)
        << std::setfill('0') << std::setw(2) << tm.tm_mday
        << "-"
        << std::setfill('0') << std::setw(4) << seq;
    return oss.str();
}

int OrderService::getDailySequence(drogon::orm::Transaction& trans,
                                   const std::string& prefix) {
    auto now = std::time(nullptr);
    auto tm = *std::gmtime(&now);
    std::ostringstream pattern;
    pattern << prefix
            << std::setfill('0') << std::setw(2) << (tm.tm_year % 100)
            << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1)
            << std::setfill('0') << std::setw(2) << tm.tm_mday
            << "-%";

    // Determine which column and table to query based on prefix
    std::string table = (prefix == "ORD") ? "orders" : "subscriptions";
    std::string column = (prefix == "ORD") ? "order_no" : "sub_no";

    std::string sql =
        "SELECT COALESCE(MAX(SUBSTRING(" + column + " FROM '.{4}$')::INTEGER), 0) + 1 "
        "FROM " + table + " "
        "WHERE " + column + " LIKE $1";

    auto result = trans.execSqlSync(sql, pattern.str());
    if (!result.empty()) {
        return result[0][0].as<int>();
    }
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: compute subtotal
// ═══════════════════════════════════════════════════════════════════════════

std::string OrderService::computeSubtotal(int quantity,
                                          const std::string& unitPrice,
                                          int periodMonths) {
    double price = std::stod(unitPrice);
    double subtotal = price * quantity * periodMonths;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << subtotal;
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: get user's distributor_id
// ═══════════════════════════════════════════════════════════════════════════

int64_t OrderService::getUserDistributorId(int64_t userId) {
    auto db = DbClient::getClient();
    auto result = db->execSqlSync(
        "SELECT distributor_id FROM users WHERE id = $1", userId);
    if (result.empty()) return 0;
    auto val = result[0]["distributor_id"];
    return val.isNull() ? 0 : val.as<int64_t>();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Create Order (cart → order transaction)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value OrderService::createOrder(int64_t userId, int64_t roleId,
                                       int64_t distributorId,
                                       const std::string& billingCycle,
                                       const std::string& remark) {
    // ── 1. Load cart from Redis ─────────────────────────────────────────────
    std::string cartKey = "cart:" + std::to_string(distributorId);
    auto redisClient = RedisClient::getClient();

    // HGETALL cart:{distributor_id}
    Json::Value cartItems(Json::arrayValue);
    {
        auto redisResult = redisClient->execCommandSync<Json::Value>(
            [](const drogon::nosql::RedisResult& r) -> Json::Value {
                Json::Value items(Json::arrayValue);
                if (r.isNil()) return items;
                auto arr = r.asArray();
                // Each pair is (field, value)
                for (size_t i = 0; i + 1 < arr.size(); i += 2) {
                    Json::Reader reader;
                    Json::Value parsed;
                    if (reader.parse(arr[i + 1].asString(), parsed)) {
                        parsed["_field"] = arr[i].asString();
                        items.append(parsed);
                    }
                }
                return items;
            },
            "HGETALL %s", cartKey.c_str());
        cartItems = redisResult;
    }

    if (cartItems.empty()) {
        throw std::invalid_argument("Cart is empty");
    }

    // Determine the effective billing cycle
    std::string effBillingCycle = billingCycle;
    if (effBillingCycle.empty()) {
        // Default to monthly if not specified
        effBillingCycle = "monthly";
    }
    if (effBillingCycle != "monthly" && effBillingCycle != "yearly") {
        throw std::invalid_argument("Invalid billing_cycle: must be 'monthly' or 'yearly'");
    }

    // ── 2. Validate products & compute pricing ──────────────────────────────
    double totalAmount = 0.0;
    Json::Value orderItemsJson(Json::arrayValue);

    for (auto& cartItem : cartItems) {
        int64_t productId = cartItem["product_id"].asInt64();
        int quantity = cartItem.get("quantity", 1).asInt();
        int periodMonths = cartItem.get("period_months", 1).asInt();
        Json::Value config = cartItem.get("config", Json::objectValue);

        // Validate product exists
        auto product = ProductService::getProductById(productId);
        if (product.isNull()) {
            throw std::invalid_argument(
                "Product not found: id=" + std::to_string(productId));
        }

        // Look up price for this user/distributor
        Json::Value price;
        if (roleId == 1) {
            // Admin sees system default price
            price = PricingService::getSystemDefaultPrice(productId);
        } else {
            auto priceResult = PricingService::getPriceForDistributor(
                distributorId, productId);
            price = priceResult.priceData;
        }

        if (price.isNull()) {
            throw std::invalid_argument(
                "No price configured for product: " + product["name"].asString());
        }

        // Determine unit price based on billing cycle
        std::string unitPrice;
        if (effBillingCycle == "yearly" && price.isMember("yearly_price")) {
            unitPrice = price["yearly_price"].asString();
        } else {
            unitPrice = price["monthly_price"].asString();
        }

        // For yearly, the unit_price is the yearly total; subtotal = unit_price * quantity
        // For monthly, subtotal = unit_price * quantity * period_months
        double unitPriceVal = std::stod(unitPrice);
        double subtotalVal;
        if (effBillingCycle == "yearly") {
            subtotalVal = unitPriceVal * quantity;
        } else {
            subtotalVal = unitPriceVal * quantity * periodMonths;
        }
        totalAmount += subtotalVal;

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << subtotalVal;
        std::string subtotal = ss.str();

        Json::Value item;
        item["product_id"] = static_cast<Json::Int64>(productId);
        item["product_name"] = product["name"].asString();
        item["product_specs"] = config;
        item["quantity"] = quantity;
        item["unit_price"] = unitPrice;
        item["subtotal"] = subtotal;
        item["period_months"] = periodMonths;
        orderItemsJson.append(item);
    }

    std::ostringstream totalSs;
    totalSs << std::fixed << std::setprecision(2) << totalAmount;
    std::string totalAmountStr = totalSs.str();

    // ── 3. Execute DB transaction ───────────────────────────────────────────
    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // Generate order number
        std::string orderNo = generateOrderNo(trans);

        // INSERT into orders
        auto orderResult = trans.execSqlSync(
            "INSERT INTO orders (order_no, distributor_id, status, "
            "total_amount, final_amount, billing_cycle, remark) "
            "VALUES ($1, $2, 'pending', $3, $3, $4, $5) "
            "RETURNING id",
            orderNo, distributorId, totalAmountStr,
            effBillingCycle, remark);

        if (orderResult.empty()) {
            throw std::runtime_error("Failed to create order");
        }
        int64_t orderId = orderResult[0][0].as<int64_t>();

        // INSERT each order_item
        for (const auto& item : orderItemsJson) {
            trans.execSqlSync(
                "INSERT INTO order_items (order_id, product_id, product_name, "
                "product_specs, quantity, unit_price, subtotal, period_months) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)",
                orderId,
                item["product_id"].asInt64(),
                item["product_name"].asString(),
                item["product_specs"].toStyledString(), // store config as specs snapshot
                item["quantity"].asInt(),
                item["unit_price"].asString(),
                item["subtotal"].asString(),
                item["period_months"].asInt());
        }

        // INSERT subscriptions (one per order item)
        for (const auto& item : orderItemsJson) {
            std::string subNo = generateSubNo(trans);
            int64_t productId = item["product_id"].asInt64();
            int periodMonths = item["period_months"].asInt();

            trans.execSqlSync(
                "INSERT INTO subscriptions (sub_no, order_id, distributor_id, "
                "product_id, product_specs, status, billing_cycle, auto_renew, "
                "provision_status) "
                "VALUES ($1, $2, $3, $4, $5, 'pending', $6, true, 'pending')",
                subNo, orderId, distributorId, productId,
                item["product_specs"].toStyledString(),
                effBillingCycle);
        }

        // INSERT initial order_timeline entry
        trans.execSqlSync(
            "INSERT INTO order_timeline (order_id, from_status, to_status, "
            "operator_id, operator_name, remark) "
            "VALUES ($1, NULL, 'pending', $2, $3, '订单已创建')",
            orderId, userId, std::to_string(userId)); // simple operator name

        // Clear cart from Redis
        redisClient->execCommandSync<int64_t>(
            [](const drogon::nosql::RedisResult&) { return 0; },
            "DEL %s", cartKey.c_str());

        // Build result
        result["id"] = static_cast<Json::Int64>(orderId);
        result["order_no"] = orderNo;
        result["status"] = "pending";
        result["total_amount"] = totalAmountStr;
        result["final_amount"] = totalAmountStr;
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  List Orders
// ═══════════════════════════════════════════════════════════════════════════

Json::Value OrderService::listOrders(int64_t userId, int64_t roleId,
                                       const std::string& status,
                                       int64_t distributorId,
                                       int page, int perPage) {
    auto db = DbClient::getClient();

    // Permission filter: dealers only see their own orders
    bool isAdmin = (roleId == 1);
    if (!isAdmin && distributorId <= 0) {
        // If dealer didn't specify distributor_id, get their own
        distributorId = getUserDistributorId(userId);
    }

    // Determine which filters are active
    bool hasDistributorFilter = (distributorId > 0);
    bool hasStatusFilter = (!status.empty() && OrderStates::isValid(status));

    // Build parameterized SQL with positional placeholders ($1, $2, ...)
    std::string whereClause;
    std::vector<std::string> whereParts;
    int paramIdx = 0;

    if (hasDistributorFilter) {
        whereParts.push_back("o.distributor_id = $" + std::to_string(++paramIdx));
    }
    if (hasStatusFilter) {
        whereParts.push_back("o.status = $" + std::to_string(++paramIdx));
    }

    if (!whereParts.empty()) {
        whereClause = " WHERE ";
        for (size_t i = 0; i < whereParts.size(); ++i) {
            if (i > 0) whereClause += " AND ";
            whereClause += whereParts[i];
        }
    }

    // Sort / pagination placeholders
    std::string limitPlaceholder = "$" + std::to_string(++paramIdx);
    std::string offsetPlaceholder = "$" + std::to_string(++paramIdx);

    std::string countSql = "SELECT COUNT(*) FROM orders o" + whereClause;
    std::string querySql =
        "SELECT o.id, o.order_no, o.distributor_id, d.name AS distributor_name, "
        "o.status, o.total_amount, o.final_amount, o.billing_cycle, "
        "(SELECT COUNT(*) FROM order_items oi WHERE oi.order_id = o.id) AS item_count, "
        "o.created_at "
        "FROM orders o "
        "LEFT JOIN distributors d ON d.id = o.distributor_id" +
        whereClause +
        " ORDER BY o.created_at DESC "
        "LIMIT " + limitPlaceholder + " OFFSET " + offsetPlaceholder;

    int offset = (page - 1) * perPage;

    // Dispatch to parameterized query based on active filters
    // Drogon's execSqlSync uses variadic templates, so we branch on filter
    // combinations to pass the correct parameters.
    auto execWithParams = [&](const std::string& sql) -> drogon::orm::Result {
        if (hasDistributorFilter && hasStatusFilter) {
            return db->execSqlSync(sql, distributorId, status, perPage, offset);
        } else if (hasDistributorFilter) {
            return db->execSqlSync(sql, distributorId, perPage, offset);
        } else if (hasStatusFilter) {
            return db->execSqlSync(sql, status, perPage, offset);
        } else {
            return db->execSqlSync(sql, perPage, offset);
        }
    };

    // Count total
    auto countSqlNoPage = [&]() -> std::string {
        if (hasDistributorFilter && hasStatusFilter) {
            return "SELECT COUNT(*) FROM orders o WHERE o.distributor_id = $1 AND o.status = $2";
        } else if (hasDistributorFilter) {
            return "SELECT COUNT(*) FROM orders o WHERE o.distributor_id = $1";
        } else if (hasStatusFilter) {
            return "SELECT COUNT(*) FROM orders o WHERE o.status = $1";
        } else {
            return "SELECT COUNT(*) FROM orders o";
        }
    };

    auto countResult = [&]() -> drogon::orm::Result {
        if (hasDistributorFilter && hasStatusFilter) {
            return db->execSqlSync(countSqlNoPage(), distributorId, status);
        } else if (hasDistributorFilter) {
            return db->execSqlSync(countSqlNoPage(), distributorId);
        } else if (hasStatusFilter) {
            return db->execSqlSync(countSqlNoPage(), status);
        } else {
            return db->execSqlSync(countSqlNoPage());
        }
    }();

    int64_t total = countResult.empty() ? 0 : countResult[0][0].as<int64_t>();

    // Fetch page
    auto rows = execWithParams(querySql);

    Json::Value items(Json::arrayValue);
    for (const auto& row : rows) {
        Json::Value item;
        item["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
        item["order_no"] = row["order_no"].as<std::string>();
        item["distributor_id"] = static_cast<Json::Int64>(row["distributor_id"].as<int64_t>());
        item["distributor_name"] = row["distributor_name"].isNull()
            ? "" : row["distributor_name"].as<std::string>();
        item["status"] = row["status"].as<std::string>();
        item["total_amount"] = row["total_amount"].as<std::string>();
        item["final_amount"] = row["final_amount"].as<std::string>();
        item["billing_cycle"] = row["billing_cycle"].as<std::string>();
        item["item_count"] = row["item_count"].as<int>();
        item["created_at"] = row["created_at"].as<std::string>();
        items.append(item);
    }

    Json::Value data;
    data["items"] = items;
    data["total"] = static_cast<Json::Int64>(total);
    data["page"] = page;
    data["per_page"] = perPage;
    return data;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Get Order Detail
// ═══════════════════════════════════════════════════════════════════════════

Json::Value OrderService::getOrderById(int64_t orderId, int64_t userId,
                                        int64_t roleId) {
    auto db = DbClient::getClient();

    // Fetch order
    std::string orderSql =
        "SELECT o.*, d.name AS distributor_name, "
        "u.username AS approved_by_name "
        "FROM orders o "
        "LEFT JOIN distributors d ON d.id = o.distributor_id "
        "LEFT JOIN users u ON u.id = o.approved_by "
        "WHERE o.id = $1";
    auto orderRows = db->execSqlSync(orderSql, orderId);

    if (orderRows.empty()) {
        return Json::nullValue;
    }

    auto row = orderRows[0];

    // Permission check: dealers can only see their own orders
    bool isAdmin = (roleId == 1);
    if (!isAdmin) {
        int64_t userDistId = getUserDistributorId(userId);
        int64_t orderDistId = row["distributor_id"].as<int64_t>();
        if (userDistId != orderDistId) {
            throw std::invalid_argument("Order not found");
        }
    }

    Json::Value order;
    order["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
    order["order_no"] = row["order_no"].as<std::string>();
    order["distributor_id"] = static_cast<Json::Int64>(row["distributor_id"].as<int64_t>());
    order["distributor_name"] = row["distributor_name"].isNull()
        ? "" : row["distributor_name"].as<std::string>();
    order["status"] = row["status"].as<std::string>();
    order["total_amount"] = row["total_amount"].as<std::string>();
    order["discount_amount"] = row["discount_amount"].as<std::string>();
    order["final_amount"] = row["final_amount"].as<std::string>();
    order["billing_cycle"] = row["billing_cycle"].as<std::string>();
    order["remark"] = row["remark"].isNull() ? "" : row["remark"].as<std::string>();
    order["approved_by"] = row["approved_by"].isNull()
        ? Json::nullValue : Json::Value(static_cast<Json::Int64>(row["approved_by"].as<int64_t>()));
    order["approved_by_name"] = row["approved_by_name"].isNull()
        ? "" : row["approved_by_name"].as<std::string>();
    order["approved_at"] = row["approved_at"].isNull()
        ? "" : row["approved_at"].as<std::string>();
    order["created_at"] = row["created_at"].as<std::string>();
    order["updated_at"] = row["updated_at"].as<std::string>();

    // Fetch order items
    auto itemRows = db->execSqlSync(
        "SELECT * FROM order_items WHERE order_id = $1", orderId);

    Json::Value items(Json::arrayValue);
    for (const auto& itemRow : itemRows) {
        Json::Value item;
        item["id"] = static_cast<Json::Int64>(itemRow["id"].as<int64_t>());
        item["product_id"] = static_cast<Json::Int64>(itemRow["product_id"].as<int64_t>());
        item["product_name"] = itemRow["product_name"].as<std::string>();
        if (!itemRow["product_specs"].isNull()) {
            std::string specsStr = itemRow["product_specs"].as<std::string>();
            Json::Reader reader;
            Json::Value specs;
            if (reader.parse(specsStr, specs)) {
                item["config"] = specs;
            }
        }
        item["quantity"] = itemRow["quantity"].as<int>();
        item["unit_price"] = itemRow["unit_price"].as<std::string>();
        item["subtotal"] = itemRow["subtotal"].as<std::string>();
        item["period_months"] = itemRow["period_months"].as<int>();
        items.append(item);
    }
    order["items"] = items;

    // Fetch timeline
    auto tlRows = db->execSqlSync(
        "SELECT * FROM order_timeline WHERE order_id = $1 "
        "ORDER BY created_at ASC", orderId);

    Json::Value timeline(Json::arrayValue);
    for (const auto& tlRow : tlRows) {
        Json::Value entry;
        entry["status"] = tlRow["to_status"].as<std::string>();
        entry["from_status"] = tlRow["from_status"].isNull()
            ? Json::nullValue : Json::Value(tlRow["from_status"].as<std::string>());
        entry["time"] = tlRow["created_at"].as<std::string>();
        entry["operator"] = tlRow["operator_name"].isNull()
            ? Json::nullValue : Json::Value(tlRow["operator_name"].as<std::string>());
        entry["remark"] = tlRow["remark"].isNull()
            ? "" : tlRow["remark"].as<std::string>();
        timeline.append(entry);
    }
    order["timeline"] = timeline;

    return order;
}

// ═══════════════════════════════════════════════════════════════════════════
//  State Transition
// ═══════════════════════════════════════════════════════════════════════════

Json::Value OrderService::transitionOrder(int64_t orderId,
                                           const std::string& toState,
                                           int64_t operatorId,
                                           const std::string& operatorName,
                                           int64_t roleId,
                                           const std::string& remark) {
    // Determine if operator is admin (role_id == 1) or dealer
    bool isAdmin = (roleId == 1);
    bool isDealer = !isAdmin;

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // Fetch current order state
        auto orderRows = trans.execSqlSync(
            "SELECT * FROM orders WHERE id = $1 FOR UPDATE", orderId);

        if (orderRows.empty()) {
            throw std::invalid_argument("Order not found");
        }

        auto row = orderRows[0];
        std::string currentState = row["status"].as<std::string>();
        int64_t distributorId = row["distributor_id"].as<int64_t>();

        // Validate the transition
        if (!OrderStateMachine::isValidTransition(currentState, toState)) {
            throw std::invalid_argument(
                "Invalid state transition: " + currentState + " → " + toState);
        }

        // Check permissions
        if (!OrderStateMachine::canTransition(currentState, toState,
                                               isAdmin, isDealer)) {
            throw std::invalid_argument(
                "Not authorized to perform transition: " + currentState +
                " → " + toState);
        }

        // For approve/reject, verify operator is admin
        std::string perm = OrderStateMachine::requiredPermission(currentState, toState);
        if (perm == "order:approve" || perm == "order:reject") {
            if (!isAdmin) {
                throw std::invalid_argument(
                    "Only administrators can approve or reject orders");
            }
        }

        // Update order status
        std::string updateSql;
        if (toState == OrderStates::kApproved) {
            // Set approved_by and approved_at
            updateSql =
                "UPDATE orders SET status = $1, approved_by = $2, "
                "approved_at = NOW(), updated_at = NOW() "
                "WHERE id = $3";
            trans.execSqlSync(updateSql, toState, operatorId, orderId);
        } else {
            updateSql =
                "UPDATE orders SET status = $1, updated_at = NOW() "
                "WHERE id = $2";
            trans.execSqlSync(updateSql, toState, orderId);
        }

        // Create default remark if empty
        std::string effRemark = remark;
        if (effRemark.empty()) {
            if (toState == OrderStates::kApproved) {
                effRemark = "订单审核通过";
            } else if (toState == OrderStates::kRejected) {
                effRemark = "订单被驳回";
            } else if (toState == OrderStates::kCancelled) {
                effRemark = "订单已取消";
            } else if (toState == OrderStates::kProvisioning) {
                effRemark = "开始开通服务";
            } else if (toState == OrderStates::kActive) {
                effRemark = "服务开通完成";
            } else if (toState == OrderStates::kSuspended) {
                effRemark = "服务已暂停";
            } else if (toState == OrderStates::kTerminated) {
                effRemark = "服务已终止";
            }
        }

        // Record timeline entry
        trans.execSqlSync(
            "INSERT INTO order_timeline (order_id, from_status, to_status, "
            "operator_id, operator_name, remark) "
            "VALUES ($1, $2, $3, $4, $5, $6)",
            orderId, currentState, toState, operatorId, operatorName, effRemark);

        // If approving, update subscriptions from pending to provisioning
        if (toState == OrderStates::kApproved) {
            // Get affected subscription IDs for timeline recording
            auto subRows = trans.execSqlSync(
                "SELECT id, sub_no FROM subscriptions "
                "WHERE order_id = $1 AND status = 'pending'", orderId);

            trans.execSqlSync(
                "UPDATE subscriptions SET status = 'provisioning', "
                "provision_status = 'provisioning', updated_at = NOW() "
                "WHERE order_id = $1 AND status = 'pending'",
                orderId);

            // Record subscription timeline entries
            for (const auto& srow : subRows) {
                trans.execSqlSync(
                    "INSERT INTO subscription_timeline "
                    "(subscription_id, from_status, to_status, "
                    "operator_id, operator_name, remark) "
                    "VALUES ($1, 'pending', 'provisioning', $2, $3, '订单审核通过，开始开通服务')",
                    srow["id"].as<int64_t>(), operatorId, operatorName);
            }
        }

        // If transitioning to active, update subscriptions
        if (toState == OrderStates::kActive) {
            // Get affected subscription IDs for timeline recording
            auto subRows = trans.execSqlSync(
                "SELECT id, sub_no, status FROM subscriptions "
                "WHERE order_id = $1", orderId);

            trans.execSqlSync(
                "UPDATE subscriptions SET status = 'active', "
                "provision_status = 'done', start_date = CURRENT_DATE, "
                "updated_at = NOW() "
                "WHERE order_id = $1",
                orderId);

            // Record subscription timeline entries
            for (const auto& srow : subRows) {
                trans.execSqlSync(
                    "INSERT INTO subscription_timeline "
                    "(subscription_id, from_status, to_status, "
                    "operator_id, operator_name, remark) "
                    "VALUES ($1, $2, 'active', $3, $4, '服务开通完成')",
                    srow["id"].as<int64_t>(),
                    srow["status"].as<std::string>(),
                    operatorId, operatorName);
            }
        }

        // If cancelled, update subscriptions accordingly
        if (toState == OrderStates::kCancelled || toState == OrderStates::kTerminated) {
            std::string subStatus = (toState == OrderStates::kCancelled)
                ? "cancelled" : "terminated";
            std::string tlRemark = (toState == OrderStates::kCancelled)
                ? "订单已取消，订阅同步取消" : "订单已终止，订阅同步终止";

            // Get affected subscription IDs for timeline recording
            auto subRows = trans.execSqlSync(
                "SELECT id, sub_no, status FROM subscriptions "
                "WHERE order_id = $1", orderId);

            trans.execSqlSync(
                "UPDATE subscriptions SET status = $1, updated_at = NOW() "
                "WHERE order_id = $2",
                subStatus, orderId);

            // Record subscription timeline entries
            for (const auto& srow : subRows) {
                trans.execSqlSync(
                    "INSERT INTO subscription_timeline "
                    "(subscription_id, from_status, to_status, "
                    "operator_id, operator_name, remark) "
                    "VALUES ($1, $2, $3, $4, $5, $6)",
                    srow["id"].as<int64_t>(),
                    srow["status"].as<std::string>(),
                    subStatus,
                    operatorId, operatorName,
                    tlRemark);
            }
        }

        // Build result
        result["id"] = static_cast<Json::Int64>(orderId);
        result["order_no"] = row["order_no"].as<std::string>();
        result["status"] = toState;
        result["previous_status"] = currentState;
    });

    return result;
}

} // namespace idc
