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

/// Business logic for subscription (服务实例) lifecycle management.
///
/// Handles:
///   - Subscription detail and listing with pagination & filters
///   - Lifecycle state transitions (suspend / activate / terminate)
///   - Upgrade/downgrade spec change tracking
///   - Auto-renew detection (daily cron)
///   - Subscription timeline recording for audit trail
///
/// Subscriptions are auto-created by OrderService on order approval.
class SubscriptionService {
public:
    // ── Subscription queries ──────────────────────────────────────────────

    /// Paginated subscription list with optional status and distributor filters.
    /// Returns JSON: { items: [...], total, page, per_page }
    ///
    /// If `userId` is a dealer (role != admin), results are filtered to their
    /// distributor_id. Admin sees all.
    static Json::Value listSubscriptions(int64_t userId, int64_t roleId,
                                          const std::string& status,
                                          int64_t distributorId,
                                          int page, int perPage);

    /// Get subscription detail by id, including product info and timeline.
    /// Returns null value if not found.
    static Json::Value getSubscriptionById(int64_t subId, int64_t userId,
                                            int64_t roleId);

    // ── Lifecycle state transitions ──────────────────────────────────────

    /// Suspend a subscription (admin only).
    ///
    /// Transition: active → suspended
    /// Records timeline entry with the suspend reason.
    ///
    /// @param subId         Subscription ID
    /// @param operatorId    User performing the action
    /// @param operatorName  Username snapshot
    /// @param roleId        Role ID for permission check
    /// @param reason        Required suspend reason
    /// @return Updated subscription JSON
    /// @throws std::invalid_argument on invalid transition or permissions
    static Json::Value suspendSubscription(int64_t subId,
                                            int64_t operatorId,
                                            const std::string& operatorName,
                                            int64_t roleId,
                                            const std::string& reason);

    /// Activate a suspended subscription (admin only).
    ///
    /// Transition: suspended → active
    ///
    /// @param subId         Subscription ID
    /// @param operatorId    User performing the action
    /// @param operatorName  Username snapshot
    /// @param roleId        Role ID for permission check
    /// @param reason        Optional reactivation note
    /// @return Updated subscription JSON
    /// @throws std::invalid_argument on invalid transition or permissions
    static Json::Value activateSubscription(int64_t subId,
                                             int64_t operatorId,
                                             const std::string& operatorName,
                                             int64_t roleId,
                                             const std::string& reason = "");

    /// Terminate a subscription (admin only).
    ///
    /// Transition: active/suspended → terminated
    ///
    /// @param subId         Subscription ID
    /// @param operatorId    User performing the action
    /// @param operatorName  Username snapshot
    /// @param roleId        Role ID for permission check
    /// @param reason        Optional termination note
    /// @return Updated subscription JSON
    /// @throws std::invalid_argument on invalid transition or permissions
    static Json::Value terminateSubscription(int64_t subId,
                                              int64_t operatorId,
                                              const std::string& operatorName,
                                              int64_t roleId,
                                              const std::string& reason = "");

    // ── Upgrade / downgrade ──────────────────────────────────────────────

    /// Submit an upgrade/downgrade spec change request.
    ///
    /// Creates a subscription_upgrades record with the current specs as
    /// `from_specs` and the requested new specs as `to_specs`.
    ///
    /// @param subId          Subscription ID
    /// @param newSpecs       Requested new specs JSON object
    /// @param effectiveDate  "immediate" or "next_billing"
    /// @param requestedBy    User ID who submitted the request
    /// @param roleId         Role ID for permission check
    /// @return Created upgrade request JSON
    /// @throws std::invalid_argument if subscription not found or invalid
    static Json::Value submitUpgrade(int64_t subId,
                                      const Json::Value& newSpecs,
                                      const std::string& effectiveDate,
                                      int64_t requestedBy,
                                      int64_t roleId);

    // ── Auto-renew (daily cron) ──────────────────────────────────────────

    /// Check subscriptions expiring in the next 7 days and auto-renew them.
    ///
    /// For each subscription where:
    ///   - auto_renew = true
    ///   - next_billing_date <= NOW() + INTERVAL '7 days'
    ///   - status = 'active'
    ///
    /// Updates next_billing_date by adding the billing cycle period,
    /// and records a timeline entry.
    ///
    /// Called by a Drogon daily cron job.
    /// Returns a summary JSON with { renewed_count, errors }.
    static Json::Value runAutoRenewCheck();

private:
    // ── Internal helpers ──────────────────────────────────────────────────

    /// Record a subscription_timeline entry.
    static void recordTimeline(drogon::orm::Transaction& trans,
                                int64_t subId,
                                const std::string& fromStatus,
                                const std::string& toStatus,
                                int64_t operatorId,
                                const std::string& operatorName,
                                const std::string& remark);

    /// Validate a state transition and check permissions.
    /// Throws std::invalid_argument on failure.
    static void validateTransition(const std::string& currentState,
                                    const std::string& toState,
                                    bool isAdmin, bool isDealer);

    /// Get the distributor_id for a given user. Returns 0 if user has no
    /// distributor or is admin.
    static int64_t getUserDistributorId(int64_t userId);
};

} // namespace idc
