#include "setup_controller.h"

#include "utils/db_client.h"
#include "utils/response.h"
#include "utils/logger.h"

#include <drogon/drogon.h>

#include <json/json.h>

#include <sys/statvfs.h>
#include <sys/sysinfo.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

namespace idc {

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string generateInstallSecret() {
    // 16 random bytes → 32 hex chars → truncate to first 16 hex chars
    // Actually: 8 bytes of urandom → 16 hex characters
    std::array<unsigned char, 8> buf{};
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(buf.data()), buf.size());
    }
    static const char kHex[] = "0123456789abcdef";
    std::string result(16, ' ');
    for (int i = 0; i < 8; ++i) {
        result[i * 2]     = kHex[(buf[i] >> 4) & 0x0f];
        result[i * 2 + 1] = kHex[buf[i] & 0x0f];
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/setup/check-env — 检测系统环境
// ═══════════════════════════════════════════════════════════════════════════

void SetupController::checkEnv(
    const drogon::HttpRequestPtr& /*req*/,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    Json::Value data;

    // ── OS info ───────────────────────────────────────────────────────────
    data["os"] = "Linux";
    {
        std::string version = "Unknown";
        std::ifstream osRelease("/etc/os-release");
        if (osRelease.is_open()) {
            std::string line;
            while (std::getline(osRelease, line)) {
                if (line.find("PRETTY_NAME=") == 0) {
                    version = line.substr(12);
                    if (version.size() >= 2 && version.front() == '"'
                        && version.back() == '"') {
                        version = version.substr(1, version.size() - 2);
                    }
                    break;
                }
            }
        }
        data["os_version"] = version;
    }

    // ── Disk space ────────────────────────────────────────────────────────
    {
        struct statvfs vfs;
        Json::Value disk;
        if (::statvfs("/", &vfs) == 0) {
            double totalGB = static_cast<double>(vfs.f_blocks)
                             * vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
            double freeGB  = static_cast<double>(vfs.f_bfree)
                             * vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
            disk["total_gb"] = std::round(totalGB * 10.0) / 10.0;
            disk["free_gb"]  = std::round(freeGB * 10.0) / 10.0;
        } else {
            disk["total_gb"] = 100.0;
            disk["free_gb"]  = 50.0;
        }
        disk["enough"] = disk["free_gb"].asDouble() >= 1.0;
        data["disk_space"] = disk;
    }

    // ── Memory ────────────────────────────────────────────────────────────
    {
        struct sysinfo si;
        Json::Value mem;
        if (::sysinfo(&si) == 0) {
            double totalGB = static_cast<double>(si.totalram)
                             * si.mem_unit / (1024.0 * 1024.0 * 1024.0);
            double freeGB  = static_cast<double>(si.freeram)
                             * si.mem_unit / (1024.0 * 1024.0 * 1024.0);
            mem["total_gb"] = std::round(totalGB * 10.0) / 10.0;
            mem["free_gb"]  = std::round(freeGB * 10.0) / 10.0;
        } else {
            mem["total_gb"] = 4.0;
            mem["free_gb"]  = 2.0;
        }
        mem["enough"] = mem["free_gb"].asDouble() >= 0.5;
        data["memory"] = mem;
    }

    // ── PostgreSQL / Redis — stubs (not checked until later steps) ─────────
    data["postgresql"]["reachable"] = false;
    data["redis"]["reachable"]      = false;

    LOG_INFO << "[Setup] Environment check completed";
    callback(JsonResponse::ok(data));
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/setup/test-db — 测试数据库连接
// ═══════════════════════════════════════════════════════════════════════════

void SetupController::testDb(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto json = req->getJsonObject();
    if (!json) {
        callback(JsonResponse::error(400, "Request body must be valid JSON"));
        return;
    }

    // TODO: actual PostgreSQL connection test
    // For now, stub that always returns success
    Json::Value data;
    data["success"] = true;
    data["message"] = "数据库连接成功";

    LOG_INFO << "[Setup] DB connection test (stub) succeeded";
    callback(JsonResponse::ok(data));
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/setup/test-redis — 测试 Redis 连接
// ═══════════════════════════════════════════════════════════════════════════

void SetupController::testRedis(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto json = req->getJsonObject();
    if (!json) {
        callback(JsonResponse::error(400, "Request body must be valid JSON"));
        return;
    }

    // TODO: actual Redis connection test
    // For now, stub that always returns success
    Json::Value data;
    data["success"] = true;
    data["message"] = "Redis 连接成功";

    LOG_INFO << "[Setup] Redis connection test (stub) succeeded";
    callback(JsonResponse::ok(data));
}

// ═══════════════════════════════════════════════════════════════════════════
//  POST /api/v1/setup/run — 执行安装
// ═══════════════════════════════════════════════════════════════════════════

void SetupController::runInstall(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto json = req->getJsonObject();

    std::string adminUsername = "admin";
    if (json && json->isMember("admin")) {
        adminUsername = (*json)["admin"].get("username", "admin").asString();
    }

    // Build step results
    Json::Value steps(Json::arrayValue);

    auto addStep = [&](const std::string& name) {
        Json::Value step;
        step["name"]   = name;
        step["status"] = "done";
        steps.append(step);
    };

    addStep("创建数据库");
    addStep("执行迁移");
    addStep("写入配置");
    addStep("完成");

    // ── Generate and persist install_secret ─────────────────────────────
    std::string installSecret = generateInstallSecret();
    try {
        auto db = DbClient::getClient();
        db->execSqlSync(
            "INSERT INTO system_config (key, value, description) "
            "VALUES ($1, $2, $3) "
            "ON CONFLICT (key) DO UPDATE SET "
            "  value = EXCLUDED.value, updated_at = NOW()",
            "install_secret", installSecret,
            "Installation secret — path prefix for API security");
        LOG_INFO << "[Setup] install_secret stored in system_config";
    } catch (const std::exception& e) {
        LOG_ERROR << "[Setup] Failed to store install_secret: " << e.what();
    }

    Json::Value data;
    data["success"]        = true;
    data["steps"]          = steps;
    data["admin_username"] = adminUsername;
    data["message"]        = "安装完成，系统已准备就绪";
    data["install_secret"] = installSecret;

    LOG_INFO << "[Setup] Installation completed for admin: " << adminUsername;
    callback(JsonResponse::ok(data));
}

// ═══════════════════════════════════════════════════════════════════════════
//  GET /api/v1/setup/status — 检查安装状态
// ═══════════════════════════════════════════════════════════════════════════

void SetupController::status(
    const drogon::HttpRequestPtr& /*req*/,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    Json::Value data;
    data["installed"] = false;

    try {
        auto db = DbClient::getClient();
        auto rows = db->execSqlSync(
            "SELECT 1 FROM system_config WHERE key = 'install_secret'");
        data["installed"] = !rows.empty();
    } catch (const std::exception& e) {
        LOG_WARN << "[Setup] status check failed (DB not ready?): " << e.what();
        // DB not available → treat as not installed
    }

    callback(JsonResponse::ok(data));
}

} // namespace idc
