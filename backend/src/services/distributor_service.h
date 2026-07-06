#pragma once

#include <json/json.h>
#include <string>

namespace idc {

/// Business logic for distributor (经销商) management.
///
/// Handles:
///   - CRUD with pagination and multi-field filtering
///   - Recursive tree traversal (WITH RECURSIVE CTE)
///   - Circular reference prevention on parent_id assignment
///   - Statistics aggregation from orders / invoices / subscriptions
class DistributorService {
public:
    // ── Queries ──────────────────────────────────────────────────────────────

    /// Paginated distributor list with optional filters.
    /// Query params: status, level, keyword (name/phone/email), parent_id.
    /// Returns JSON: { items: [...], total, page, per_page }
    static Json::Value listDistributors(int page, int per_page,
                                        int16_t status, int16_t level,
                                        const std::string& keyword,
                                        int64_t parentId);

    /// Get a single distributor by id. Returns null value if not found.
    /// Includes aggregate stats: order_count, total_revenue,
    ///   active_subscriptions, overdue_invoices.
    static Json::Value getDistributorById(int64_t id);

    // ── Mutations (admin only) ───────────────────────────────────────────────

    /// Create a new distributor.
    /// Checks parent_id exists and prevents circular references.
    /// Returns the new distributor id.
    /// Throws std::invalid_argument on validation failure.
    static int64_t createDistributor(const Json::Value& body);

    /// Update an existing distributor.
    /// If parent_id changes, validates no circular reference.
    /// Throws std::invalid_argument on not-found or validation failure.
    static void updateDistributor(int64_t id, const Json::Value& body);

    // ── Tree ─────────────────────────────────────────────────────────────────

    /// Recursive children tree (all descendants, unlimited depth).
    /// Uses WITH RECURSIVE CTE. Returns nested JSON:
    ///   { id, name, children: [...] }
    static Json::Value getDistributorTree(int64_t id);

    /// Direct children of the given distributor (one level).
    /// Returns a flat JSON array.
    static Json::Value getDirectChildren(int64_t id);

    // ── Statistics ───────────────────────────────────────────────────────────

    /// Monthly statistics for a distributor:
    ///   orders_this_month, revenue_this_month,
    ///   subscription_count, overdue_invoice_count
    static Json::Value getDistributorStatistics(int64_t id);

private:
    // ── Helpers ──────────────────────────────────────────────────────────────

    /// Check whether assigning `newParentId` to `distributorId` would create
    /// a circular reference (i.e. newParentId is already a descendant of
    /// distributorId). Returns true if safe, false if circular.
    static bool validateParentAssignment(int64_t distributorId,
                                         int64_t newParentId);

    /// Build the recursive tree node from a flat row set returned by the
    /// WITH RECURSIVE CTE. The rows must contain at least id and parent_id.
    static Json::Value buildTreeFromRows(const Json::Value& rows, int64_t rootId);

    /// Recursively attach children to a tree node.
    static void attachChildren(Json::Value& node,
                               const Json::Value& rows,
                               int64_t nodeId);
};

} // namespace idc
