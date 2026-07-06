#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Order State Constants
// ═══════════════════════════════════════════════════════════════════════════

/// Canonical order state names.
struct OrderStates {
    static constexpr const char* kPending       = "pending";
    static constexpr const char* kApproved      = "approved";
    static constexpr const char* kRejected      = "rejected";
    static constexpr const char* kCancelled     = "cancelled";
    static constexpr const char* kProvisioning  = "provisioning";
    static constexpr const char* kActive        = "active";
    static constexpr const char* kSuspended               = "suspended";
    static constexpr const char* kTerminated              = "terminated";
    static constexpr const char* kProvisioningFailed      = "provisioning_failed";

    /// All defined states.
    static const std::vector<const char*>& all() {
        static const std::vector<const char*> states = {
            kPending, kApproved, kRejected, kCancelled,
            kProvisioning, kActive, kSuspended, kTerminated,
            kProvisioningFailed
        };
        return states;
    }

    /// Check whether a string is a valid order state.
    static bool isValid(const std::string& s) {
        static const std::unordered_set<std::string> valid = {
            kPending, kApproved, kRejected, kCancelled,
            kProvisioning, kActive, kSuspended, kTerminated,
            kProvisioningFailed
        };
        return valid.find(s) != valid.end();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Transition Rules
// ═══════════════════════════════════════════════════════════════════════════

/// Describes a single valid state transition.
struct TransitionRule {
    const char* from;
    const char* to;
    const char* permission;          ///< Required RBAC permission ("" = auto/internal)
    bool        allowDealer;         ///< Whether a dealer (non-admin) can trigger this
    const char* description;         ///< Human-readable label
};

/// All valid state transitions for the order state machine.
inline const std::vector<TransitionRule>& kOrderTransitions() {
    static const std::vector<TransitionRule> rules = {
        // User-initiated transitions
        { OrderStates::kPending, OrderStates::kApproved,
          "order:approve", false, "审核通过" },
        { OrderStates::kPending, OrderStates::kRejected,
          "order:reject",  false, "驳回订单" },
        { OrderStates::kPending, OrderStates::kCancelled,
          "",              true,  "取消订单" },

        // Auto/internal transitions (triggered by system)
        { OrderStates::kApproved, OrderStates::kProvisioning,
          "",              false, "开始开通" },
        { OrderStates::kProvisioning, OrderStates::kActive,
          "",              false, "开通完成" },
        { OrderStates::kProvisioning, OrderStates::kProvisioningFailed,
          "",              false, "开通失败（超时）" },

        // Manual retry (admin)
        { OrderStates::kProvisioningFailed, OrderStates::kProvisioning,
          "provisioning:retry", false, "重试开通" },

        // Admin-initiated lifecycle transitions
        { OrderStates::kActive, OrderStates::kSuspended,
          "order:update",  false, "暂停服务" },
        { OrderStates::kSuspended, OrderStates::kActive,
          "order:update",  false, "恢复服务" },
        { OrderStates::kActive, OrderStates::kTerminated,
          "order:update",  false, "终止服务" },
        { OrderStates::kSuspended, OrderStates::kTerminated,
          "order:update",  false, "终止服务" },
    };
    return rules;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Permission Helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace OrderStateMachine {

/// Check if a transition from `fromState` to `toState` is valid.
inline bool isValidTransition(const std::string& fromState,
                              const std::string& toState) {
    for (const auto& rule : kOrderTransitions()) {
        if (fromState == rule.from && toState == rule.to) {
            return true;
        }
    }
    return false;
}

/// Check if a user with the given `isAdmin` flag can perform the transition.
/// A transition is allowed if the rule has no permission requirement
/// (auto/internal) OR if the user is an admin with the required permission.
inline bool canTransition(const std::string& fromState,
                          const std::string& toState,
                          bool isAdmin,
                          bool isDealer) {
    for (const auto& rule : kOrderTransitions()) {
        if (fromState == rule.from && toState == rule.to) {
            // Internal/auto transitions: no user-triggered check needed
            if (std::string(rule.permission).empty() && !rule.allowDealer) {
                return false; // auto transitions cannot be triggered by users
            }
            // Dealer-allowed transitions
            if (rule.allowDealer && isDealer) {
                return true;
            }
            // Admin required
            return isAdmin;
        }
    }
    return false;
}

/// Get the required permission code for a transition (empty = auto/internal).
inline std::string requiredPermission(const std::string& fromState,
                                      const std::string& toState) {
    for (const auto& rule : kOrderTransitions()) {
        if (fromState == rule.from && toState == rule.to) {
            return rule.permission;
        }
    }
    return {};
}

/// Check whether a state is a terminal state (no outgoing transitions).
inline bool isTerminalState(const std::string& state) {
    for (const auto& rule : kOrderTransitions()) {
        if (state == rule.from) {
            return false;
        }
    }
    return true;
}

/// Get the list of valid target states from a given state.
inline std::vector<std::string> validTargets(const std::string& fromState) {
    std::vector<std::string> targets;
    for (const auto& rule : kOrderTransitions()) {
        if (fromState == rule.from) {
            targets.push_back(rule.to);
        }
    }
    return targets;
}

} // namespace OrderStateMachine

} // namespace idc
