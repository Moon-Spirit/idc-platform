#pragma once

#include <json/json.h>
#include <string>
#include <vector>

// Forward declarations
namespace drogon {
namespace orm {
class Transaction;
} // namespace orm
} // namespace drogon

namespace idc {

/// Business logic for order lifecycle management.
///
/// Handles:
///   - Order number generation (ORD{YYMMDD}-{4-digit-seq})
///   - Cart-to-order conversion in a DB transaction
///   - State machine transitions with permission validation
///   - Order timeline recording for audit trail
///   - Order listing with pagination and status filters
///
/// On order approval, creates Subscriptions automatically.
/// Price snapshots are stored at order creation time (unit_price on order_items).
class OrderService {
public:
    // ── Order number generation ─────────────────────────────────────────────

    /// Generate the next order number in format ORD{YYMMDD}-{4-digit-seq}.
    /// Uses DB sequence query: SELECT COALESCE(MAX(seq), 0)+1 FROM today's orders.
    /// Safe within a transaction.
    static std::string generateOrderNo(drogon::orm::Transaction& trans);

    /// Generate the next subscription number in format SUB{YYMMDD}-{4-digit-seq}.
    static std::string generateSubNo(drogon::orm::Transaction& trans);

    // ── Order creation ──────────────────────────────────────────────────────

    /// Submit a new order from the current user's cart.
    ///
    /// Performs in a single DB transaction:
    ///   1. Load cart items from Redis
    ///   2. Validate each product exists
    ///   3. Look up prices via PricingService
    ///   4. Generate order_no
    ///   5. INSERT into orders + order_items
    ///   6. Clear cart from Redis
    ///   7. INSERT into subscriptions (one per order item)
    ///   8. INSERT initial order_timeline entry
    ///
    /// @param userId       Current user ID (from JWT)
    /// @param roleId       Current user role ID
    /// @param distributorId Distributor ID for pricing lookup
    /// @param billingCycle Override billing cycle ("monthly"/"yearly"), or empty
    ///                     to use per-item period
    /// @param remark       Optional order remark
    /// @return JSON with order_id, order_no, status
    /// @throws std::invalid_argument on validation failure
    /// @throws std::runtime_error on DB/Redis errors
    static Json::Value createOrder(int64_t userId, int64_t roleId,
                                   int64_t distributorId,
                                   const std::string& billingCycle,
                                   const std::string& remark);

    // ── Order queries ───────────────────────────────────────────────────────

    /// Paginated order list with optional status and distributor filters.
    /// Returns JSON: { items: [...], total, page, per_page }
    ///
    /// If `userId` is a dealer (role != admin), results are filtered to their
    /// distributor_id. Admin sees all.
    static Json::Value listOrders(int64_t userId, int64_t roleId,
                                  const std::string& status,
                                  int64_t distributorId,
                                  int page, int perPage);

    /// Get order detail by id, including items and timeline.
    /// Returns null value if not found.
    static Json::Value getOrderById(int64_t orderId, int64_t userId,
                                    int64_t roleId);

    // ── State transitions ───────────────────────────────────────────────────

    /// Transition an order from one state to another.
    ///
    /// Records the transition in order_timeline and updates order.status.
    /// If transitioning to "approved", also creates subscriptions.
    ///
    /// @param orderId   Order ID
    /// @param toState   Target state
    /// @param operatorId   User performing the action
    /// @param operatorName Username snapshot
    /// @param roleId    Role ID for permission check
    /// @param remark    Optional note
    /// @return Updated order JSON
    /// @throws std::invalid_argument on invalid transition or permissions
    static Json::Value transitionOrder(int64_t orderId,
                                       const std::string& toState,
                                       int64_t operatorId,
                                       const std::string& operatorName,
                                       int64_t roleId,
                                       const std::string& remark = "");

private:
    // ── Internal helpers ─────────────────────────────────────────────────────

    /// Build the sequence number part of order_no / sub_no.
    /// Queries daily count and returns next sequential number (zero-padded 4 digits).
    static int getDailySequence(drogon::orm::Transaction& trans,
                                const std::string& prefix);

    /// Compute subtotal for a cart item given quantity, unit_price, and months.
    static std::string computeSubtotal(int quantity,
                                       const std::string& unitPrice,
                                       int periodMonths);

    /// Get the distributor_id for a given user. Returns 0 if user has no
    /// distributor or is admin.
    static int64_t getUserDistributorId(int64_t userId);
};

} // namespace idc
