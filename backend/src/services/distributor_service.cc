#include "distributor_service.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <json/json.h>
#include <cstdint>

#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════════

/// Build a paginated response envelope { items, total, page, per_page }.
static Json::Value paginatedResponse(const Json::Value& items,
                                     int64_t total,
                                     int page, int per_page) {
    Json::Value data;
    data["items"]    = items;
    data["total"]    = static_cast<Json::Int64>(total);
    data["page"]     = page;
    data["per_page"] = per_page;
    return data;
}

/// Convert a Result row to a distributor JSON object (basic fields).
static Json::Value rowToDistributor(const drogon::orm::Row& row) {
    Json::Value d;
    d["id"]              = static_cast<Json::Int64>(row["id"].as<int64_t>());
    d["name"]            = row["name"].as<std::string>();
    d["level"]           = row["level"].as<int16_t>();
    d["parent_id"]       = row["parent_id"].isNull()
        ? Json::Value(Json::nullValue)
        : Json::Value(static_cast<Json::Int64>(row["parent_id"].as<int64_t>()));
    d["contact_person"]  = row["contact_person"].as<std::string>();
    d["contact_phone"]   = row["contact_phone"].as<std::string>();
    d["contact_email"]   = row["contact_email"].as<std::string>();
    d["balance"]         = row["balance"].as<std::string>();
    d["credit_limit"]    = row["credit_limit"].as<std::string>();
    d["status"]          = row["status"].as<int16_t>();
    d["remark"]          = row["remark"].as<std::string>();

    // Optional fields
    if (!row["price_template_id"].isNull()) {
        d["price_template_id"] = static_cast<Json::Int64>(
            row["price_template_id"].as<int64_t>());
    }

    // Timestamps
    auto created = row["created_at"].as<std::string>();
    auto updated = row["updated_at"].as<std::string>();
    d["created_at"] = created.substr(0, 19) + "Z";
    d["updated_at"] = updated.substr(0, 19) + "Z";

    return d;
}

/// Escape single-quotes for inline SQL string values (used in ILIKE).
static std::string escapeKeyword(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\'') { out.insert(i, "'"); ++i; }
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Paginated list
// ═══════════════════════════════════════════════════════════════════════════

Json::Value DistributorService::listDistributors(
    int page, int per_page,
    int16_t status, int16_t level,
    const std::string& keyword,
    int64_t parentId) {

    auto db = DbClient::getClient();

    // Build WHERE clause
    std::string where = "WHERE d.status >= 0";  // exclude deleted

    if (status >= 0) {
        where += " AND d.status = " + std::to_string(status);
    }
    if (level > 0) {
        where += " AND d.level = " + std::to_string(level);
    }
    if (!keyword.empty()) {
        std::string kw = escapeKeyword(keyword);
        where += " AND (d.name ILIKE '%" + kw + "%'"
                 " OR d.contact_phone ILIKE '%" + kw + "%'"
                 " OR d.contact_email ILIKE '%" + kw + "%')";
    }
    if (parentId > 0) {
        where += " AND d.parent_id = " + std::to_string(parentId);
    } else if (parentId == 0) {
        // parentId=0 means top-level only (no parent)
        where += " AND d.parent_id IS NULL";
    }
    // parentId < 0 means no filter on parent_id

    // Count total
    auto countResult = db->execSqlSync(
        "SELECT COUNT(*) FROM distributors d " + where);
    int64_t total = countResult[0][0].as<int64_t>();

    // Fetch page with children_count subquery
    int offset = (page - 1) * per_page;
    std::string dataSql =
        "SELECT d.*,"
        "  (SELECT COUNT(*) FROM distributors ch WHERE ch.parent_id = d.id) AS children_count"
        " FROM distributors d " + where +
        " ORDER BY d.id DESC LIMIT $1 OFFSET $2";

    auto dataResult = db->execSqlSync(dataSql, per_page, offset);

    Json::Value items(Json::arrayValue);
    for (const auto& row : dataResult) {
        Json::Value item = rowToDistributor(row);
        item["children_count"] = row["children_count"].as<int64_t>();
        // Also compute aggregate stats for list display
        int64_t distId = row["id"].as<int64_t>();

        // Quick order count & revenue for this distributor
        try {
            auto stats = db->execSqlSync(
                "SELECT COUNT(*) AS cnt,"
                "  COALESCE(SUM(final_amount), 0) AS revenue"
                " FROM orders WHERE distributor_id = $1 AND status NOT IN ('cancelled','rejected')",
                distId);
            if (!stats.empty()) {
                item["order_count"]   = static_cast<Json::Int64>(stats[0]["cnt"].as<int64_t>());
                item["total_revenue"] = stats[0]["revenue"].as<std::string>();
            } else {
                item["order_count"]   = 0;
                item["total_revenue"] = "0.00";
            }
        } catch (...) {
            item["order_count"]   = 0;
            item["total_revenue"] = "0.00";
        }

        items.append(item);
    }

    return paginatedResponse(items, total, page, per_page);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Get by ID (detail with aggregate stats)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value DistributorService::getDistributorById(int64_t id) {
    auto db = DbClient::getClient();

    auto result = db->execSqlSync(
        "SELECT d.*, dp.name AS parent_name"
        " FROM distributors d"
        " LEFT JOIN distributors dp ON dp.id = d.parent_id"
        " WHERE d.id = $1", id);

    if (result.empty()) {
        return Json::nullValue;
    }

    auto row = result[0];
    Json::Value item = rowToDistributor(row);
    item["parent_name"] = row["parent_name"].isNull()
        ? Json::Value(Json::nullValue)
        : Json::Value(row["parent_name"].as<std::string>());

    // Children count
    auto childCnt = db->execSqlSync(
        "SELECT COUNT(*) FROM distributors WHERE parent_id = $1 AND status >= 0",
        id);
    item["children_count"] = childCnt[0][0].as<int64_t>();

    // Aggregate stats from related tables
    // Order count & revenue
    try {
        auto orderStats = db->execSqlSync(
            "SELECT COUNT(*) AS cnt,"
            "  COALESCE(SUM(final_amount), 0) AS revenue"
            " FROM orders WHERE distributor_id = $1"
            "   AND status NOT IN ('cancelled','rejected')",
            id);
        item["order_count"]   = static_cast<Json::Int64>(orderStats[0]["cnt"].as<int64_t>());
        item["total_revenue"] = orderStats[0]["revenue"].as<std::string>();
    } catch (...) {
        item["order_count"]   = 0;
        item["total_revenue"] = "0.00";
    }

    // Active subscriptions
    try {
        auto subStats = db->execSqlSync(
            "SELECT COUNT(*) FROM subscriptions"
            " WHERE distributor_id = $1 AND status = 'active'",
            id);
        item["active_subscriptions"] = subStats[0][0].as<int64_t>();
    } catch (...) {
        item["active_subscriptions"] = 0;
    }

    // Overdue invoices
    try {
        auto overdueStats = db->execSqlSync(
            "SELECT COUNT(*) FROM invoices"
            " WHERE distributor_id = $1"
            "   AND status = 'unpaid'"
            "   AND due_date < CURRENT_DATE",
            id);
        item["overdue_invoices"] = overdueStats[0][0].as<int64_t>();
    } catch (...) {
        item["overdue_invoices"] = 0;
    }

    return item;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Create
// ═══════════════════════════════════════════════════════════════════════════

int64_t DistributorService::createDistributor(const Json::Value& body) {
    auto db = DbClient::getClient();

    // Extract fields with defaults
    std::string name = body["name"].asString();
    if (name.empty()) {
        throw std::invalid_argument("Name is required");
    }

    int16_t level = body.isMember("level") ? body["level"].asInt() : 1;
    if (level < 1 || level > 5) {
        throw std::invalid_argument("Level must be between 1 and 5");
    }

    int64_t parentId = 0;
    if (body.isMember("parent_id") && !body["parent_id"].isNull()) {
        parentId = static_cast<int64_t>(body["parent_id"].asInt64());

        // Verify parent exists
        auto parentCheck = db->execSqlSync(
            "SELECT id, status FROM distributors WHERE id = $1", parentId);
        if (parentCheck.empty()) {
            throw std::invalid_argument("Parent distributor not found: id="
                                        + std::to_string(parentId));
        }
        if (parentCheck[0]["status"].as<int16_t>() != 1) {
            throw std::invalid_argument("Parent distributor is not active");
        }
    }

    // Validate no duplicate name (soft-deleted names are OK to reuse)
    auto nameCheck = db->execSqlSync(
        "SELECT id FROM distributors WHERE name = $1 AND status >= 0", name);
    if (!nameCheck.empty()) {
        throw std::invalid_argument("Distributor name already exists: " + name);
    }

    std::string contactPerson = body.isMember("contact_person")
        ? body["contact_person"].asString() : "";
    std::string contactPhone  = body.isMember("contact_phone")
        ? body["contact_phone"].asString() : "";
    std::string contactEmail  = body.isMember("contact_email")
        ? body["contact_email"].asString() : "";
    std::string creditLimit   = body.isMember("credit_limit")
        ? body["credit_limit"].asString() : "0.00";
    std::string remark        = body.isMember("remark")
        ? body["remark"].asString() : "";
    int16_t status            = body.isMember("status")
        ? body["status"].asInt() : 1;

    int64_t priceTemplateId = 0;
    if (body.isMember("price_template_id") && !body["price_template_id"].isNull()) {
        priceTemplateId = static_cast<int64_t>(body["price_template_id"].asInt64());
    }

    auto result = db->execSqlSync(
        "INSERT INTO distributors"
        " (name, level, parent_id, price_template_id,"
        "  contact_person, contact_phone, contact_email,"
        "  credit_limit, status, remark)"
        " VALUES ($1, $2, $3, $4, $5, $6, $7, $8::decimal, $9, $10)"
        " RETURNING id",
        name, level,
        parentId > 0 ? std::make_optional(parentId) : std::nullopt,
        priceTemplateId > 0 ? std::make_optional(priceTemplateId) : std::nullopt,
        contactPerson, contactPhone, contactEmail,
        creditLimit, status, remark);

    return result[0]["id"].as<int64_t>();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Update
// ═══════════════════════════════════════════════════════════════════════════

void DistributorService::updateDistributor(int64_t id, const Json::Value& body) {
    auto db = DbClient::getClient();

    // Verify existence
    auto existing = db->execSqlSync(
        "SELECT id, parent_id FROM distributors WHERE id = $1 AND status >= 0",
        id);
    if (existing.empty()) {
        throw std::invalid_argument("Distributor not found: id=" + std::to_string(id));
    }

    // Fetch current values
    auto current = db->execSqlSync("SELECT * FROM distributors WHERE id = $1", id);
    if (current.empty()) return; // shouldn't happen

    std::string name        = body.isMember("name")
        ? body["name"].asString() : current[0]["name"].as<std::string>();
    int16_t level           = body.isMember("level")
        ? body["level"].asInt() : current[0]["level"].as<int16_t>();
    std::string contactPerson = body.isMember("contact_person")
        ? body["contact_person"].asString() : current[0]["contact_person"].as<std::string>();
    std::string contactPhone = body.isMember("contact_phone")
        ? body["contact_phone"].asString() : current[0]["contact_phone"].as<std::string>();
    std::string contactEmail = body.isMember("contact_email")
        ? body["contact_email"].asString() : current[0]["contact_email"].as<std::string>();
    std::string creditLimit  = body.isMember("credit_limit")
        ? body["credit_limit"].asString() : current[0]["credit_limit"].as<std::string>();
    std::string remark       = body.isMember("remark")
        ? body["remark"].asString() : current[0]["remark"].as<std::string>();
    int16_t status           = body.isMember("status")
        ? body["status"].asInt() : current[0]["status"].as<int16_t>();

    // Handle parent_id
    std::optional<int64_t> parentId;
    if (body.isMember("parent_id")) {
        if (body["parent_id"].isNull()) {
            parentId = std::nullopt;
        } else {
            int64_t newParent = static_cast<int64_t>(body["parent_id"].asInt64());
            if (newParent == id) {
                throw std::invalid_argument("A distributor cannot be its own parent");
            }
            // Circular reference check
            if (!validateParentAssignment(id, newParent)) {
                throw std::invalid_argument("Circular reference detected: new parent is a descendant");
            }
            // Verify parent exists
            auto parentCheck = db->execSqlSync(
                "SELECT id FROM distributors WHERE id = $1 AND status >= 0", newParent);
            if (parentCheck.empty()) {
                throw std::invalid_argument("Parent distributor not found");
            }
            parentId = newParent;
        }
    } else {
        // Keep current parent_id
        if (!current[0]["parent_id"].isNull()) {
            parentId = current[0]["parent_id"].as<int64_t>();
        }
    }

    // Handle price_template_id
    std::optional<int64_t> priceTemplateId;
    if (body.isMember("price_template_id")) {
        if (body["price_template_id"].isNull()) {
            priceTemplateId = std::nullopt;
        } else {
            priceTemplateId = static_cast<int64_t>(body["price_template_id"].asInt64());
        }
    } else if (!current[0]["price_template_id"].isNull()) {
        priceTemplateId = current[0]["price_template_id"].as<int64_t>();
    }

    // Check duplicate name (exclude self)
    auto nameCheck = db->execSqlSync(
        "SELECT id FROM distributors WHERE name = $1 AND id != $2 AND status >= 0",
        name, id);
    if (!nameCheck.empty()) {
        throw std::invalid_argument("Distributor name already exists: " + name);
    }

    if (level < 1 || level > 5) {
        throw std::invalid_argument("Level must be between 1 and 5");
    }

    db->execSqlSync(
        "UPDATE distributors SET"
        "  name=$1, level=$2, parent_id=$3, price_template_id=$4,"
        "  contact_person=$5, contact_phone=$6, contact_email=$7,"
        "  credit_limit=$8::decimal, status=$9, remark=$10,"
        "  updated_at=NOW()"
        " WHERE id=$11",
        name, level, parentId, priceTemplateId,
        contactPerson, contactPhone, contactEmail,
        creditLimit, status, remark, id);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Tree — WITH RECURSIVE CTE
// ═══════════════════════════════════════════════════════════════════════════

Json::Value DistributorService::getDistributorTree(int64_t id) {
    auto db = DbClient::getClient();

    // Verify root exists
    auto rootCheck = db->execSqlSync(
        "SELECT id, name, status FROM distributors WHERE id = $1", id);
    if (rootCheck.empty()) {
        return Json::nullValue;
    }

    // WITH RECURSIVE CTE — PostgreSQL
    auto rows = db->execSqlSync(
        "WITH RECURSIVE tree AS ("
        "  SELECT id, name, parent_id, level, status"
        "  FROM distributors WHERE id = $1"
        "  UNION ALL"
        "  SELECT d.id, d.name, d.parent_id, d.level, d.status"
        "  FROM distributors d"
        "  JOIN tree t ON d.parent_id = t.id"
        "  WHERE d.status >= 0"
        ") SELECT * FROM tree ORDER BY id",
        id);

    if (rows.empty()) {
        return Json::nullValue;
    }

    // Build nested JSON from flat rows
    Json::Value rootNode;
    rootNode["id"]   = static_cast<Json::Int64>(rows[0]["id"].as<int64_t>());
    rootNode["name"] = rows[0]["name"].as<std::string>();
    rootNode["level"]= rows[0]["level"].as<int16_t>();
    rootNode["children"] = Json::Value(Json::arrayValue);

    // Index all nodes by parent_id for O(n) tree building
    std::unordered_map<int64_t, Json::Value*> nodeMap;
    nodeMap[rows[0]["id"].as<int64_t>()] = &rootNode;

    for (size_t i = 1; i < rows.size(); ++i) {
        int64_t nodeId   = rows[i]["id"].as<int64_t>();
        int64_t parentId = rows[i]["parent_id"].as<int64_t>();

        Json::Value child;
        child["id"]       = static_cast<Json::Int64>(nodeId);
        child["name"]     = rows[i]["name"].as<std::string>();
        child["level"]    = rows[i]["level"].as<int16_t>();
        child["status"]   = rows[i]["status"].as<int16_t>();
        child["children"] = Json::Value(Json::arrayValue);

        // Insert into parent's children array
        auto it = nodeMap.find(parentId);
        if (it != nodeMap.end()) {
            it->second->append(child);
            // Point the newly appended entry's pointer
            nodeMap[nodeId] = &((*it->second)[it->second->size() - 1]);
        }
    }

    return rootNode;
}

Json::Value DistributorService::getDirectChildren(int64_t id) {
    auto db = DbClient::getClient();

    auto rows = db->execSqlSync(
        "SELECT d.* FROM distributors d"
        " WHERE d.parent_id = $1 AND d.status >= 0"
        " ORDER BY d.id ASC",
        id);

    Json::Value arr(Json::arrayValue);
    for (const auto& row : rows) {
        arr.append(rowToDistributor(row));
    }
    return arr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Statistics
// ═══════════════════════════════════════════════════════════════════════════

Json::Value DistributorService::getDistributorStatistics(int64_t id) {
    auto db = DbClient::getClient();

    // Verify existence
    auto exists = db->execSqlSync(
        "SELECT id FROM distributors WHERE id = $1 AND status >= 0", id);
    if (exists.empty()) {
        return Json::nullValue;
    }

    Json::Value stats;

    // Orders this month
    try {
        auto orderStats = db->execSqlSync(
            "SELECT COUNT(*) AS cnt, COALESCE(SUM(final_amount), 0) AS revenue"
            " FROM orders"
            " WHERE distributor_id = $1"
            "   AND date_trunc('month', created_at) = date_trunc('month', NOW())"
            "   AND status NOT IN ('cancelled','rejected')",
            id);
        stats["orders_this_month"]   = static_cast<Json::Int64>(orderStats[0]["cnt"].as<int64_t>());
        stats["revenue_this_month"]  = orderStats[0]["revenue"].as<std::string>();
    } catch (...) {
        stats["orders_this_month"]   = 0;
        stats["revenue_this_month"]  = "0.00";
    }

    // Active subscriptions
    try {
        auto subResult = db->execSqlSync(
            "SELECT COUNT(*) FROM subscriptions"
            " WHERE distributor_id = $1 AND status = 'active'",
            id);
        stats["subscription_count"] = subResult[0][0].as<int64_t>();
    } catch (...) {
        stats["subscription_count"] = 0;
    }

    // Overdue invoices
    try {
        auto overdueResult = db->execSqlSync(
            "SELECT COUNT(*) FROM invoices"
            " WHERE distributor_id = $1"
            "   AND status = 'unpaid'"
            "   AND due_date < CURRENT_DATE",
            id);
        stats["overdue_invoice_count"] = overdueResult[0][0].as<int64_t>();
    } catch (...) {
        stats["overdue_invoice_count"] = 0;
    }

    // Total all-time order count and revenue
    try {
        auto allTime = db->execSqlSync(
            "SELECT COUNT(*) AS cnt, COALESCE(SUM(final_amount), 0) AS revenue"
            " FROM orders"
            " WHERE distributor_id = $1 AND status NOT IN ('cancelled','rejected')",
            id);
        stats["total_orders"]   = static_cast<Json::Int64>(allTime[0]["cnt"].as<int64_t>());
        stats["total_revenue"]  = allTime[0]["revenue"].as<std::string>();
    } catch (...) {
        stats["total_orders"]   = 0;
        stats["total_revenue"]  = "0.00";
    }

    return stats;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Private helpers
// ═══════════════════════════════════════════════════════════════════════════

bool DistributorService::validateParentAssignment(int64_t distributorId,
                                                   int64_t newParentId) {
    // If newParentId is already a descendant of distributorId, that's circular.
    // Use WITH RECURSIVE to walk up from newParentId to see if we hit distributorId.
    // But actually we need to check: is distributorId an ancestor of newParentId?
    // Walk parent chain from newParentId up to root.
    auto db = DbClient::getClient();

    std::unordered_set<int64_t> ancestors;
    int64_t current = newParentId;
    constexpr int64_t kMaxDepth = 100; // safety limit

    for (int i = 0; i < kMaxDepth; ++i) {
        if (current == distributorId) {
            return false; // circular: distributorId is an ancestor of newParentId
        }
        auto row = db->execSqlSync(
            "SELECT parent_id FROM distributors WHERE id = $1 AND status >= 0",
            current);
        if (row.empty() || row[0]["parent_id"].isNull()) {
            break; // reached root
        }
        current = row[0]["parent_id"].as<int64_t>();
        if (ancestors.count(current)) {
            break; // already visited — existing cycle in data (shouldn't happen)
        }
        ancestors.insert(current);
    }

    return true; // no circular reference
}

Json::Value DistributorService::buildTreeFromRows(const Json::Value& rows,
                                                   int64_t rootId) {
    // Not used directly — see getDistributorTree for inline implementation
    (void)rows;
    (void)rootId;
    return Json::nullValue;
}

void DistributorService::attachChildren(Json::Value& node,
                                        const Json::Value& rows,
                                        int64_t nodeId) {
    // Not used directly — see getDistributorTree for inline implementation
    (void)node;
    (void)rows;
    (void)nodeId;
}

} // namespace idc
