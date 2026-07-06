# idc-platform-v1 - Work Plan

## TL;DR (For humans)
<!-- Fill this LAST, after the detailed plan below is written, so it summarizes the REAL plan. -->

**What you'll get:** 一个完整的 IDC 分销平台第一版。下游经销商可以在你的平台上注册、查看产品价格（根据等级不同价格不同）、自助下单购买物理机/云服务器/带宽/IP、查看订单和账单、在线支付（支付宝/微信）。管理员可以在后台管理经销商、产品、定价模板、审核订单、查看报表。系统会自动和智简魔方双向同步数据。

**Why this approach:** 你们是自有资源+分销一体化供应商，上下游都用智简魔方，需要一个中间自定义分销层。C++ (Drogon) 提供高性能计费引擎处理95带宽计算和大量并发计费，PostgreSQL+TimescaleDB 保障复杂计费数据的可靠性。

**What it will NOT do:** 第一期不做终端客户门户、不做移动端App、不做监控告警（智简魔方处理）、不做BI分析、不做国际化。

**Effort:** Large (12周)
**Risk:** Medium-High — 4个Oracle审查发现的关键问题已修复（95计费SQL、Drogon线程模型锁、wkhtmltopdf→Goternberg、TimescaleDB数据保留策略），但C++编译时间长(首次~25min)和ZJMF对接不确定性仍是主要风险
**Decisions to sanity-check:** 后端框架Drogon确认、前端UI库Element Plus确认、部署架构Nginx+Drogon分离确认

Your next move: 审阅计划后启动执行。

---

> TL;DR (machine): 12周 / Medium风险 / 6组件一期上线 — Drogon+Vue3+Element Plus+PostgreSQL+TimescaleDB+Redis

## Scope
### Must have
1. 经销商门户（注册、产品浏览、下单、订单跟踪、账单支付）
2. 管理员后台（经销商/产品/订单/账单管理）
3. 多级价格模板（上级→下级价格继承和覆盖）
4. 计费引擎（月付/年付/95带宽/按量/增值服务prorata）
5. 支付宝+微信支付集成
6. 智简魔方双向对接（上游库存同步+下游订单下发）

### Must NOT have (guardrails, anti-slop, scope boundaries)
- 不引入微服务架构（单体起步）
- 不引入Elasticsearch（第一期SQL查询足够）
- 不做前端SSR（纯SPA）
- 不做多语言i18n
- 不做移动端适配（仅后台管理，非C端）

## Verification strategy
> Agent-executed verification with minimal human sign-off.
- Test decision: tests-after + Playwright E2E for frontend
- 详细附录文件：
  - 📘 `appendix-db.md` — 数据库表结构（16张核心表的字段、索引、SQL）
  - 📘 `appendix-api.md` — API接口定义（40+个端点的请求/响应/错误码）
  - 📘 `appendix-zjmf.md` — 智简魔方对接设计（v10+财务双产品，上游+下游双方向）
  - 📘 `appendix-frontend.md` — 前端页面原型（20+页面的布局和交互设计）
- 后端：Drogon自带测试框架 + cppunit/gtest
- 前端：Vue Test Utils + Vitest + Playwright（无头浏览器E2E）
- API：curl/httpie + 自动化脚本
- 人工参与：仅用于上线前一周的"验收走查"（非阻塞性，不影响开发验证）
- Evidence: .omo/evidence/task-<N>-idc-platform-v1.<ext>

## Execution strategy
### Parallel execution waves
> 每波5-8个任务

- Wave 1 (第1-2周): 项目骨架 + 基础架构
- Wave 2 (第3-4周): 认证 + 经销商管理 + 产品目录
- Wave 3 (第5-6周): 订单核心流程 + 订阅管理
- Wave 4 (第7-8周): 计费引擎
- Wave 5 (第9-10周): 支付 + 智简魔方对接
- Wave 6 (第11-12周): 前端门户完善 + 报表 + 部署上线 + 高精度审查

> 📘 每个Wave的详细原子任务分解见 `appendix-tasks.md`（23个主任务→41个原子任务）

### Dependency matrix
| Todo | Depends on | Blocks | Can parallelize with |
| --- | --- | --- | --- |
| 1-2 项目骨架 | - | 所有 | - |
| 3 数据库 | 1 | 后端所有 | 4 |
| 4 Docker | 1, 2 | 部署 | 3 |
| 5 认证 | 1, 3 | 6, 7 | 8 |
| 6 前端登录 | 2, 5 | 10, 21 | 7, 8 |
| 7 经销商管理 | 3, 5 | 9 | 8 |
| 8 产品+定价 | 3 | 9, 21 | 5, 7 |
| 9 订单后端 | 7, 8 | 10, 11, 12, 22 | - |
| 10 前端下单 | 6, 9 | 21 | 11 |
| 11 订阅管理 | 9 | 12, 14 | 10 |
| 12 月付计费 | 11 | 15 | 13, 14 |
| 13 95带宽计费 | 3 | 15 | 12, 14 |
| 14 按量计费 | 11 | 15 | 12, 13 |
| 15 账单+PDF+催缴 | 12, 13, 14 | 16, 17 | - |
| 16 前端账单 | 15 | 22 | 17, 18, 19 |
| 17 支付 | 15 | - | 16, 18 |
| 18 智简魔方上游 | 1 | 19, 20 | 17 |
| 19 智简魔方下游 | 9, 18 | 20 | 17 |
| 20 同步管理界面 | 18, 19 | - | 21, 22 |
| 21 经销商门户优化 | 8, 10 | 23 | 20, 22 |
| 22 Dashboard+报表 | 9, 16 | 23 | 20, 21 |
| 23 部署+集成测试 | 所有 | - | - |

## Todos
> Implementation + Test = ONE todo. Never separate.
<!-- APPEND TASK BATCHES BELOW THIS LINE WITH edit/apply_patch - never rewrite the headers above. -->

### ⚡ 执行前准备

> 在执行器启动前，需要先在您的 GitHub 组织创建仓库。Type the following in your terminal:
> ```bash
> gh repo create idc-platform --private --description="IDC分销平台"
> cd /home/sanjiu/IDC
> git init && git remote add origin git@github.com:Moon-Spirit/idc-platform.git
> mkdir -p backend frontend docker docs
> git add . && git commit -m "chore: init project structure"
> git push -u origin main
> ```
> 创建后的目录结构：
> ```
> idc-platform/
> ├── backend/   # Drogon C++ 后端
> ├── frontend/  # Vue 3 前端
> ├── docker/    # Docker 编排
> └── docs/      # 文档
> ```

### Wave 1: 项目骨架 & 基础架构 (第1-2周)

- [x] 1. 后端项目初始化
  What to do / Must NOT do: 创建 backend/ 目录，初始化 CMakeLists.txt，引入 Drogon 依赖，创建 main.cc 实现基本 HTTP 服务监听 8080 端口。配置 config.json（日志级别、线程数、数据库连接池）。必须能编译运行返回 200。
  Parallelization: Wave 1 | Blocked by: - | Blocks: 所有
  References: Drogon 官方文档 - https://drogonframework.github.io/drogon-docs/ , CMakeLists.txt 标准配置
  Acceptance criteria (agent-executable): `curl http://localhost:8080/` 返回 200，编译无警告
  QA scenarios: happy - curl 返回预期响应；failure - 端口被占用时优雅报错。
  **API统一响应格式：** 所有API响应使用 `{ "code": 0, "message": "success", "data": {...} }`，错误时使用 `{ "code": 错误码(int), "message": "错误描述" }`，HTTP状态码遵循 RESTful 规范（200成功、400参数错误、401未认证、403无权限、404不存在、500服务器错误）。在 Wave 1 中定义为共享响应基类。
  Evidence .omo/evidence/task-1-idc-platform-v1.log
  Commit: Y | chore(backend): initialize Drogon project skeleton with API conventions

- [x] 2. 前端项目初始化
  What to do / Must NOT do: 创建 frontend/ 目录，Vite + Vue 3 + TypeScript + Element Plus + Pinia + Vue Router 初始化。配置 vite.config.ts（proxy /api 到 localhost:8080，proxy /ws 到 ws://localhost:8080）。创建基础布局组件（侧边栏+顶栏+内容区），登录页路由。必须能 dev server 正常启动。
  Parallelization: Wave 1 | Blocked by: - | Blocks: 前端所有
  References: Vite官方文档 - https://vite.dev/guide/ , Vue Router官方 - https://router.vuejs.org/ , Element Plus - https://element-plus.org/ , Playwright - https://playwright.dev/
  Acceptance criteria (agent-executable): `npm run dev` 启动成功，`npx playwright install` 安装成功，基本 E2E 测试示例运行通过
  QA scenarios: happy - 首页布局完整，Playwright测试通过；failure - 依赖缺失时报错信息清晰。Evidence .omo/evidence/task-2-idc-platform-v1.log
  Commit: Y | chore(frontend): initialize Vue 3 + Element Plus project with Playwright

- [x] 3. 数据库初始化
  What to do / Must NOT do: 创建 PostgreSQL 初始化 SQL 脚本：创建数据库 idc_platform，创建核心表（users, roles, permissions, distributors, products, orders, subscriptions, invoices, payments, sync_logs）。启用 TimescaleDB 扩展并创建 bandwidth_samples hypertable。提交 SQL 到 backend/migrations/ 目录。
  Parallelization: Wave 1 | Blocked by: 1 | Blocks: 后端所有
  References: PostgreSQL 16 文档 - https://www.postgresql.org/docs/16/ , TimescaleDB 文档 - https://docs.timescale.com/
  Acceptance criteria (agent-executable): `psql -d idc_platform -f migrations/001_init.sql` 无报错，所有表存在
  QA scenarios: happy - 表结构完整，外键正确；failure - 重复执行幂等。Evidence .omo/evidence/task-3-idc-platform-v1.sql
  DB迁移策略：采用版本化SQL文件（migrations/001_init.sql, 002_xxx.sql...），每次数据库结构变更创建新版本文件，不修改已发布的迁移文件。
  Commit: Y | feat(db): initialize database schema with core tables

- [x] 4. Docker 开发环境
  What to do / Must NOT do: 创建 docker-compose.yml（PostgreSQL 16 + Redis 7 + Nginx 最新版）。创建 Dockerfile.backend（多阶段编译：第一阶段编译 Drogon 项目，第二阶段最小运行镜像）。创建 nginx/default.conf（/api/ 反代到后端，/ws/ WebSocket 升级，/ 静态文件到 dist）。必须 docker compose up 一键启动全部服务。
  Parallelization: Wave 1 | Blocked by: 1, 2 | Blocks: 部署
  References: Docker Compose v3 文档, Gotenberg - https://gotenberg.dev/
  Acceptance criteria (agent-executable): `docker compose up -d` 启动所有容器（含Goternberg），`curl localhost/api/` 返回200
  QA scenarios: happy - 四容器（PG+Redis+Nginx+Goternberg）正常运行；failure - 端口冲突检测。
  **Gotenberg 服务：** 添加 gotenberg/gotenberg:8 作为 PDF 生成服务（替代 wkhtmltopdf），POST HTML 返回 PDF，CJK支持良好。
  Evidence .omo/evidence/task-4-idc-platform-v1.log
  Commit: Y | chore(docker): add development environment with compose + Gotenberg

### Wave 2: 认证 & 经销商管理 & 产品目录 (第3-4周)

- [ ] 5. JWT 认证 + RBAC 权限系统（后端）
  What to do / Must NOT do: 实现 Drogon JWT 过滤器（jwt_filter.cc），token 签发/刷新/黑名单（Redis存黑名单）。实现 RBAC 过滤器（rbac_filter.cc），基于角色判断接口权限。创建用户注册/登录 API。支持管理员和经销商两种角色。
  Parallelization: Wave 2 | Blocked by: 1, 3 | Blocks: 前端认证
  References: Drogon AOP过滤器文档, JWT RFC 7519
  Acceptance criteria (agent-executable): `POST /api/v1/auth/login` 返回 token，带 token 访问受保护接口正常，无 token 返回 401
  QA scenarios: happy - 登录→签发token→访问受保护接口→刷新token；failure - 过期token返回401，错误密码返回403。**Redis故障防护：如果Redis不可用，认证服务降级为fail-closed（拒绝所有请求，直到Redis恢复），记录告警日志。** Evidence .omo/evidence/task-5-idc-platform-v1.log
  Commit: Y | feat(auth): implement JWT auth and RBAC filters with Redis fail-closed

- [ ] 6. 前端登录+路由守卫+动态路由
  What to do / Must NOT do: 实现登录页（Element Plus 表单组件），Pinia auth store（存token、用户信息、权限）。Vue Router 导航守卫（beforeEach：未登录→/login，已登录自动添加动态路由）。实现 layout 布局组件（侧边栏根据权限动态生成菜单，顶栏显示用户信息+退出）。
  Parallelization: Wave 2 | Blocked by: 2, 5 | Blocks: 前端业务页面
  References: Vue Router 导航守卫 - https://router.vuejs.org/guide/advanced/navigation-guards.html , Pinia文档 - https://pinia.vuejs.org/
  Acceptance criteria (agent-executable): 访问 /login 正常，登录后跳转到首页，未登录访问 /admin 自动跳转/login
  QA scenarios: happy - 登录→跳转首页→动态菜单生成；failure - token失效→跳回登录页。Evidence .omo/evidence/task-6-idc-platform-v1.log
  Commit: Y | feat(frontend): implement auth flow and route guards

- [ ] 7. 经销商管理（后端+前端管理员页面）
  What to do / Must NOT do: 后端：经销商 CRUD API（GET/POST/PUT /api/v1/distributors），组织树 API（GET /distributors/:id/tree 递归返回所有下级），多级关系维护（parent_id 自引用）。前端：管理员→经销商管理页面（Element Plus 表格+树形选择器），创建/编辑表单，组织树可视化组件。
  Parallelization: Wave 2 | Blocked by: 3, 5 | Blocks: 价格模板
  References: PostgreSQL 递归查询（WITH RECURSIVE）用于组织树
  Acceptance criteria (agent-executable): 创建经销商 A，创建下级经销商 B，GET /distributors/A/tree 返回包含B的完整树
  QA scenarios: happy - 创建→修改→删除→查询树；failure - 删除有关联的经销商时拒绝。Evidence .omo/evidence/task-7-idc-platform-v1.log
  Commit: Y | feat(distributor): implement distributor CRUD and organization tree

- [ ] 8. 产品目录+多级价格模板（后端+前端）
  What to do / Must NOT do: 后端：产品 CRUD API（type: bare_metal/cloud/bandwidth/ip/addon，specs: JSONB），价格模板API（模板CRUD，产品定价CRUD，支持继承/覆盖/有效期），价格查询API（根据经销商等级返回对应价格）。前端：管理员→产品管理页面，价格模板配置页，经销商查看价格页面。
  Parallelization: Wave 2 | Blocked by: 3 | Blocks: 订单
  References: PostgreSQL JSONB 查询文档
  Acceptance criteria (agent-executable): 创建产品A基价1000，创建经销商模板覆盖为800，经销商B看到800，非经销商看到1000
  QA scenarios: happy - 设置不同等级价格→各等级看到不同价格；failure - 无效价格模板触发校验。Evidence .omo/evidence/task-8-idc-platform-v1.log
  Commit: Y | feat(product): implement product catalog and multi-tier pricing

### Wave 3: 订单 & 订阅管理 (第5-6周)

- [ ] 9. 购物车+下单流程（后端）
  What to do / Must NOT do: 实现购物车API（添加/删除/修改产品数量、配置规格），下单API（POST /api/v1/orders，将购物车转为订单），订单生命周期状态机（pending → approved → provisioning → active → suspended → terminated），订单列表+详情API。支持含产品配置的复杂订单（如物理机+带宽+IP组合下单）。
  Parallelization: Wave 3 | Blocked by: 7, 8 | Blocks: 计费、支付
  References: Drogon ORM事务教程
  Acceptance criteria (agent-executable): 创建订单→查看订单状态pending→审核通过→状态变为active
  QA scenarios: happy - 组合产品下单→审核→状态流转完成；failure - 库存不足下单被拒绝。**注意：第一期库存使用本地模拟数据（硬编码充足库存），第5波智简魔方对接后再替换为真实库存校验逻辑。** Evidence .omo/evidence/task-9-idc-platform-v1.log
  Commit: Y | feat(order): implement order lifecycle with state machine

- [ ] 10. 前端下单页面+订单管理
  What to do / Must NOT do: 经销商门户：产品选购页面（产品列表+详情+配置面板→加入购物车），购物车组件（展示已选产品/数量/价格），下单页面（确认订单→选择周期→提交）。订单列表页（筛选/分页），订单详情页（状态跟踪、产品明细）。管理员：订单审核页面（待审列表→审核通过/驳回）。
  Parallelization: Wave 3 | Blocked by: 6, 9 | Blocks: -
  References: Element Plus Table/Form/Dialog 文档, Playwright E2E测试 - https://playwright.dev/
  Acceptance criteria (agent-executable): Playwright E2E: 选择产品→加入购物车→提交订单→在订单列表看到→管理员审核
  QA scenarios: happy - 完整下单流程走通；failure - 表单校验不通过时提示准确。Evidence .omo/evidence/task-10-idc-platform-v1.log
  Commit: Y | feat(frontend): implement order flow (cart → submit → review)

- [ ] 11. 订阅/服务实例管理
  What to do / Must NOT do: 订单审核通过后自动创建 Subscription（服务实例）。订阅生命周期管理（暂停/恢复/终止API），订阅列表+详情API。自动续费逻辑（到期前通知+自动扣款）。服务升降配逻辑（提交变更→审核→执行变更→更新订阅）。
  Parallelization: Wave 3 | Blocked by: 9 | Blocks: 计费
  References: Drogon定时任务文档
  Acceptance criteria (agent-executable): 订单审核通过后创建订阅，GET /subscriptions 返回该订阅，PUT /suspend 后状态变为suspended
  QA scenarios: happy - 激活→暂停→恢复→终止全流程；failure - 已终止的订阅不能暂停。Evidence .omo/evidence/task-11-idc-platform-v1.log
  Commit: Y | feat(subscription): implement subscription lifecycle management

### Wave 4: 计费引擎 (第7-8周)

- [ ] 12. 计费引擎核心—月付/年付
  What to do / Must NOT do: 实现周期性计费逻辑：每月1日生成所有活跃订阅的月账单（prorata处理首月不足月情况），年付按年生成。支持自动折扣（年付=月付*10）。计费规则配置化（可配置每月几号出账、付款期限）。实现计费任务调度器（Drogon定时任务，每日检查哪些订阅需要出账）。
  Parallelization: Wave 4 | Blocked by: 11 | Blocks: 账单
  References: Drogon定时任务 - https://drogonframework.github.io/drogon-docs/#/AOP/定时任务
  Acceptance criteria (agent-executable): 创建订阅（月付）→模拟计费日→生成对应月份的Invoice
  QA scenarios: happy - 月付/年付都生成正确金额；failure - 已暂停的订阅不应生成新账单。Evidence .omo/evidence/task-12-idc-platform-v1.log
  Commit: Y | feat(billing): implement recurring monthly/yearly billing

- [ ] 13. 95分位带宽计费
  What to do / Must NOT do: 实现带宽采样数据入库（模拟或对接智简魔方获取5分钟粒度的带宽数据），写入 bandwidth_samples hypertable。月底执行95计费计算：每个订阅一个月内的采样数据排序→去掉前5%→取最高值→乘以单价=带宽费。支持多端口/多IP的分开计费。提供带宽用量查询API和图表数据接口（供前端WebSocket实时推送用）。
  Parallelization: Wave 4 | Blocked by: 3 (TimescaleDB) | Blocks: 账单汇总
  References: TimescaleDB hypertable + percentile_cont文档, DrPeering 95th percentile白皮书
  Acceptance criteria (agent-executable): 插入100条模拟采样数据（已知模式：50个低值+50个高值，95分位 = 第95个样品的值） → 执行算法 → 断言结果 = 预期值。首期带宽数据用模拟生成器，第5波智简魔方对接后接入真实数据源。
  QA scenarios: happy - 算法结果与手动计算一致（提供测试数据/预期值的JSON fixture）；failure - 采样数据不足时降级为平均计费。Evidence .omo/evidence/task-13-idc-platform-v1.log
  Commit: Y | feat(billing): implement 95th percentile bandwidth billing

- [ ] 14. 按量计费+增值服务
  What to do / Must NOT do: 按量计费：计时器（记录云服务器运行时长），费率表（CPU/内存/磁盘/流量单价），月末汇总=时长*费率。增值服务prorata（如额外IP、DDOS防护在月中加购，按剩余天数比例计费）。所有计费记录持久化到 billing_records 表。
  Parallelization: Wave 4 | Blocked by: 11 | Blocks: 账单汇总
  References: - 
  Acceptance criteria (agent-executable): 模拟云服务器运行100小时（单价0.5元/小时）→按量计费=50元
  QA scenarios: happy - 按量精准计费；failure - 并发扣费防超扣（乐观锁）。Evidence .omo/evidence/task-14-idc-platform-v1.log
  Commit: Y | feat(billing): implement hourly metered billing and addon prorata

- [ ] 15. 账单生成+PDF+催缴
  What to do / Must NOT do: 账单汇总结算：合并月付+年付+95带宽+按量+增值→生成一张 Invoice。支持分期调整、折扣、优惠券入口预留。PDF账单生成（使用 libharu 或 wkhtmltopdf 生成中文PDF）。催缴流程：到期前3天邮件/站内信提醒→到期日→逾期第1天、第3天、第7天逐步升级提醒→逾期15天自动暂停服务→逾期30天终止。
  Parallelization: Wave 4 | Blocked by: 12, 13, 14 | Blocks: 支付
  References: Gotenberg - https://gotenberg.dev/ , wkhtmltopdf 已不推荐（最后更新2023-01，CJK渲染不可靠）
  Acceptance criteria (agent-executable): 订阅有月付+带宽→月底生成汇总Invoice→PDF可下载→内容正确
  QA scenarios: happy - 完整对账单全部信息正确；failure - 账单金额为0时不生成。
  **PDF生成：使用 Gotenberg（Docker sidecar 容器，基于现代Chromium，CJK字体内置支持）。** 见 docker-compose.yml 添加 gotenberg 服务。
  计费防并发：使用 PostgreSQL advisory lock 作为计费引擎互斥锁，REPEATABLE READ 隔离级别做快照计费。
  优惠券/折扣：本期仅预留 invoices.discount_amount 和 discount_id 字段，不在第一期实现业务逻辑。
  Evidence .omo/evidence/task-15-idc-platform-v1.log
  Commit: Y | feat(billing): implement invoice generation, PDF and dunning

### Wave 5: 支付 & 智简魔方对接 (第9-10周)

Wave 5 workload note: Task 16（前端账单）已移至 Wave 6 以均衡工作量。

- [ ] 17. 支付宝+微信支付集成
  What to do / Must NOT do: 集成支付宝（alipay-sdk）和微信支付官方 API。支付流程：用户选择账单→点击支付→选择支付宝/微信→后端创建支付订单→返回支付链接/二维码→前端展示→用户扫码支付→异步回调通知→更新账单状态→更新余额。支持退款API。处理支付超时、重复回调、订单关闭等边界情况。
  Parallelization: Wave 5 | Blocked by: 15 | Blocks: 上线
  References: 支付宝开放平台支付API文档, 微信支付商户平台API文档
  Acceptance criteria (agent-executable): 创建支付单→获取支付链接→模拟支付成功回调→账单状态变为paid
  QA scenarios: happy - 完整支付流程走通；failure - 支付金额校验不一致拒绝、重复回调幂等。Evidence .omo/evidence/task-17-idc-platform-v1.log
  Commit: Y | feat(payment): implement Alipay and WeChat payment integration

- [ ] 18. 智简魔方上游对接—产品/库存同步
  What to do / Must NOT do: 实现智简魔方 REST API 客户端（封装HTTP请求、签名、认证）。定时任务（每5分钟）拉取上游产品目录和库存。增量同步策略：比对最后更新时间，只同步变更部分。本地缓存同步结果，提供 API 查询最新产品/库存状态。处理网络异常、上游超时的重试和告警。
  Parallelization: Wave 5 | Blocked by: 1 | Blocks: 上线
  References: 智简魔方API文档 - https://www.idcsmart.com/api_list.html , 智简魔方模块开发文档 - https://www.idcsmart.com/wiki_list/1262.html
  Acceptance criteria (agent-executable): 模拟智简魔方API返回→同步产品到本地→本地数据库包含同步的产品数据
  QA scenarios: happy - 增量同步正常；failure - 上游不可用时自动重试3次后记录失败。Evidence .omo/evidence/task-18-idc-platform-v1.log
  Commit: Y | feat(integration): implement ZJMF upstream product/inventory sync

- [ ] 19. 智简魔方下游对接—订单下发+状态同步
  What to do / Must NOT do: 订单审核通过后→构建智简魔方格式的开通请求→POST到下游智简魔方API→记录下发日志。提供 Webhook 端点接收智简魔方回调（状态变更通知）。异步轮询机制：如果无Webhook，每30秒查询订单状态直到开通完成。绑定本地Subscription和智简魔方remote_resource_id的映射关系。
  Parallelization: Wave 5 | Blocked by: 9, 18 | Blocks: 上线
  References: 智简魔方服务器API - https://www.idcsmart.com/api_list/87.html
  Acceptance criteria (agent-executable): 审核订单→调用智简魔方API→记录请求和响应→映射remote_id和local_id
  QA scenarios: happy - 订单下发→状态回调→本地更新；failure - 智简魔方返回错误→订单标记失败→人工介入。Evidence .omo/evidence/task-19-idc-platform-v1.log
  Commit: Y | feat(integration): implement ZJMF downstream order provisioning

- [ ] 20. 智简魔方同步管理界面
  What to do / Must NOT do: 管理员后台：同步日志查看页面（时间、方向、实体、状态、错误信息），手动触发同步按钮（产品/库存/订单状态），同步异常告警列表。经销商门户：显示资源来自智简魔方的状态标识。
  Parallelization: Wave 5 | Blocked by: 18, 19 | Blocks: -
  References: Element Plus Table 组件
  Acceptance criteria (agent-executable): 同步日志页面显示历史记录→点击手动同步→最新同步记录出现
  QA scenarios: happy - 同步记录完整展示；failure - 同步失败记录红色高亮。Evidence .omo/evidence/task-20-idc-platform-v1.log
  Commit: Y | feat(frontend): implement sync management dashboard

### Wave 6: 前端门户完善 & 报表 & 前端账单 & 上线 (第11-12周)

- [ ] 16. 前端账单页面+带宽图表
  What to do / Must NOT do: 经销商：账单列表页（筛选/分页/状态筛选），账单详情页（金额明细），PDF下载按钮，在线支付入口。带宽用量图表（ECharts 折线图，展示每月带宽使用趋势）。管理员：对所有经销商的账单总览页面。
  Parallelization: Wave 6 | Blocked by: 15 | Blocks: 22
  References: ECharts 折线图文档 - https://echarts.apache.org/ , Playwright E2E测试
  Acceptance criteria (agent-executable): Playwright测试：账单列表显示→点击详情展开→PDF可下载→带宽图表渲染
  QA scenarios: happy - 所有金额显示正确；failure - 无账单时显示空状态。Evidence .omo/evidence/task-16-idc-platform-v1.log
  Commit: Y | feat(frontend): implement billing pages and bandwidth charts

- [ ] 21. 经销商门户首页+产品选购体验优化
  What to do / Must NOT do: 优化经销商门户首页（产品分类导航、促销banner、快速下单入口）。产品选购支持配置器模式（选择产品类型→选择配置→选择周期→查看价格→加入购物车）。价格清晰展示（原价/经销商折扣价/年付优惠高亮）。响应式布局适配。
  Parallelization: Wave 6 | Blocked by: 8, 10 | Blocks: 上线
  References: Element Plus Carousel/Card 组件
  Acceptance criteria (agent-executable): 首页加载→浏览产品→配置器操作流畅→查看价格正确
  QA scenarios: happy - 完整浏览→配置→下单流程；failure - 无权限查看的产品不显示。Evidence .omo/evidence/task-21-idc-platform-v1.log
  Commit: Y | feat(frontend): enhance dealer portal homepage and product configurator

- [ ] 22. 管理员Dashboard+报表
  What to do / Must NOT do: 管理员首页 Dashboard（总经销商数、本月新增订单数、本月收入、待审订单数、逾期账单数，用 ECharts 图表展示趋势）。经销商销售报表（按经销商汇总销售额、订单数、续费率）。营收报表（月收入趋势、产品占比饼图）。带宽使用Top排名。
  Parallelization: Wave 6 | Blocked by: 9, 15 | Blocks: 上线
  References: ECharts 库
  Acceptance criteria (agent-executable): 管理员首页展示所有统计数据→点击报表链接→展示对应图表
  QA scenarios: happy - 所有图表数据与数据库一致；failure - 无数据时显示友好空状态。Evidence .omo/evidence/task-22-idc-platform-v1.log
  Commit: Y | feat(frontend): implement admin dashboard and reports

- [ ] 23. 部署上线文档+系统集成测试
  What to do / Must NOT do: 编写生产部署手册（环境要求、配置文件说明、启动步骤、nginx配置、systemd服务）。编写运维文档（日志查看、数据备份策略、故障恢复流程）。执行自动化集成测试（全API链路测试、计费准确性验证、支付回调测试、智简魔方对接模拟测试）。性能压测（至少模拟100并发用户）。
  Parallelization: Wave 6 | Blocked by: 所有 | Blocks: -
  References: -
  Acceptance criteria (agent-executable): 按部署手册在新环境上完成部署→所有集成测试通过
  QA scenarios: happy - 完整部署+启动+测试流程通过；failure - 部署文档步骤遗漏或错误。Evidence .omo/evidence/task-23-idc-platform-v1.log
  Commit: Y | chore(docs): add deployment and operations documentation

## Final verification wave
> Runs in parallel after ALL todos. ALL must APPROVE. Surface results and wait for the user's explicit okay before declaring complete.
- [ ] F1. Plan compliance audit
- [ ] F2. Code quality review
- [ ] F3. Real manual QA
- [ ] F4. Scope fidelity

## Commit strategy
- 遵循 Conventional Commits (type(scope): description)
- type: feat / fix / chore / docs / refactor / test
- scope: backend / frontend / db / docker / integration
- 每完成一个TODO提交一次
- PR前 rebase squash 到合理的提交粒度

## Success criteria
1. 经销商可以完整走通：注册→登录→浏览产品→下单→查订单→查账单→在线支付
2. 管理员可以：管理经销商/产品/定价→审核订单→查看报表
3. 多级价格：不同级别经销商看到不同价格，价格继承+覆盖正确
4. 计费引擎：月付/年付/95带宽/按量均生成正确账单金额
5. 支付宝/微信支付：支付成功自动核销账单，支付回调幂等
6. 智简魔方：上游产品库存同步正确，下游订单下发成功并跟踪状态
7. 所有API有JWT+RBAC保护，越权访问返回403
8. 性能要求：100并发API请求，95%响应时间<200ms
