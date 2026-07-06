#include "subscription_service.h"
#include "models/subscription_state_machine.h"
#include "utils/db_client.h"
#include "utils/redis_client.h"
#include "utils/logger.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace idc {

// ═══════════════════════════════════════════════════════════════════════════
//  Helper: get user's distributor_id
// ═══════════════════════════════════════════════════════════════════════════

int64_t SubscriptionService::getUserDistributorId(int64_t userId) {
    auto db = DbClient::getClient();
    auto result = db->execSqlSync(
        "SELECT distributor_id FROM users WHERE id = $1", userId);
    if (result.empty()) return 0;
    auto val = result[0]["distributor_id"];
    return val.isNull() ? 0 : val.as<int64_t>();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Record Timeline Entry
// ═══════════════════════════════════════════════════════════════════════════

void SubscriptionService::recordTimeline(drogon::orm::Transaction& trans,
                                          int64_t subId,
                                          const std::string& fromStatus,
                                          const std::string& toStatus,
                                          int64_t operatorId,
                                          const std::string& operatorName,
                                          const std::string& remark) {
    trans.execSqlSync(
        "INSERT INTO subscription_timeline "
        "(subscription_id, from_status, to_status, operator_id, operator_name, remark) "
        "VALUES ($1, $2, $3, $4, $5, $6)",
        subId, fromStatus, toStatus, operatorId, operatorName, remark);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Validate Transition
// ═══════════════════════════════════════════════════════════════════════════

void SubscriptionService::validateTransition(const std::string& currentState,
                                              const std::string& toState,
                                              bool isAdmin, bool isDealer) {
    if (!SubscriptionStateMachine::isValidTransition(currentState, toState)) {
        throw std::invalid_argument(
            "Invalid subscription state transition: " + currentState +
            " → " + toState);
    }
    if (!SubscriptionStateMachine::canTransition(currentState, toState,
                                                  isAdmin, isDealer)) {
        throw std::invalid_argument(
            "Not authorized to perform subscription transition: " +
            currentState + " → " + toState);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  List Subscriptions
// ═══════════════════════════════════════════════════════════════════════════

Json::Value SubscriptionService::listSubscriptions(int64_t userId,
                                                     int64_t roleId,
                                                     const std::string& status,
                                                     int64_t distributorId,
                                                     int page, int perPage) {
    auto db = DbClient::getClient();

    // Permission filter: dealers only see their own subscriptions
    bool isAdmin = (roleId == 1);
    if (!isAdmin && distributorId <= 0) {
        distributorId = getUserDistributorId(userId);
    }

    // Build SQL
    std::string whereClause;
    std::string countSql = "SELECT COUNT(*) FROM subscriptions s";
    std::string querySql =
        "SELECT s.id, s.sub_no, s.order_id, s.distributor_id, "
        "d.name AS distributor_name, "
        "s.product_id, p.name AS product_name, "
        "s.status, s.start_date, s.next_billing_date, s.end_date, "
        "s.billing_cycle, s.auto_renew, "
        "s.provision_status, s.remote_system, s.remote_resource_id, "
        "s.created_at "
        "FROM subscriptions s "
        "LEFT JOIN distributors d ON d.id = s.distributor_id "
        "LEFT JOIN products p ON p.id = s.product_id";

    // Build WHERE conditions
    std::vector<std::string> conditions;
    if (distributorId > 0) {
        conditions.push_back("s.distributor_id = " + std::to_string(distributorId));
    }
    if (!status.empty() && SubscriptionStates::isValid(status)) {
        conditions.push_back("s.status = '" + status + "'");
    }

    if (!conditions.empty()) {
        whereClause = " WHERE ";
        for (size_t i = 0; i < conditions.size(); ++i) {
            if (i > 0) whereClause += " AND ";
            whereClause += conditions[i];
        }
    }

    // Count total
    auto countResult = db->execSqlSync(countSql + whereClause);
    int64_t total = countResult.empty() ? 0 : countResult[0][0].as<int64_t>();

    // Fetch page
    int offset = (page - 1) * perPage;
    querySql += whereClause +
        " ORDER BY s.created_at DESC "
        "LIMIT " + std::to_string(perPage) + " OFFSET " + std::to_string(offset);

    auto rows = db->execSqlSync(querySql);

    Json::Value items(Json::arrayValue);
    for (const auto& row : rows) {
        Json::Value item;
        item["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
        item["sub_no"] = row["sub_no"].as<std::string>();
        item["order_id"] = static_cast<Json::Int64>(row["order_id"].as<int64_t>());
        item["distributor_id"] = static_cast<Json::Int64>(
            row["distributor_id"].as<int64_t>());
        item["distributor_name"] = row["distributor_name"].isNull()
            ? "" : row["distributor_name"].as<std::string>();
        item["product_id"] = static_cast<Json::Int64>(row["product_id"].as<int64_t>());
        item["product_name"] = row["product_name"].isNull()
            ? "" : row["product_name"].as<std::string>();
        item["status"] = row["status"].as<std::string>();
        item["start_date"] = row["start_date"].isNull()
            ? "" : row["start_date"].as<std::string>();
        item["next_billing_date"] = row["next_billing_date"].isNull()
            ? "" : row["next_billing_date"].as<std::string>();
        item["end_date"] = row["end_date"].isNull()
            ? "" : row["end_date"].as<std::string>();
        item["billing_cycle"] = row["billing_cycle"].as<std::string>();
        item["auto_renew"] = row["auto_renew"].as<bool>();
        item["provision_status"] = row["provision_status"].isNull()
            ? "" : row["provision_status"].as<std::string>();
        item["remote_system"] = row["remote_system"].isNull()
            ? Json::nullValue : Json::Value(row["remote_system"].as<std::string>());
        item["remote_resource_id"] = row["remote_resource_id"].isNull()
            ? Json::nullValue : Json::Value(row["remote_resource_id"].as<std::string>());
        item["created_at"] = row["created_at"].as<std::string>();
        items.append(item);
    }

    Json::Value data;
    data["items"] = items;
    data["total"] = static_cast<Json::Int64>(total);
    data["page"] = page;
    data["per_page"] = perPage;
    return data;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Get Subscription Detail
// ═══════════════════════════════════════════════════════════════════════════

Json::Value SubscriptionService::getSubscriptionById(int64_t subId,
                                                      int64_t userId,
                                                      int64_t roleId) {
    auto db = DbClient::getClient();

    // Fetch subscription with product info
    std::string subSql =
        "SELECT s.id, s.sub_no, s.order_id, s.order_item_id, s.distributor_id, "
        "s.product_id, s.status, s.start_date, s.next_billing_date, s.end_date, "
        "s.billing_cycle, s.auto_renew, s.provision_status, "
        "s.remote_system, s.remote_resource_id, s.suspend_reason, "
        "s.product_specs AS sub_specs, s.provision_data, "
        "s.created_at, s.updated_at, "
        "d.name AS distributor_name, "
        "p.name AS product_name, p.type AS product_type, p.specs AS product_definition "
        "FROM subscriptions s "
        "LEFT JOIN distributors d ON d.id = s.distributor_id "
        "LEFT JOIN products p ON p.id = s.product_id "
        "WHERE s.id = $1";
    auto subRows = db->execSqlSync(subSql, subId);

    if (subRows.empty()) {
        return Json::nullValue;
    }

    auto row = subRows[0];

    // Permission check: dealers can only see their own subscriptions
    bool isAdmin = (roleId == 1);
    if (!isAdmin) {
        int64_t userDistId = getUserDistributorId(userId);
        int64_t subDistId = row["distributor_id"].as<int64_t>();
        if (userDistId != subDistId) {
            throw std::invalid_argument("Subscription not found");
        }
    }

    Json::Value sub;
    sub["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
    sub["sub_no"] = row["sub_no"].as<std::string>();
    sub["order_id"] = static_cast<Json::Int64>(row["order_id"].as<int64_t>());
    sub["order_item_id"] = row["order_item_id"].isNull()
        ? Json::nullValue
        : Json::Value(static_cast<Json::Int64>(row["order_item_id"].as<int64_t>()));
    sub["distributor_id"] = static_cast<Json::Int64>(row["distributor_id"].as<int64_t>());
    sub["distributor_name"] = row["distributor_name"].isNull()
        ? "" : row["distributor_name"].as<std::string>();
    sub["product_id"] = static_cast<Json::Int64>(row["product_id"].as<int64_t>());
    sub["product_name"] = row["product_name"].isNull()
        ? "" : row["product_name"].as<std::string>();
    sub["product_type"] = row["product_type"].isNull()
        ? "" : row["product_type"].as<std::string>();

    // Parse subscription's own product_specs (actual configuration)
    if (!row["sub_specs"].isNull()) {
        std::string specsStr = row["sub_specs"].as<std::string>();
        Json::Reader reader;
        Json::Value specs;
        if (reader.parse(specsStr, specs)) {
            sub["specs"] = specs;
        }
    }

    // Parse provision_data JSON
    if (!row["provision_data"].isNull()) {
        std::string pdStr = row["provision_data"].as<std::string>();
        Json::Reader reader;
        Json::Value pd;
        if (reader.parse(pdStr, pd)) {
            sub["provision_data"] = pd;
        }
    }

    // Parse product definition specs
    if (!row["product_definition"].isNull()) {
        std::string defStr = row["product_definition"].as<std::string>();
        Json::Reader reader;
        Json::Value def;
        if (reader.parse(defStr, def)) {
            sub["product_specs"] = def;
        }
    }

    sub["status"] = row["status"].as<std::string>();
    sub["start_date"] = row["start_date"].isNull()
        ? "" : row["start_date"].as<std::string>();
    sub["next_billing_date"] = row["next_billing_date"].isNull()
        ? "" : row["next_billing_date"].as<std::string>();
    sub["end_date"] = row["end_date"].isNull()
        ? "" : row["end_date"].as<std::string>();
    sub["billing_cycle"] = row["billing_cycle"].as<std::string>();
    sub["auto_renew"] = row["auto_renew"].as<bool>();
    sub["provision_status"] = row["provision_status"].isNull()
        ? "" : row["provision_status"].as<std::string>();
    sub["remote_system"] = row["remote_system"].isNull()
        ? Json::nullValue : Json::Value(row["remote_system"].as<std::string>());
    sub["remote_resource_id"] = row["remote_resource_id"].isNull()
        ? Json::nullValue : Json::Value(row["remote_resource_id"].as<std::string>());
    sub["suspend_reason"] = row["suspend_reason"].isNull()
        ? "" : row["suspend_reason"].as<std::string>();

    sub["created_at"] = row["created_at"].as<std::string>();
    sub["updated_at"] = row["updated_at"].as<std::string>();

    // Fetch subscription timeline
    auto tlRows = db->execSqlSync(
        "SELECT * FROM subscription_timeline WHERE subscription_id = $1 "
        "ORDER BY created_at ASC", subId);

    Json::Value timeline(Json::arrayValue);
    for (const auto& tlRow : tlRows) {
        Json::Value entry;
        entry["status"] = tlRow["to_status"].as<std::string>();
        entry["from_status"] = tlRow["from_status"].isNull()
            ? Json::nullValue : Json::Value(tlRow["from_status"].as<std::string>());
        entry["time"] = tlRow["created_at"].as<std::string>();
        entry["operator"] = tlRow["operator_name"].isNull()
            ? Json::nullValue : Json::Value(tlRow["operator_name"].as<std::string>());
        entry["remark"] = tlRow["remark"].isNull()
            ? "" : tlRow["remark"].as<std::string>();
        timeline.append(entry);
    }
    sub["timeline"] = timeline;

    // Fetch upgrade history
    auto ugRows = db->execSqlSync(
        "SELECT * FROM subscription_upgrades "
        "WHERE subscription_id = $1 "
        "ORDER BY created_at DESC LIMIT 10", subId);

    Json::Value upgrades(Json::arrayValue);
    for (const auto& ugRow : ugRows) {
        Json::Value ug;
        ug["id"] = static_cast<Json::Int64>(ugRow["id"].as<int64_t>());
        ug["status"] = ugRow["status"].as<std::string>();
        ug["effective_date"] = ugRow["effective_date"].as<std::string>();

        // Parse from_specs and to_specs JSON
        Json::Reader reader;

        std::string fromSpecsStr = ugRow["from_specs"].as<std::string>();
        Json::Value fromSpecs;
        if (reader.parse(fromSpecsStr, fromSpecs)) {
            ug["from_specs"] = fromSpecs;
        }

        std::string toSpecsStr = ugRow["to_specs"].as<std::string>();
        Json::Value toSpecs;
        if (reader.parse(toSpecsStr, toSpecs)) {
            ug["to_specs"] = toSpecs;
        }

        ug["created_at"] = ugRow["created_at"].as<std::string>();
        upgrades.append(ug);
    }
    sub["upgrade_history"] = upgrades;

    return sub;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Suspend Subscription
// ═══════════════════════════════════════════════════════════════════════════

Json::Value SubscriptionService::suspendSubscription(int64_t subId,
                                                       int64_t operatorId,
                                                       const std::string& operatorName,
                                                       int64_t roleId,
                                                       const std::string& reason) {
    if (reason.empty()) {
        throw std::invalid_argument("Suspend reason is required");
    }

    bool isAdmin = (roleId == 1);
    bool isDealer = !isAdmin;

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // Fetch current subscription state with row lock
        auto rows = trans.execSqlSync(
            "SELECT * FROM subscriptions WHERE id = $1 FOR UPDATE", subId);

        if (rows.empty()) {
            throw std::invalid_argument("Subscription not found");
        }

        auto row = rows[0];
        std::string currentState = row["status"].as<std::string>();

        // Validate transition
        validateTransition(currentState, SubscriptionStates::kSuspended,
                            isAdmin, isDealer);

        // Update subscription status
        trans.execSqlSync(
            "UPDATE subscriptions SET status = $1, suspend_reason = $2, "
            "updated_at = NOW() WHERE id = $3",
            SubscriptionStates::kSuspended, reason, subId);

        // Record timeline
        recordTimeline(trans, subId, currentState,
                        SubscriptionStates::kSuspended,
                        operatorId, operatorName, reason);

        // Build result
        result["id"] = static_cast<Json::Int64>(subId);
        result["sub_no"] = row["sub_no"].as<std::string>();
        result["status"] = SubscriptionStates::kSuspended;
        result["previous_status"] = currentState;
        result["suspend_reason"] = reason;
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Activate Subscription (resume from suspended)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value SubscriptionService::activateSubscription(int64_t subId,
                                                        int64_t operatorId,
                                                        const std::string& operatorName,
                                                        int64_t roleId,
                                                        const std::string& reason) {
    bool isAdmin = (roleId == 1);
    bool isDealer = !isAdmin;

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // Fetch current subscription state with row lock
        auto rows = trans.execSqlSync(
            "SELECT * FROM subscriptions WHERE id = $1 FOR UPDATE", subId);

        if (rows.empty()) {
            throw std::invalid_argument("Subscription not found");
        }

        auto row = rows[0];
        std::string currentState = row["status"].as<std::string>();

        // Validate transition
        validateTransition(currentState, SubscriptionStates::kActive,
                            isAdmin, isDealer);

        // Update subscription status
        trans.execSqlSync(
            "UPDATE subscriptions SET status = $1, suspend_reason = NULL, "
            "updated_at = NOW() WHERE id = $2",
            SubscriptionStates::kActive, subId);

        // Record timeline
        std::string effRemark = reason.empty() ? "服务已恢复" : reason;
        recordTimeline(trans, subId, currentState,
                        SubscriptionStates::kActive,
                        operatorId, operatorName, effRemark);

        // Build result
        result["id"] = static_cast<Json::Int64>(subId);
        result["sub_no"] = row["sub_no"].as<std::string>();
        result["status"] = SubscriptionStates::kActive;
        result["previous_status"] = currentState;
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Terminate Subscription
// ═══════════════════════════════════════════════════════════════════════════

Json::Value SubscriptionService::terminateSubscription(int64_t subId,
                                                         int64_t operatorId,
                                                         const std::string& operatorName,
                                                         int64_t roleId,
                                                         const std::string& reason) {
    bool isAdmin = (roleId == 1);
    bool isDealer = !isAdmin;

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // Fetch current subscription state with row lock
        auto rows = trans.execSqlSync(
            "SELECT * FROM subscriptions WHERE id = $1 FOR UPDATE", subId);

        if (rows.empty()) {
            throw std::invalid_argument("Subscription not found");
        }

        auto row = rows[0];
        std::string currentState = row["status"].as<std::string>();

        // Validate transition
        validateTransition(currentState, SubscriptionStates::kTerminated,
                            isAdmin, isDealer);

        // Update subscription status and set end_date
        trans.execSqlSync(
            "UPDATE subscriptions SET status = $1, end_date = CURRENT_DATE, "
            "auto_renew = false, updated_at = NOW() WHERE id = $2",
            SubscriptionStates::kTerminated, subId);

        // Record timeline
        std::string effRemark = reason.empty() ? "服务已终止" : reason;
        recordTimeline(trans, subId, currentState,
                        SubscriptionStates::kTerminated,
                        operatorId, operatorName, effRemark);

        // Build result
        result["id"] = static_cast<Json::Int64>(subId);
        result["sub_no"] = row["sub_no"].as<std::string>();
        result["status"] = SubscriptionStates::kTerminated;
        result["previous_status"] = currentState;
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Submit Upgrade / Downgrade Request
// ═══════════════════════════════════════════════════════════════════════════

Json::Value SubscriptionService::submitUpgrade(int64_t subId,
                                                 const Json::Value& newSpecs,
                                                 const std::string& effectiveDate,
                                                 int64_t requestedBy,
                                                 int64_t roleId) {
    if (newSpecs.isNull() || !newSpecs.isObject() || newSpecs.empty()) {
        throw std::invalid_argument("New specs must be a non-empty JSON object");
    }

    std::string effDate = effectiveDate;
    if (effDate.empty()) {
        effDate = "immediate";
    }
    if (effDate != "immediate" && effDate != "next_billing") {
        throw std::invalid_argument(
            "effective_date must be 'immediate' or 'next_billing'");
    }

    bool isAdmin = (roleId == 1);

    Json::Value result;

    DbClient::transaction([&](drogon::orm::Transaction& trans) {
        // Fetch current subscription with row lock
        auto rows = trans.execSqlSync(
            "SELECT * FROM subscriptions WHERE id = $1 FOR UPDATE", subId);

        if (rows.empty()) {
            throw std::invalid_argument("Subscription not found");
        }

        auto row = rows[0];
        std::string currentState = row["status"].as<std::string>();

        // Only active or suspended subscriptions can be upgraded
        if (currentState != SubscriptionStates::kActive &&
            currentState != SubscriptionStates::kSuspended) {
            throw std::invalid_argument(
                "Cannot upgrade subscription in status: " + currentState);
        }

        // Get current specs from subscription record
        // Use product_specs as the current configuration
        Json::Value currentSpecs;
        std::string currentSpecsStr = row["product_specs"].isNull()
            ? "{}"
            : row["product_specs"].as<std::string>();
        {
            Json::Reader reader;
            if (!reader.parse(currentSpecsStr, currentSpecs)) {
                currentSpecs = Json::objectValue;
            }
        }

        // INSERT upgrade request
        auto ugResult = trans.execSqlSync(
            "INSERT INTO subscription_upgrades "
            "(subscription_id, from_specs, to_specs, status, effective_date, "
            "requested_by) "
            "VALUES ($1, $2, $3, 'pending', $4, $5) "
            "RETURNING id",
            subId,
            currentSpecs.toStyledString(),
            newSpecs.toStyledString(),
            effDate,
            requestedBy);

        if (ugResult.empty()) {
            throw std::runtime_error("Failed to create upgrade request");
        }

        int64_t upgradeId = ugResult[0][0].as<int64_t>();

        // Build result
        result["id"] = static_cast<Json::Int64>(upgradeId);
        result["subscription_id"] = static_cast<Json::Int64>(subId);
        result["sub_no"] = row["sub_no"].as<std::string>();
        result["status"] = "pending";
        result["effective_date"] = effDate;
        result["from_specs"] = currentSpecs;
        result["to_specs"] = newSpecs;
    });

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Auto-Renew (daily cron)
// ═══════════════════════════════════════════════════════════════════════════

Json::Value SubscriptionService::runAutoRenewCheck() {
    Json::Value result;
    int renewedCount = 0;
    Json::Value errors(Json::arrayValue);

    try {
        auto db = DbClient::getClient();

        // Find subscriptions due for renewal:
        //   - status = 'active'
        //   - auto_renew = true
        //   - next_billing_date <= NOW() + 7 days
        auto rows = db->execSqlSync(
            "SELECT id, sub_no, next_billing_date, billing_cycle "
            "FROM subscriptions "
            "WHERE status = 'active' "
            "  AND auto_renew = true "
            "  AND next_billing_date IS NOT NULL "
            "  AND next_billing_date <= CURRENT_DATE + INTERVAL '7 days'");

        for (const auto& row : rows) {
            try {
                int64_t subId = row["id"].as<int64_t>();
                std::string subNo = row["sub_no"].as<std::string>();
                std::string billingCycle = row["billing_cycle"].as<std::string>();

                // Compute the interval to add
                std::string interval;
                if (billingCycle == "yearly") {
                    interval = "1 year";
                } else {
                    interval = "1 month"; // default: monthly
                }

                // Update next_billing_date within a transaction
                DbClient::transaction([&](drogon::orm::Transaction& trans) {
                    // Get the current next_billing_date
                    auto subRows = trans.execSqlSync(
                        "SELECT next_billing_date FROM subscriptions "
                        "WHERE id = $1 FOR UPDATE", subId);

                    if (subRows.empty()) return;

                    auto nextBilling = subRows[0]["next_billing_date"].as<std::string>();

                    // Advance next_billing_date
                    trans.execSqlSync(
                        "UPDATE subscriptions SET "
                        "next_billing_date = next_billing_date + INTERVAL '" +
                        interval + "', "
                        "updated_at = NOW() "
                        "WHERE id = $1",
                        subId);

                    // Record timeline entry
                    trans.execSqlSync(
                        "INSERT INTO subscription_timeline "
                        "(subscription_id, from_status, to_status, "
                        "operator_id, operator_name, remark) "
                        "VALUES ($1, 'active', 'active', "
                        "0, 'system', '自动续费：下次计费日 " + nextBilling +
                        " → 更新后')",
                        subId);
                });

                renewedCount++;
            } catch (const std::exception& e) {
                Json::Value err;
                err["subscription_id"] = static_cast<Json::Int64>(
                    row["id"].as<int64_t>());
                err["error"] = e.what();
                errors.append(err);

                LOG_ERROR << "[AutoRenew] Failed for sub_id="
                          << row["id"].as<int64_t>()
                          << ": " << e.what();
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "[AutoRenew] Query failed: " << e.what();
        result["error"] = e.what();
    }

    result["renewed_count"] = renewedCount;
    result["errors"] = errors;
    return result;
}

} // namespace idc
