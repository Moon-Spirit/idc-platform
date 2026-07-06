#include "product_service.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <json/json.h>
#include <cstdint>

#include <sstream>
#include <stdexcept>

namespace idc {

// ── Helpers ──────────────────────────────────────────────────────────────────

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

/// Convert a Result row to a product JSON object.
static Json::Value rowToProduct(const drogon::orm::Row& row) {
    Json::Value prod;
    prod["id"]          = static_cast<Json::Int64>(row["id"].as<int64_t>());
    prod["name"]        = row["name"].as<std::string>();
    prod["type"]        = row["type"].as<std::string>();
    prod["description"] = row["description"].as<std::string>();

    // Parse specs JSONB
    Json::Value specs;
    Json::Reader reader;
    if (reader.parse(row["specs"].as<std::string>(), specs)) {
        prod["specs"] = specs;
    } else {
        prod["specs"] = Json::objectValue;
    }

    prod["status"]     = row["status"].as<int16_t>();
    prod["sort_order"] = row["sort_order"].as<int32_t>();

    // Timestamps (truncate sub-second precision for brevity)
    auto created = row["created_at"].as<std::string>();
    auto updated = row["updated_at"].as<std::string>();
    prod["created_at"] = created.substr(0, 19) + "Z";
    prod["updated_at"] = updated.substr(0, 19) + "Z";

    return prod;
}

// ── ProductService implementation ────────────────────────────────────────────

Json::Value ProductService::listProducts(const std::string& type,
                                          const std::string& keyword,
                                          int page, int per_page) {
    auto db = DbClient::getClient();

    // Build WHERE clause with inline string values.
    // type is validated against kProductTypes() — safe for inline use.
    // keyword is escaped for ILIKE patterns.
    std::string where = "WHERE p.status = 1";

    if (!type.empty()) {
        where += " AND p.type = '" + type + "'";
    }
    if (!keyword.empty()) {
        std::string kw = keyword;
        for (size_t i = 0; i < kw.size(); ++i) {
            if (kw[i] == '\'') { kw.insert(i, "'"); ++i; }
        }
        where += " AND (p.name ILIKE '%" + kw + "%' OR p.description ILIKE '%" + kw + "%')";
    }

    // Count total
    auto countResult = db->execSqlSync(
        "SELECT COUNT(*) FROM products p " + where);
    int64_t total = countResult[0][0].as<int64_t>();

    // Fetch page (LIMIT/OFFSET use parameterized queries)
    int offset = (page - 1) * per_page;
    std::string dataSql = "SELECT p.* FROM products p " + where +
                          " ORDER BY p.sort_order ASC, p.id DESC LIMIT $1 OFFSET $2";

    auto dataResult = db->execSqlSync(dataSql, per_page, offset);

    Json::Value items(Json::arrayValue);
    for (const auto& row : dataResult) {
        items.append(rowToProduct(row));
    }

    return paginatedResponse(items, total, page, per_page);
}

Json::Value ProductService::getProductById(int64_t id) {
    auto db = DbClient::getClient();
    auto result = db->execSqlSync(
        "SELECT * FROM products WHERE id = $1 AND status = 1", id);

    if (result.empty()) {
        return Json::nullValue;
    }
    return rowToProduct(result[0]);
}

Json::Value ProductService::getProductTypes() {
    Json::Value arr(Json::arrayValue);
    for (const auto& t : kProductTypes()) {
        arr.append(t);
    }
    return arr;
}

int64_t ProductService::createProduct(const std::string& name,
                                       const std::string& type,
                                       const std::string& description,
                                       const Json::Value& specs,
                                       int32_t sortOrder) {
    // Validate type
    bool validType = false;
    for (const auto& t : kProductTypes()) {
        if (t == type) { validType = true; break; }
    }
    if (!validType) {
        throw std::invalid_argument("Invalid product type: " + type);
    }

    // Validate specs
    std::string specErr = validateSpecs(type, specs);
    if (!specErr.empty()) {
        throw std::invalid_argument(specErr);
    }

    auto db = DbClient::getClient();
    auto result = db->execSqlSync(
        "INSERT INTO products (name, type, description, specs, sort_order) "
        "VALUES ($1, $2, $3, $4::jsonb, $5) RETURNING id",
        name, type, description,
        Json::FastWriter().write(specs),
        sortOrder);

    return result[0]["id"].as<int64_t>();
}

void ProductService::updateProduct(int64_t id,
                                    const std::string& name,
                                    const std::string& type,
                                    const std::string& description,
                                    const Json::Value& specs,
                                    int16_t status,
                                    int32_t sortOrder) {
    // Validate existence
    auto db = DbClient::getClient();
    auto existing = db->execSqlSync(
        "SELECT id FROM products WHERE id = $1", id);
    if (existing.empty()) {
        throw std::invalid_argument("Product not found: id=" + std::to_string(id));
    }

    // Validate type
    bool validType = false;
    for (const auto& t : kProductTypes()) {
        if (t == type) { validType = true; break; }
    }
    if (!validType) {
        throw std::invalid_argument("Invalid product type: " + type);
    }

    // Validate specs
    std::string specErr = validateSpecs(type, specs);
    if (!specErr.empty()) {
        throw std::invalid_argument(specErr);
    }

    db->execSqlSync(
        "UPDATE products SET name=$1, type=$2, description=$3, specs=$4::jsonb, "
        "status=$5, sort_order=$6, updated_at=NOW() WHERE id=$7",
        name, type, description,
        Json::FastWriter().write(specs),
        status, sortOrder, id);
}

void ProductService::deleteProduct(int64_t id) {
    auto db = DbClient::getClient();
    auto result = db->execSqlSync(
        "UPDATE products SET status=0, updated_at=NOW() WHERE id=$1 RETURNING id",
        id);
    if (result.empty()) {
        throw std::invalid_argument("Product not found: id=" + std::to_string(id));
    }
}

std::string ProductService::validateSpecs(const std::string& type,
                                           const Json::Value& specs) {
    if (!specs.isObject()) {
        return "specs must be a JSON object";
    }

    if (type == "bare_metal") {
        // Required: cpu, cores, ram, disk
        if (!specs.isMember("cpu") || !specs["cpu"].isString())
            return "bare_metal requires 'cpu' (string)";
        if (!specs.isMember("cores") || !specs["cores"].isInt())
            return "bare_metal requires 'cores' (integer)";
        if (!specs.isMember("ram") || !specs["ram"].isString())
            return "bare_metal requires 'ram' (string)";
        if (!specs.isMember("disk") || !specs["disk"].isString())
            return "bare_metal requires 'disk' (string)";
    } else if (type == "cloud") {
        if (!specs.isMember("cpu_cores") || !specs["cpu_cores"].isInt())
            return "cloud requires 'cpu_cores' (integer)";
        if (!specs.isMember("ram_mb") || !specs["ram_mb"].isInt())
            return "cloud requires 'ram_mb' (integer)";
        if (!specs.isMember("disk_gb") || !specs["disk_gb"].isInt())
            return "cloud requires 'disk_gb' (integer)";
    } else if (type == "bandwidth") {
        if (!specs.isMember("port_speed") || !specs["port_speed"].isString())
            return "bandwidth requires 'port_speed' (string)";
    } else if (type == "ip") {
        if (!specs.isMember("ip_count") || !specs["ip_count"].isInt())
            return "ip requires 'ip_count' (integer)";
        if (!specs.isMember("subnet") || !specs["subnet"].isString())
            return "ip requires 'subnet' (string)";
    } else if (type == "addon") {
        // Addons are flexible; no strict requirements
    } else if (type == "rack") {
        if (!specs.isMember("size") || !specs["size"].isString())
            return "rack requires 'size' (string)";
        if (!specs.isMember("power_kw") || !specs["power_kw"].isNumeric())
            return "rack requires 'power_kw' (number)";
    }

    return ""; // valid
}

} // namespace idc
