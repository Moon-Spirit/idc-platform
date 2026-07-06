#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Subscription State Constants
// ═══════════════════════════════════════════════════════════════════════════

/// Canonical subscription state names.
struct SubscriptionStates {
    static constexpr const char* kPending     = "pending";
    static constexpr const char* kActive      = "active";
    static constexpr const char* kSuspended   = "suspended";
    static constexpr const char* kTerminated  = "terminated";

    /// All defined states.
    static const std::vector<const char*>& all() {
        static const std::vector<const char*> states = {
            kPending, kActive, kSuspended, kTerminated
        };
        return states;
    }

    /// Check whether a string is a valid subscription state.
    static bool isValid(const std::string& s) {
        static const std::unordered_set<std::string> valid = {
            kPending, kActive, kSuspended, kTerminated
        };
        return valid.find(s) != valid.end();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Transition Rules
// ═══════════════════════════════════════════════════════════════════════════

/// Describes a single valid state transition for subscriptions.
struct SubTransitionRule {
    const char* from;
    const char* to;
    const char* permission;          ///< Required RBAC permission ("" = auto/internal)
    bool        allowDealer;         ///< Whether a dealer (non-admin) can trigger this
    const char* description;         ///< Human-readable label
};

/// All valid state transitions for the subscription state machine.
inline const std::vector<SubTransitionRule>& kSubTransitions() {
    static const std::vector<SubTransitionRule> rules = {
        // Provisioning complete (auto/system)
        { SubscriptionStates::kPending, SubscriptionStates::kActive,
          "",              false, "开通完成" },

        // Admin-initiated lifecycle transitions
        { SubscriptionStates::kActive, SubscriptionStates::kSuspended,
          "subscription:suspend", false, "暂停服务" },
        { SubscriptionStates::kSuspended, SubscriptionStates::kActive,
          "subscription:update", false, "恢复服务" },
        { SubscriptionStates::kActive, SubscriptionStates::kTerminated,
          "subscription:terminate", false, "终止服务" },
        { SubscriptionStates::kSuspended, SubscriptionStates::kTerminated,
          "subscription:terminate", false, "终止服务" },
    };
    return rules;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Permission Helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace SubscriptionStateMachine {

/// Check if a transition from `fromState` to `toState` is valid.
inline bool isValidTransition(const std::string& fromState,
                              const std::string& toState) {
    for (const auto& rule : kSubTransitions()) {
        if (fromState == rule.from && toState == rule.to) {
            return true;
        }
    }
    return false;
}

/// Check if a user with the given flags can perform the transition.
inline bool canTransition(const std::string& fromState,
                          const std::string& toState,
                          bool isAdmin,
                          bool isDealer) {
    for (const auto& rule : kSubTransitions()) {
        if (fromState == rule.from && toState == rule.to) {
            // Internal/auto transitions: not user-triggerable
            if (std::string(rule.permission).empty() && !rule.allowDealer) {
                return false;
            }
            // Dealer-allowed transitions
            if (rule.allowDealer && isDealer) {
                return true;
            }
            // Admin required for all subscription mutations
            return isAdmin;
        }
    }
    return false;
}

/// Get the required permission code for a transition (empty = auto/internal).
inline std::string requiredPermission(const std::string& fromState,
                                      const std::string& toState) {
    for (const auto& rule : kSubTransitions()) {
        if (fromState == rule.from && toState == rule.to) {
            return rule.permission;
        }
    }
    return {};
}

/// Check whether a state is a terminal state (no outgoing transitions).
inline bool isTerminalState(const std::string& state) {
    for (const auto& rule : kSubTransitions()) {
        if (state == rule.from) {
            return false;
        }
    }
    return true;
}

/// Get the list of valid target states from a given state.
inline std::vector<std::string> validTargets(const std::string& fromState) {
    std::vector<std::string> targets;
    for (const auto& rule : kSubTransitions()) {
        if (fromState == rule.from) {
            targets.push_back(rule.to);
        }
    }
    return targets;
}

} // namespace SubscriptionStateMachine

} // namespace idc
