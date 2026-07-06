#pragma once

#include <json/json.h>
#include <string>

namespace idc {

/// Result of a price lookup including the source of the price.
struct PriceResult {
    Json::Value priceData; ///< { monthly_price, yearly_price, ... } or null
    std::string source;    ///< "system_default" | "price_template:N"
};

/// Pricing logic with multi-tier template inheritance.
///
/// Inheritance chain:
///   dealer's template → parent template → ... → system default template
///
/// The system default template is the one with `distributor_id IS NULL`.
class PricingService {
public:
    /// Get the effective price for a user.
    /// - Admin (role_id == 1) → system default price
    /// - Dealer → walks the inheritance chain from their distributor's template
    static Json::Value getPriceForUser(int64_t userId, int64_t roleId,
                                       int64_t productId);

    /// Walk the price inheritance chain for a specific distributor.
    /// Returns the first price found in the chain, or system default.
    static PriceResult getPriceForDistributor(int64_t distributorId,
                                              int64_t productId);

    /// Get the system default price (from template with distributor_id IS NULL).
    static Json::Value getSystemDefaultPrice(int64_t productId);

    /// Get the product name by id (helper).
    static std::string getProductName(int64_t productId);
};

} // namespace idc
