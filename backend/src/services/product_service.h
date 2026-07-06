#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace idc {

/// Product types supported by the IDC platform.
inline const std::vector<std::string>& kProductTypes() {
    static const std::vector<std::string> types = {
        "bare_metal", "cloud", "bandwidth", "ip", "addon", "rack"
    };
    return types;
}

/// Business logic for product CRUD, search, and spec validation.
class ProductService {
public:
    // ── Queries ───────────────────────────────────────────────────────────────

    /// List products with optional type filter and keyword search (paginated).
    /// Returns JSON: { items: [...], total, page, per_page }
    static Json::Value listProducts(const std::string& type,
                                    const std::string& keyword,
                                    int page, int per_page);

    /// Get a single product by id. Returns null value if not found.
    static Json::Value getProductById(int64_t id);

    /// Return the list of valid product type strings.
    static Json::Value getProductTypes();

    // ── Mutations (admin only) ────────────────────────────────────────────────

    /// Create a new product. Returns the new product id.
    /// Throws std::invalid_argument on validation failure.
    static int64_t createProduct(const std::string& name,
                                 const std::string& type,
                                 const std::string& description,
                                 const Json::Value& specs,
                                 int32_t sortOrder);

    /// Update an existing product. Throws if not found or validation fails.
    static void updateProduct(int64_t id,
                              const std::string& name,
                              const std::string& type,
                              const std::string& description,
                              const Json::Value& specs,
                              int16_t status,
                              int32_t sortOrder);

    /// Soft-delete a product (set status = 0).
    static void deleteProduct(int64_t id);

    // ── Validation ────────────────────────────────────────────────────────────

    /// Validate specs JSON against the product type requirements.
    /// Returns an empty string on success, or an error message on failure.
    static std::string validateSpecs(const std::string& type,
                                     const Json::Value& specs);
};

} // namespace idc
