#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Mapper.h>
#include <functional>
#include <string>
#include <stdexcept>

namespace idc {

/// PostgreSQL DbClient convenience wrapper.
///
/// Usage:
///   auto db = DbClient::getClient();
///   auto rows = db->execSqlSync("SELECT * FROM users WHERE id = $1", 42);
///
/// Transaction helper:
///   DbClient::transaction([](drogon::orm::Transaction& t) {
///       t.execSqlSync("UPDATE accounts SET balance = balance - 100 WHERE id = $1", 1);
///       t.execSqlSync("UPDATE accounts SET balance = balance + 100 WHERE id = $1", 2);
///   });
class DbClient {
public:
    /// Default database client name in config.json.
    static constexpr const char* kDefaultDbName = "idc_db";

    /// Get a DbClient from the framework connection pool.
    static drogon::orm::DbClientPtr getClient(
        const std::string& name = kDefaultDbName) {
        return drogon::app().getDbClient(name);
    }

    /// Execute a lambda inside a database transaction.
    ///
    /// The lambda receives `drogon::orm::Transaction&`.
    /// On success the transaction is committed; on exception it is rolled back
    /// and the exception propagates to the caller.
    template <typename Func>
    static auto transaction(Func&& func) -> void {
        static_assert(std::is_invocable_v<Func, drogon::orm::Transaction&>,
                      "transaction callback must accept drogon::orm::Transaction&");

        auto client = getClient();
        auto trans  = client->newTransaction();
        try {
            func(*trans);
            // commit synchronously — throws on failure
            trans->commit();
        } catch (...) {
            trans->rollback();
            throw;
        }
    }

    /// Overload that uses a named DbClient.
    template <typename Func>
    static auto transaction(const std::string& dbName, Func&& func) -> void {
        static_assert(std::is_invocable_v<Func, drogon::orm::Transaction&>,
                      "transaction callback must accept drogon::orm::Transaction&");

        auto client = getClient(dbName);
        auto trans  = client->newTransaction();
        try {
            func(*trans);
            trans->commit();
        } catch (...) {
            trans->rollback();
            throw;
        }
    }

    DbClient() = delete;
};

}  // namespace idc
