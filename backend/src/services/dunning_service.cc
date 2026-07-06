#include "dunning_service.h"
#include "subscription_service.h"
#include "pdf_service.h"
#include "utils/db_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Date helpers
// ═══════════════════════════════════════════════════════════════════════════

static std::string todayDateStr() {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d");
    return oss.str();
}

static std::tm parseDate(const std::string& dateStr) {
    std::tm tm = {};
    std::istringstream ss(dateStr);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) {
        throw std::invalid_argument("Invalid date: " + dateStr);
    }
    return tm;
}

/// Compute days between two YYYY-MM-DD dates (today - dueDate).
static int daysBetween(const std::string& fromStr, const std::string& toStr) {
    auto fromTm = parseDate(fromStr);
    auto toTm   = parseDate(toStr);
    std::time_t fromT = std::mktime(&fromTm);
    std::time_t toT   = std::mktime(&toTm);
    double diffSec = std::difftime(toT, fromT);
    return static_cast<int>(std::round(diffSec / 86400.0));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Config helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string DunningService::getConfigValue(const std::string& key,
                                            const std::string& defaultValue) {
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "SELECT value FROM system_config WHERE key = $1", key);
        if (!result.empty() && !result[0][0].isNull()) {
            return result[0][0].as<std::string>();
        }
    } catch (const std::exception& e) {
        LOG_WARN << "[DunningService] Config read failed for " << key
                 << ": " << e.what();
    }
    return defaultValue;
}

std::vector<int> DunningService::parseOverdueDays(const std::string& val) {
    std::vector<int> days;
    std::istringstream ss(val);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try {
            days.push_back(std::stoi(token));
        } catch (...) {
            // skip invalid
        }
    }
    return days;
}

int DunningService::computeOverdueDays(const std::string& dueDateStr) {
    std::string today = todayDateStr();
    return daysBetween(dueDateStr, today);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Get system operator
// ═══════════════════════════════════════════════════════════════════════════

void DunningService::getSystemOperator(int64_t& userId, std::string& username) {
    try {
        auto db = DbClient::getClient();
        auto result = db->execSqlSync(
            "SELECT id, username FROM users WHERE role_id = "
            "(SELECT id FROM roles WHERE name = 'admin') "
            "ORDER BY id LIMIT 1");
        if (!result.empty()) {
            userId = result[0]["id"].as<int64_t>();
            username = result[0]["username"].as<std::string>();
            return;
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[DunningService] Failed to get system operator: " << e.what();
    }
    userId = 0;
    username = "system";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Create notification event
// ═══════════════════════════════════════════════════════════════════════════

int64_t DunningService::createNotificationEvent(
    int64_t distributorId,
    const std::string& type,
    const std::string& title,
    const std::string& message,
    const Json::Value& data) {

    auto db = DbClient::getClient();
    std::string dataStr = data.isNull() ? "{}" : data.toStyledString();

    auto result = db->execSqlSync(R"(
        INSERT INTO notification_events
        (distributor_id, type, title, message, data)
        VALUES ($1, $2, $3, $4, $5::JSONB)
        RETURNING id
    )", distributorId, type, title, message, dataStr);

    if (result.empty()) {
        LOG_ERROR << "[DunningService] Failed to create notification event";
        return 0;
    }

    return result[0]["id"].as<int64_t>();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Get dunning config
// ═══════════════════════════════════════════════════════════════════════════

Json::Value DunningService::getConfig() {
    Json::Value config;
    config["enabled"] = getConfigValue("dunning.enabled", "true");
    config["batch_size"] = getConfigValue("dunning.batch_size", "50");
    config["reminder_days_before"] = getConfigValue("dunning.reminder_days_before", "3");
    config["reminder_overdue_days"] = getConfigValue("dunning.reminder_overdue_days", "1,3,7");
    config["suspend_after_days"] = getConfigValue("dunning.suspend_after_days", "15");
    config["terminate_after_days"] = getConfigValue("dunning.terminate_after_days", "30");
    return config;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Run Daily Dunning
// ═══════════════════════════════════════════════════════════════════════════

Json::Value DunningService::runDailyDunning() {
    Json::Value summary;
    summary["run_at"] = todayDateStr();
    summary["upcoming_reminders"] = 0;
    summary["overdue_marked"] = 0;
    summary["overdue_reminders"] = 0;
    summary["auto_suspended"] = 0;
    summary["auto_terminated"] = 0;
    summary["errors"] = Json::arrayValue;

    // Check if dunning is enabled
    if (getConfigValue("dunning.enabled", "true") != "true") {
        LOG_INFO << "[DunningService] Dunning is disabled — skipping";
        summary["skipped"] = true;
        return summary;
    }

    LOG_INFO << "[DunningService] Starting daily dunning check";

    try {
        processUpcomingReminders(summary);
        processOverdueMarking(summary);
        processOverdueReminders(summary);
        processAutoSuspend(summary);
        processAutoTerminate(summary);
    } catch (const std::exception& e) {
        LOG_ERROR << "[DunningService] Fatal error: " << e.what();
        Json::Value err;
        err["message"] = e.what();
        summary["errors"].append(err);
    }

    LOG_INFO << "[DunningService] Daily dunning complete: "
             << "upcoming=" << summary["upcoming_reminders"].asInt()
             << " overdue_marked=" << summary["overdue_marked"].asInt()
             << " reminders=" << summary["overdue_reminders"].asInt()
             << " suspended=" << summary["auto_suspended"].asInt()
             << " terminated=" << summary["auto_terminated"].asInt();

    return summary;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 1: Pre-due reminders
// ═══════════════════════════════════════════════════════════════════════════

void DunningService::processUpcomingReminders(Json::Value& summary) {
    try {
        int reminderDaysBefore = 3;
        try {
            reminderDaysBefore = std::stoi(
                getConfigValue("dunning.reminder_days_before", "3"));
        } catch (...) {}

        std::string today = todayDateStr();
        std::string targetDate = todayDateStr(); // invoices due in N days

        auto db = DbClient::getClient();
        auto batchSizeStr = getConfigValue("dunning.batch_size", "50");
        int batchSize = 50;
        try { batchSize = std::stoi(batchSizeStr); } catch (...) {}

        // Find invoices where due_date = today + reminderDaysBefore
        // and they're still unpaid
        auto rows = db->execSqlSync(R"(
            SELECT i.id, i.invoice_no, i.distributor_id,
                   i.due_date::TEXT AS due_date,
                   i.total_amount,
                   d.name AS distributor_name
            FROM invoices i
            JOIN distributors d ON d.id = i.distributor_id
            WHERE i.status = 'unpaid'
              AND i.due_date = ($1::DATE + $2::INTERVAL)::DATE
            LIMIT $3
        )", today, std::to_string(reminderDaysBefore) + " days", batchSize);

        std::string notifyTemplate = getConfigValue(
            "dunning.reminder_notify_template",
            "您的账单 ${invoice_no} 将于 ${due_date} 到期，金额 ${amount} 元。请确保账户余额充足。");

        int count = 0;
        for (const auto& row : rows) {
            try {
                int64_t distId = row["distributor_id"].as<int64_t>();
                std::string invoiceNo = row["invoice_no"].as<std::string>();
                std::string dueDate = row["due_date"].as<std::string>();
                std::string amount = row["total_amount"].as<std::string>();
                std::string distName = row["distributor_name"].isNull()
                    ? "" : row["distributor_name"].as<std::string>();

                // Build message from template (simple substitution)
                std::string message = notifyTemplate;
                auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
                    size_t pos = 0;
                    while ((pos = s.find(from, pos)) != std::string::npos) {
                        s.replace(pos, from.length(), to);
                        pos += to.length();
                    }
                };
                replaceAll(message, "${invoice_no}", invoiceNo);
                replaceAll(message, "${due_date}", dueDate);
                replaceAll(message, "${amount}", amount);
                replaceAll(message, "${distributor_name}", distName);

                Json::Value data;
                data["invoice_id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
                data["invoice_no"] = invoiceNo;
                data["amount"] = amount;
                data["due_date"] = dueDate;

                createNotificationEvent(
                    distId, "invoice.reminder",
                    "账单即将到期 - " + invoiceNo,
                    message, data);

                count++;
            } catch (const std::exception& e) {
                LOG_ERROR << "[DunningService] Upcoming reminder error for invoice_id="
                          << row["id"].as<int64_t>() << ": " << e.what();
                Json::Value err;
                err["phase"] = "upcoming_reminder";
                err["invoice_id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
                err["error"] = e.what();
                summary["errors"].append(err);
            }
        }

        summary["upcoming_reminders"] = count;
        LOG_DEBUG << "[DunningService] Upcoming reminders sent: " << count;

    } catch (const std::exception& e) {
        LOG_ERROR << "[DunningService] processUpcomingReminders error: " << e.what();
        Json::Value err;
        err["phase"] = "upcoming_reminder";
        err["error"] = e.what();
        summary["errors"].append(err);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 2: Overdue marking (due_date passed → mark overdue)
// ═══════════════════════════════════════════════════════════════════════════

void DunningService::processOverdueMarking(Json::Value& summary) {
    try {
        std::string today = todayDateStr();
        auto db = DbClient::getClient();
        auto batchSizeStr = getConfigValue("dunning.batch_size", "50");
        int batchSize = 50;
        try { batchSize = std::stoi(batchSizeStr); } catch (...) {}

        // Find invoices that just became overdue today
        auto rows = db->execSqlSync(R"(
            SELECT i.id, i.invoice_no, i.distributor_id,
                   i.due_date::TEXT AS due_date,
                   i.total_amount,
                   d.name AS distributor_name
            FROM invoices i
            JOIN distributors d ON d.id = i.distributor_id
            WHERE i.status = 'unpaid'
              AND i.due_date < $1::DATE
              AND i.due_date >= ($1::DATE - INTERVAL '1 day')::DATE
            LIMIT $2
        )", today, batchSize);

        std::string notifyTemplate = getConfigValue(
            "dunning.overdue_notify_template",
            "您的账单 ${invoice_no} 已逾期，逾期金额 ${amount} 元，请尽快支付避免服务暂停。");

        int count = 0;
        for (const auto& row : rows) {
            try {
                int64_t distId = row["distributor_id"].as<int64_t>();
                int64_t invoiceId = row["id"].as<int64_t>();
                std::string invoiceNo = row["invoice_no"].as<std::string>();
                std::string amount = row["total_amount"].as<std::string>();
                std::string dueDate = row["due_date"].as<std::string>();
                std::string distName = row["distributor_name"].isNull()
                    ? "" : row["distributor_name"].as<std::string>();

                // Mark as overdue
                db->execSqlSync(
                    "UPDATE invoices SET status = 'overdue', updated_at = NOW() "
                    "WHERE id = $1 AND status = 'unpaid'",
                    invoiceId);

                // Build message
                std::string message = notifyTemplate;
                auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
                    size_t pos = 0;
                    while ((pos = s.find(from, pos)) != std::string::npos) {
                        s.replace(pos, from.length(), to);
                        pos += to.length();
                    }
                };
                replaceAll(message, "${invoice_no}", invoiceNo);
                replaceAll(message, "${amount}", amount);
                replaceAll(message, "${due_date}", dueDate);
                replaceAll(message, "${distributor_name}", distName);

                Json::Value data;
                data["invoice_id"] = static_cast<Json::Int64>(invoiceId);
                data["invoice_no"] = invoiceNo;
                data["amount"] = amount;
                data["due_date"] = dueDate;

                createNotificationEvent(
                    distId, "invoice.overdue",
                    "账单已逾期 - " + invoiceNo,
                    message, data);

                count++;
            } catch (const std::exception& e) {
                LOG_ERROR << "[DunningService] Overdue marking error for invoice_id="
                          << row["id"].as<int64_t>() << ": " << e.what();
                Json::Value err;
                err["phase"] = "overdue_marking";
                err["invoice_id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
                err["error"] = e.what();
                summary["errors"].append(err);
            }
        }

        summary["overdue_marked"] = count;

    } catch (const std::exception& e) {
        LOG_ERROR << "[DunningService] processOverdueMarking error: " << e.what();
        Json::Value err;
        err["phase"] = "overdue_marking";
        err["error"] = e.what();
        summary["errors"].append(err);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 3: Overdue reminders (escalating)
// ═══════════════════════════════════════════════════════════════════════════

void DunningService::processOverdueReminders(Json::Value& summary) {
    try {
        std::string today = todayDateStr();
        auto db = DbClient::getClient();

        std::string overdueDaysStr = getConfigValue(
            "dunning.reminder_overdue_days", "1,3,7");
        auto reminderDays = parseOverdueDays(overdueDaysStr);

        if (reminderDays.empty()) return;

        std::string notifyTemplate = getConfigValue(
            "dunning.overdue_notify_template",
            "您的账单 ${invoice_no} 已逾期 ${overdue_days} 天，逾期金额 ${amount} 元，请尽快支付避免服务暂停。");

        auto batchSizeStr = getConfigValue("dunning.batch_size", "50");
        int batchSize = 50;
        try { batchSize = std::stoi(batchSizeStr); } catch (...) {}

        int totalReminders = 0;

        for (int overdueDay : reminderDays) {
            // Find invoices that became overdue exactly N days ago
            auto rows = db->execSqlSync(R"(
                SELECT i.id, i.invoice_no, i.distributor_id,
                       i.due_date::TEXT AS due_date,
                       i.total_amount,
                       d.name AS distributor_name
                FROM invoices i
                JOIN distributors d ON d.id = i.distributor_id
                WHERE i.status = 'overdue'
                  AND i.due_date = ($1::DATE - $2::INTERVAL)::DATE
                LIMIT $3
            )", today, std::to_string(overdueDay) + " days", batchSize);

            for (const auto& row : rows) {
                try {
                    int64_t distId = row["distributor_id"].as<int64_t>();
                    std::string invoiceNo = row["invoice_no"].as<std::string>();
                    std::string amount = row["total_amount"].as<std::string>();
                    std::string dueDate = row["due_date"].as<std::string>();
                    std::string distName = row["distributor_name"].isNull()
                        ? "" : row["distributor_name"].as<std::string>();

                    std::string message = notifyTemplate;
                    auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
                        size_t pos = 0;
                        while ((pos = s.find(from, pos)) != std::string::npos) {
                            s.replace(pos, from.length(), to);
                            pos += to.length();
                        }
                    };
                    std::string overdueDaysStr = std::to_string(overdueDay);
                    replaceAll(message, "${invoice_no}", invoiceNo);
                    replaceAll(message, "${amount}", amount);
                    replaceAll(message, "${overdue_days}", overdueDaysStr);
                    replaceAll(message, "${due_date}", dueDate);
                    replaceAll(message, "${distributor_name}", distName);

                    Json::Value data;
                    data["invoice_id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
                    data["invoice_no"] = invoiceNo;
                    data["amount"] = amount;
                    data["overdue_days"] = overdueDay;

                    createNotificationEvent(
                        distId, "invoice.reminder",
                        "逾期提醒 (" + std::to_string(overdueDay) + "天) - " + invoiceNo,
                        message, data);

                    totalReminders++;
                } catch (const std::exception& e) {
                    LOG_ERROR << "[DunningService] Overdue reminder error: " << e.what();
                }
            }
        }

        summary["overdue_reminders"] = totalReminders;

    } catch (const std::exception& e) {
        LOG_ERROR << "[DunningService] processOverdueReminders error: " << e.what();
        Json::Value err;
        err["phase"] = "overdue_reminder";
        err["error"] = e.what();
        summary["errors"].append(err);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 4: Auto-suspend subscriptions for long-overdue invoices
// ═══════════════════════════════════════════════════════════════════════════

void DunningService::processAutoSuspend(Json::Value& summary) {
    try {
        std::string today = todayDateStr();
        auto db = DbClient::getClient();

        int suspendAfterDays = 15;
        try {
            suspendAfterDays = std::stoi(
                getConfigValue("dunning.suspend_after_days", "15"));
        } catch (...) {}

        auto batchSizeStr = getConfigValue("dunning.batch_size", "50");
        int batchSize = 50;
        try { batchSize = std::stoi(batchSizeStr); } catch (...) {}

        // Find invoices that have been overdue for >= suspend_after_days
        // and whose subscriptions haven't been suspended yet
        auto rows = db->execSqlSync(R"(
            SELECT i.id, i.invoice_no, i.distributor_id,
                   i.due_date::TEXT AS due_date,
                   i.total_amount,
                   ii.subscription_id,
                   s.sub_no
            FROM invoices i
            JOIN invoice_items ii ON ii.invoice_id = i.id
            JOIN subscriptions s ON s.id = ii.subscription_id
            WHERE i.status = 'overdue'
              AND i.due_date <= ($1::DATE - $2::INTERVAL)::DATE
              AND s.status = 'active'
            LIMIT $3
        )", today, std::to_string(suspendAfterDays) + " days", batchSize);

        int64_t sysUserId = 0;
        std::string sysUsername;
        getSystemOperator(sysUserId, sysUsername);

        std::string notifyTemplate = getConfigValue(
            "dunning.suspend_notify_template",
            "您的账单 ${invoice_no} 已逾期 ${overdue_days} 天，服务将被暂停。请尽快登录平台完成支付。");

        int suspended = 0;
        for (const auto& row : rows) {
            try {
                int64_t invoiceId = row["id"].as<int64_t>();
                int64_t distId = row["distributor_id"].as<int64_t>();
                int64_t subId = row["subscription_id"].isNull()
                    ? 0 : row["subscription_id"].as<int64_t>();
                std::string invoiceNo = row["invoice_no"].as<std::string>();
                std::string dueDate = row["due_date"].as<std::string>();
                std::string subNo = row["sub_no"].isNull()
                    ? "N/A" : row["sub_no"].as<std::string>();

                if (subId <= 0) continue;

                int overdueDays = computeOverdueDays(dueDate);

                // Suspend the subscription (system operator)
                try {
                    SubscriptionService::suspendSubscription(
                        subId, sysUserId, sysUsername, 1, /* role_id = admin */
                        "逾期未付款 - 账单 " + invoiceNo +
                        " 已逾期 " + std::to_string(overdueDays) + " 天");
                } catch (const std::exception& e) {
                    LOG_WARN << "[DunningService] Failed to suspend sub_id="
                             << subId << ": " << e.what();
                    // Continue — some subs may fail but we try others
                }

                // Create notification
                std::string message = notifyTemplate;
                auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
                    size_t pos = 0;
                    while ((pos = s.find(from, pos)) != std::string::npos) {
                        s.replace(pos, from.length(), to);
                        pos += to.length();
                    }
                };
                replaceAll(message, "${invoice_no}", invoiceNo);
                replaceAll(message, "${overdue_days}", std::to_string(overdueDays));
                replaceAll(message, "${sub_no}", subNo);

                Json::Value data;
                data["invoice_id"] = static_cast<Json::Int64>(invoiceId);
                data["invoice_no"] = invoiceNo;
                data["subscription_id"] = static_cast<Json::Int64>(subId);
                data["sub_no"] = subNo;
                data["overdue_days"] = overdueDays;
                data["action"] = "suspended";

                createNotificationEvent(
                    distId, "subscription.suspended",
                    "服务已暂停 - " + subNo,
                    message, data);

                suspended++;
            } catch (const std::exception& e) {
                LOG_ERROR << "[DunningService] Auto-suspend error: " << e.what();
            }
        }

        summary["auto_suspended"] = suspended;

    } catch (const std::exception& e) {
        LOG_ERROR << "[DunningService] processAutoSuspend error: " << e.what();
        Json::Value err;
        err["phase"] = "auto_suspend";
        err["error"] = e.what();
        summary["errors"].append(err);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 5: Auto-terminate for extremely overdue invoices
// ═══════════════════════════════════════════════════════════════════════════

void DunningService::processAutoTerminate(Json::Value& summary) {
    try {
        std::string today = todayDateStr();
        auto db = DbClient::getClient();

        int terminateAfterDays = 30;
        try {
            terminateAfterDays = std::stoi(
                getConfigValue("dunning.terminate_after_days", "30"));
        } catch (...) {}

        auto batchSizeStr = getConfigValue("dunning.batch_size", "50");
        int batchSize = 50;
        try { batchSize = std::stoi(batchSizeStr); } catch (...) {}

        // Find invoices that have been overdue for >= terminate_after_days
        auto rows = db->execSqlSync(R"(
            SELECT i.id, i.invoice_no, i.distributor_id,
                   i.due_date::TEXT AS due_date,
                   ii.subscription_id,
                   s.sub_no
            FROM invoices i
            JOIN invoice_items ii ON ii.invoice_id = i.id
            JOIN subscriptions s ON s.id = ii.subscription_id
            WHERE i.status = 'overdue'
              AND i.due_date <= ($1::DATE - $2::INTERVAL)::DATE
              AND s.status IN ('active', 'suspended')
            LIMIT $3
        )", today, std::to_string(terminateAfterDays) + " days", batchSize);

        int64_t sysUserId = 0;
        std::string sysUsername;
        getSystemOperator(sysUserId, sysUsername);

        std::string notifyTemplate = getConfigValue(
            "dunning.terminate_notify_template",
            "您的账单 ${invoice_no} 已逾期 ${overdue_days} 天，服务将被终止。如有疑问请联系客服。");

        int terminated = 0;
        for (const auto& row : rows) {
            try {
                int64_t invoiceId = row["id"].as<int64_t>();
                int64_t distId = row["distributor_id"].as<int64_t>();
                int64_t subId = row["subscription_id"].isNull()
                    ? 0 : row["subscription_id"].as<int64_t>();
                std::string invoiceNo = row["invoice_no"].as<std::string>();
                std::string dueDate = row["due_date"].as<std::string>();
                std::string subNo = row["sub_no"].isNull()
                    ? "N/A" : row["sub_no"].as<std::string>();

                if (subId <= 0) continue;

                int overdueDays = computeOverdueDays(dueDate);

                // Terminate the subscription
                try {
                    SubscriptionService::terminateSubscription(
                        subId, sysUserId, sysUsername, 1,
                        "逾期未付款自动终止 - 账单 " + invoiceNo +
                        " 已逾期 " + std::to_string(overdueDays) + " 天");
                } catch (const std::exception& e) {
                    LOG_WARN << "[DunningService] Failed to terminate sub_id="
                             << subId << ": " << e.what();
                }

                // Create notification
                std::string message = notifyTemplate;
                auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
                    size_t pos = 0;
                    while ((pos = s.find(from, pos)) != std::string::npos) {
                        s.replace(pos, from.length(), to);
                        pos += to.length();
                    }
                };
                replaceAll(message, "${invoice_no}", invoiceNo);
                replaceAll(message, "${overdue_days}", std::to_string(overdueDays));
                replaceAll(message, "${sub_no}", subNo);

                Json::Value data;
                data["invoice_id"] = static_cast<Json::Int64>(invoiceId);
                data["invoice_no"] = invoiceNo;
                data["subscription_id"] = static_cast<Json::Int64>(subId);
                data["sub_no"] = subNo;
                data["overdue_days"] = overdueDays;
                data["action"] = "terminated";

                createNotificationEvent(
                    distId, "subscription.terminated",
                    "服务已终止 - " + subNo,
                    message, data);

                terminated++;
            } catch (const std::exception& e) {
                LOG_ERROR << "[DunningService] Auto-terminate error: " << e.what();
            }
        }

        summary["auto_terminated"] = terminated;

    } catch (const std::exception& e) {
        LOG_ERROR << "[DunningService] processAutoTerminate error: " << e.what();
        Json::Value err;
        err["phase"] = "auto_terminate";
        err["error"] = e.what();
        summary["errors"].append(err);
    }
}

} // namespace idc
