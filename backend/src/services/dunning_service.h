#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace idc {

/// Dunning (催缴) service for overdue invoice collection.
///
/// Runs daily to check all overdue invoices and apply the configured reminder
/// schedule. The schedule is configurable via system_config keys:
///
///   dunning.enabled                = "true"
///   dunning.batch_size             = "50"
///   dunning.reminder_days_before   = "3"       (days before due → notification)
///   dunning.reminder_overdue_days  = "1,3,7"   (days after due → reminders)
///   dunning.suspend_after_days     = "15"      (auto-suspend after N days overdue)
///   dunning.terminate_after_days   = "30"      (auto-terminate after N days overdue)
///
/// Reminder schedule applied in order:
///   1. due_date - 3d  → "upcoming" notification
///   2. due_date       → "overdue" notification
///   3. due_date + 1d  → escalating reminder
///   4. due_date + 3d  → escalating reminder
///   5. due_date + 7d  → escalating reminder
///   6. due_date + 15d → auto-suspend subscription
///   7. due_date + 30d → auto-terminate subscription
///
/// All notifications are stored as notification_events (for WebSocket push).
/// No actual email is sent (just events created).
class DunningService {
public:
    /// Run the full daily dunning check.
    ///
    /// Scans all overdue invoices, applies reminders/suspends/terminates
    /// according to the configured schedule. Processes in batches to avoid
    /// long-running transactions.
    ///
    /// Called by DunningCron daily.
    /// Returns summary JSON with processed counts and errors.
    static Json::Value runDailyDunning();

    /// Get current dunning configuration from system_config.
    static Json::Value getConfig();

private:
    // ── Notification helpers ────────────────────────────────────────────

    /// Create a notification_event record.
    static int64_t createNotificationEvent(
        int64_t distributorId,
        const std::string& type,
        const std::string& title,
        const std::string& message,
        const Json::Value& data = Json::nullValue);

    // ── Config reading ──────────────────────────────────────────────────

    /// Read a system_config value, returning defaultValue if not found.
    static std::string getConfigValue(const std::string& key,
                                       const std::string& defaultValue);

    /// Parse comma-separated overdue reminder day offsets.
    static std::vector<int> parseOverdueDays(const std::string& val);

    /// Compute overdue days: how many days past the due date.
    static int computeOverdueDays(const std::string& dueDateStr);

    // ── Phase handlers ──────────────────────────────────────────────────

    /// Phase 1: Pre-due reminders (due_date - reminder_days_before)
    static void processUpcomingReminders(Json::Value& summary);

    /// Phase 2: Due-date overdue marking
    static void processOverdueMarking(Json::Value& summary);

    /// Phase 3: Post-due escalating reminders
    static void processOverdueReminders(Json::Value& summary);

    /// Phase 4: Auto-suspend (overdue >= suspend_after_days)
    static void processAutoSuspend(Json::Value& summary);

    /// Phase 5: Auto-terminate (overdue >= terminate_after_days)
    static void processAutoTerminate(Json::Value& summary);

    /// Get the system admin user (for auto operations)
    static void getSystemOperator(int64_t& userId, std::string& username);
};

} // namespace idc
