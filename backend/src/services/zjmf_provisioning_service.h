#pragma once

#include <json/json.h>
#include <string>

namespace idc {

/// Downstream provisioning orchestration for ZJMF (智简魔方) systems.
///
/// Handles pushing approved orders to downstream dealers' ZJMF instances
/// (v10: createServer, finance: createInvoice) and tracking provisioning
/// status via polling or webhook callbacks.
///
/// Flow:
///   1. Order approved → provisionOrder(orderId)
///   2. For each subscription with provision_status='pending':
///      a. Determine target system from subscription.remote_system
///      b. Get downstream ZJMF connection matching the type
///      c. Call adapter: createServer (v10) or createInvoice (finance)
///      d. Store remote_id in zjmf_entity_mappings
///      e. Update subscription.remote_resource_id
///      f. Start 30s polling loop (max 30 min) for status
///   3. Webhook receiver processes status callbacks
///   4. On done: subscription → active, order → active
///   5. On failure: subscription.provision_status → failed, order → provisioning_failed
class ZJMFProvisioningService {
public:
    // ── Constants ───────────────────────────────────────────────────────────

    static constexpr int kPollIntervalSeconds = 30;    // Poll every 30s
    static constexpr int kMaxPollTimeSeconds  = 1800;  // Max 30 minutes
    static constexpr int kMaxPollCount        = 60;    // 1800 / 30

    // ── Provisioning triggers ──────────────────────────────────────────────

    /// Provision all subscriptions for an approved order.
    /// Called automatically after OrderService::transitionOrder → "approved".
    /// Iterates each subscription in the order and provisions.
    static void provisionOrder(int64_t orderId);

    /// Provision a single subscription (used by retry as well).
    /// @return JSON with provisioning result details.
    static Json::Value provisionSubscription(int64_t subscriptionId);

    /// Manual retry for a failed provisioning.
    /// Resets provision_status to 'provisioning', re-calls the adapter.
    /// @return JSON with retry result.
    static Json::Value retryProvisioning(int64_t subscriptionId,
                                         int64_t operatorId,
                                         const std::string& operatorName);

    // ── Status queries ─────────────────────────────────────────────────────

    /// Get current provisioning status for a subscription.
    /// Returns: { subscription_id, sub_no, provision_status, remote_system,
    ///            remote_resource_id, provisioning_attempts,
    ///            provisioning_error, poll_count, ... }
    static Json::Value getProvisionStatus(int64_t subscriptionId);

    // ── Webhook handling ───────────────────────────────────────────────────

    /// Process a provisioning status callback from downstream ZJMF.
    /// Called by ZJMFController::handleWebhook when event type matches
    /// server provisioning or invoice status.
    /// @return JSON with { subscription_id, provision_status }.
    static Json::Value handleWebhookCallback(const Json::Value& payload);

    // ── Polling ────────────────────────────────────────────────────────────

    /// Run periodic provisioning polling check.
    /// Called every kPollIntervalSeconds by Drogon timer.
    /// Finds subscriptions with provision_status='provisioning' and
    /// remote_resource_id IS NOT NULL, queries downstream for status.
    static void runPollingCheck();

private:
    // ── Helpers ────────────────────────────────────────────────────────────

    /// Inner logic: provision one subscription (shared by provisionOrder
    /// and retryProvisioning). Skips if remote_system is NULL.
    /// Returns true if the provisioning call was submitted (even if async).
    static bool doProvision(int64_t subId, int64_t orderId);

    /// Store entity mapping in zjmf_entity_mappings.
    static void storeEntityMapping(const std::string& localType,
                                   int64_t localId,
                                   const std::string& remoteSystem,
                                   const std::string& remoteId,
                                   const Json::Value& remoteData);

    /// Log a provisioning sync action to zjmf_sync_logs.
    static int64_t logSyncAction(int64_t connectionId,
                                 const std::string& direction,
                                 const std::string& entityType,
                                 int64_t entityId,
                                 const std::string& remoteId,
                                 const std::string& action,
                                 const Json::Value& requestData,
                                 const Json::Value& responseData,
                                 const std::string& status,
                                 const std::string& errorMessage);

    /// Get the downstream connection ID matching a remote_system type.
    /// Finds the first active downstream connection with matching type.
    /// Returns 0 if not found.
    static int64_t findDownstreamConnection(const std::string& remoteSystem);

    /// Build server creation payload for v10 adapter from subscription data.
    static Json::Value buildServerPayload(int64_t subId);

    /// Build invoice creation payload for finance adapter.
    static Json::Value buildInvoicePayload(int64_t subId);

    /// Handle successful provisioning completion.
    static void completeProvisioning(int64_t subId,
                                     const std::string& remoteResourceId);

    /// Handle provisioning failure.
    static void failProvisioning(int64_t subId, int64_t orderId,
                                 const std::string& errorMessage);
};

} // namespace idc
