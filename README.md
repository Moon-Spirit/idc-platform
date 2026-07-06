# IDC 分销平台

> 一个高性能的 IDC（互联网数据中心）分销管理系统。前端 Vue 3 + TypeScript + Element Plus，后端 C++17 + Drogon 框架。

---

## 快速开始

### 环境要求
| 组件 | 版本 | 用途 |
|------|------|------|
| PostgreSQL | 16+ | 业务数据库 |
| Redis | 7+ | 缓存/会话/分布式锁 |
| CMake | 3.20+ | C++ 构建工具 |
| g++ | 12+ | C++17 编译器 |
| Node.js | 22+ | 前端构建 |

### 1. 创建数据库

```bash
# 创建用户和数据库
sudo -u postgres psql -c "CREATE USER idcapp WITH PASSWORD 'idcapp123';"
sudo -u postgres psql -c "CREATE DATABASE idc_platform OWNER idcapp;"
```

### 2. 执行数据库迁移

```bash
# 按顺序执行所有迁移文件
for f in backend/migrations/0*.sql; do
  sudo -u postgres psql -d idc_platform -f "$f"
done

# 授权表权限
sudo -u postgres psql -d idc_platform -c \
  "GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO idcapp;"
sudo -u postgres psql -d idc_platform -c \
  "GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO idcapp;"
```

### 3. 编译后端

```bash
cd backend
mkdir -p build && cd build
cmake .. -DBUILD_POSTGRESQL=ON -DBUILD_REDIS=ON
make -j$(nproc)
```

### 4. 启动后端服务

```bash
export LD_LIBRARY_PATH=$PWD/deps/lib
IDC_DB_PASSWORD=idcapp123 ./build/idc-platform
```

服务启动后在 http://localhost:8080 监听。

### 5. 构建前端（可选）

```bash
cd frontend
npm install
npm run build     # 生产构建
npm run dev       # 开发模式（端口5173，自动代理API到8080）
```

---

## 测试接口

```bash
# 登录（获取 JWT Token）
curl http://localhost:8080/api/v1/auth/login \
  -X POST \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"admin123"}'

# 保存 Token 供后续使用
TOKEN="<上一步返回的token>"

# 查看产品类型
curl http://localhost:8080/api/v1/products/types \
  -H "Authorization: Bearer $TOKEN"

# 查看经销商列表
curl http://localhost:8080/api/v1/distributors \
  -H "Authorization: Bearer $TOKEN"

# 查看 Dashboard 统计
curl http://localhost:8080/api/v1/dashboard/stats \
  -H "Authorization: Bearer $TOKEN"
```

---

## 专题文档

| 文档 | 说明 |
|------|------|
| `docs/deployment.md` | 生产环境部署指南（740行，含SSL、systemd、备份策略） |
| `docs/operations.md` | 运维指南（532行，含排障清单、升级回滚流程） |
| `scripts/deploy.sh` | 一键部署脚本 |
| `scripts/backup.sh` | 数据库自动备份脚本 |
| `tests/api_test.sh` | 全量 API 集成测试（15个测试用例） |
| `.omo/plans/*.md` | 完整项目架构设计文档 |

---

## 项目结构

```
idc-platform/
├── backend/               # Drogon C++ 后端
│   ├── src/
│   │   ├── controllers/   # 16个API控制器
│   │   ├── services/      # 20个业务服务
│   │   ├── filters/       # JWT + RBAC 过滤器
│   │   ├── cron/          # 定时任务（计费/催缴/同步）
│   │   └── utils/         # 工具类（JWT/加密/HTTP/限流）
│   ├── migrations/        # 9个数据库迁移文件
│   └── build/idc-platform # 编译好的后端二进制 (5.8MB)
├── frontend/              # Vue 3 前端
│   ├── src/views/dealer/  # 经销商门户（产品/订单/账单）
│   ├── src/views/admin/   # 管理员后台（管理/报表/同步）
│   └── e2e/              # Playwright E2E 测试
├── docker/                # Docker 编排
│   ├── docker-compose.yml # PG + Redis + Gotenberg + Nginx
│   └── nginx/             # Nginx 反代配置
├── docs/                  # 文档（部署/运维）
├── scripts/               # 运维脚本（部署/备份）
└── tests/                 # API 集成测试
```

---

## 架构概览

```
┌─ Vue 3 SPA (Element Plus) ─────────────────┐
│  经销商门户: 产品/购物车/订单/账单/带宽      │
│  管理员后台: 经销商/产品/定价/订单/报表/同步  │
├─ REST API + WebSocket ─────────────────────┤
│  Drogon C++ 后端                            │
│  认证 → 产品 → 经销商 → 订单 → 订阅         │
│  → 计费引擎(月付/95带宽/按量) → 账单 → 支付  │
│  → ZJMF适配器(魔方v10 + 魔方财务)双向同步     │
├─ PostgreSQL 16 + TimescaleDB + Redis ──────┤
│  16核心表 / 带宽时序数据 / 缓存/会话/锁      │
└─────────────────────────────────────────────┘
```

---

## 默认账户

| 角色 | 用户名 | 密码 |
|------|--------|------|
| 管理员 | admin | admin123 |

> ⚠️ 生产环境请立即修改默认密码和 JWT Secret（通过 `IDC_DB_PASSWORD` 和 `IDC_JWT_SECRET` 环境变量）。

---

## 技术栈

- **后端**: C++17, Drogon 1.9.10, PostgreSQL 16, TimescaleDB, Redis 7
- **前端**: Vue 3, TypeScript, Element Plus, Pinia, Vue Router, ECharts, Playwright
- **部署**: Nginx, Gotenberg (PDF), systemd
- **代码量**: 200+ 源文件, 40,000+ 行代码

---

## 许可证

AGPL v3
