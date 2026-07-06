# 附录E：任务细化分解（23→40个原子任务）

> 每个原子任务= 2-3人天工作量，含实现+测试。
> 格式：[Wave] [原任务号] 细分任务号 — 描述

---

## Wave 1：项目骨架 & 基础架构（第1-2周 → 8个原子任务）

### 原1 → 拆为3个任务
- [ ] **1. 后端项目骨架 + Drogon 基础服务**
  - init CMakeLists.txt（FetchContent 拉取 Drogon）
  - main.cc：创建 HttpAppFramework，监听 0.0.0.0:8080
  - config.json：多线程（4 worker）、日志格式、静态文件路径
  - 验证：`curl localhost:8080/` 返回 200 + 编译 0 warning
  - 测试：Drogon test case 基础 setup

- [ ] **2. ORM 模型生成 + 数据库连接 + API 公共组件**
  - drogon_ctl create model 生成所有模型文件
  - db_client 连接池配置（PG：localhost:5432, idc_platform）
  - API 公共响应类：JsonResponse(code, message, data)
  - 分页工具类、错误码枚举、参数校验基类
  - 测试：连接数据库 + CRUD 一个测试表

- [ ] **3. 前端 Vite + Vue3 + Element Plus + Playwright 初始化**
  - `npm create vite@latest frontend -- --template vue-ts`
  - 安装 element-plus、pinia、vue-router、echarts、playwright
  - vite.config.ts：proxy /api→localhost:8080，/ws→ws://localhost:8080
  - 基础布局 Layout（el-container + el-aside + el-header + el-main）
  - 登录页路由 + 空首页
  - Playwright 安装 + 基本 E2E 示例测试
  - 验证：`npm run dev` 启动，Playwright 测试能打开页面

### 原4 → 拆为2个任务
- [ ] **4. Docker Compose 开发环境**
  - docker-compose.yml：postgres:16 + redis:7 + nginx:alpine
  - Dockerfile.backend：多阶段编译（builder→runner）
  - nginx/default.conf：/api/ 反代 + /ws/ upgrade + /dist 静态文件
  - 验证：`docker compose up -d` 三容器正常运行

- [ ] **5. 数据库初始化 SQL + 迁移策略**
  - migrations/001_init.sql：16张核心 DDL（见 appendix-db.md）
  - TimescaleDB 启用 + bandwidth_samples hypertable
  - 迁移 README：如何创建新版本迁移文件
  - 验证：`psql -f migrations/001_init.sql` 无报错

### 新增原子任务
- [ ] **6. Redis 连接 + 工具层**
  - Drogon RedisClient 连接配置
  - Redis 管理器封装（set/get/del/expire）
  - 分布式锁工具（用于计费防并发）
  - 验证：Redis 读写测试

- [ ] **7. 日志系统 + 错误处理中间件**
  - 基于 Drogon 日志框架配置分级日志（debug/info/warn/error）
  - 全局异常处理中间件（catch → JSON 错误响应）
  - 请求日志中间件（method + path + status + duration）
  - 验证：访问不存在路径返回 `{"code":404,"message":"not found"}`

- [ ] **8. Makefile / 开发脚本**
  - `make backend-build` `make backend-run` `make frontend-dev`
  - `make db-migrate` `make test` `make docker-up`
  - 验证：所有脚本可用

---

## Wave 2：认证 & 经销商管理 & 产品目录（第3-4周 → 8个原子任务）

### 原5 → 拆为2个任务
- [ ] **9. JWT 认证后端**
  - jwt_filter.cc：签发、验证、刷新、黑名单（Redis）
  - `POST /api/v1/auth/login` — 用户名密码→签发token
  - `GET /api/v1/auth/me` — token→用户+权限
  - `POST /api/v1/auth/logout` — token加入黑名单
  - Redis故障降级：fail-closed（拒绝请求直到Redis恢复）
  - 测试：正常登录、错误密码、过期token、黑名单验证

- [ ] **10. RBAC 权限系统后端**
  - rbac_filter.cc：根据 role_id + permission_code 判断
  - 管理员默认拥有所有权限
  - 经销商默认权限：order:view, invoice:view, payment:pay
  - 权限可分配到角色层级
  - 测试：越权访问返回 403

### 原6 → 拆为1个任务
- [ ] **11. 前端登录页 + Auth Store + 路由守卫**
  - Pinia auth store（token持久化localStorage，用户信息，权限列表）
  - 登录页 el-form 组件（用户名+密码+登录按钮+loading状态）
  - Vue Router beforeEach 守卫：未登录→/login
  - 管理员布局 vs 经销商布局（不同侧边栏菜单）
  - Playwright E2E：登录→跳转首页→退出→跳回登录页
  - 测试：Vue Test Utils store 单元测试

### 原7 → 拆为2个任务
- [ ] **12. 经销商 API 后端**
  - `CRUD /api/v1/distributors` + 组织树 API
  - 递归查询（WITH RECURSIVE）返回无限层级
  - 多级关系维护：parent_id 自引用，不能循环引用（校验）
  - 余额操作接口（充值/扣减，Redis 乐观锁防并发）
  - 测试：创建→修改→删除→树查询，循环引用拒绝

- [ ] **13. 经销商管理前端页面**
  - 管理员：经销商列表（el-table + 搜索+分页）
  - 创建/编辑经销商（el-dialog + el-form）
  - 组织树可视化（el-tree + 递归渲染）
  - 经销商详情页（基本信息+统计数据+树）
  - Playwright：创建经销商→验证列表显示

### 原8 → 拆为2个任务
- [ ] **14. 产品管理 API + 前端**
  - `CRUD /api/v1/products`（支持 bare_metal/cloud/bandwidth/ip/addon）
  - specs JSONB 通过应用层验证（不同type不同schema）
  - 前端产品列表（管理员：el-table + 类型标签+筛选）
  - 前端产品创建/编辑表单（动态表单根据type变化）
  - 测试：创建各类型产品→查询→修改

- [ ] **15. 价格模板 API + 前端**
  - `CRUD /api/v1/price-templates` + 产品定价设置
  - 价格继承逻辑：子模板未覆盖的→继承父模板→继承全局默认
  - `GET /api/v1/products/:id/price` — 根据当前用户返回对应价格
  - 前端价格模板编辑页（el-table 行内编辑+继承标识）
  - 测试：设置三级价格→各等级用户看到不同价格
  - Playwright：管理员设置价格→经销商登录看到不同价

---

## Wave 3：订单 & 订阅管理（第5-6周 → 6个原子任务）

### 原9 → 拆为2个任务
- [ ] **16. 购物车 + 下单 API**
  - 购物车 CRUD（Redis存储购物车数据，用户维度）
  - `POST /api/v1/orders` — 购物车→订单（事务中完成）
  - 订单编号生成规则：ORD+YYMMDD+流水号
  - 下单时校验：产品存在、价格正确、库存充足（模拟库存）
  - 测试：添加购物车→修改→提交订单→检查数据库

- [ ] **17. 订单状态机 + 订单 API**
  - 订单生命周期：pending → approved → provisioning → active → suspended → terminated
  - 状态转换权限控制（只有管理员可 approve）
  - 订单审核流：`PUT /orders/:id/approve` + `reject`
  - 订单列表（分页+状态筛选）+ 详情 API
  - 测试：完整状态流转测试（每个转态转换写断言）
  - 测试：非法状态转换被拒绝

### 原10 → 拆为1个任务
- [ ] **18. 前端下单流程**
  - 产品选购页：产品列表→点击→详情+配置面板
  - 配置器：选择OS/IP/带宽等选项，实时计算价格
  - 购物车组件：侧边栏抽屉(el-drawer)
  - 下单页：确认订单→选择周期→填写备注→提交
  - 订单列表+详情（状态标签、时间线el-timeline）
  - Playwright E2E：浏览→选配→加入购物车→下单→查看订单

### 原11 → 拆为2个任务
- [ ] **19. 订阅管理 API**
  - 订单审核通过后自动创建订阅（Drogon after_commit hook）
  - 订阅编号：SUB+YYMMDD+流水号
  - 订阅生命周期：暂停/恢复/终止
  - 自动续费检测（定时任务：每日检查7天内到期订阅）
  - 升降配 API：变更规格→审核→执行变更
  - 测试：订阅创建→暂停→恢复→终止全流程

- [ ] **20. WebSocket 通知通道**
  - Drogon WebSocketController：`/api/v1/ws/notifications`
  - 连接鉴权：URL参数 token→解析→绑定用户
  - 通知推送：订单状态变更、账单到期、同步结果
  - 心跳保活（每30s ping/pong）
  - 测试：WebSocket 连接→接收通知→断开重连

---

## Wave 4：计费引擎（第7-8周 → 5个原子任务）

### 原12→14 → 拆为2个任务
- [ ] **21. 月付/年付计费 + 按量计费**
  - 计费调度器（Drogon Cron）：每月1日00:00执行
  - 月付逻辑：活跃订阅→上月天数比例计算→生成 billing_records
  - 年付逻辑：下单时已付年费→到期前30天生成续费提醒
  - 首月prorata设置：例如月中开通，按剩余天数/当月天数
  - 按量计费：云服务器计时器→月末汇总时长×费率
  - 测试：月初创建订阅→模拟计费→验证金额

- [ ] **22. 95分位带宽计费**
  - 带宽采样数据入库API（或模拟数据生成器写入TimescaleDB）
  - 95计费算法：所有5min采样→按值降序排列→排除最高5%→取剩余最高
  - 按端口分别计算，端口级汇总到订阅级
  - 提供带宽查询API + 计费明细
  - 测试：100个已知样本→断言95分位值正确
  - 边界：采样不足时降级为最大值计费

### 原15 → 拆为2个任务
- [ ] **23. 账单生成 + PDF**
  - 账单结算：订阅级别 billing_records → 汇总为 Invoice
  - 合并月付+按量+95带宽+增值服务到一张账单
  - PDF 生成（wkhtmltopdf）：HTML模板 → 渲染 → 中文支持
  - 账单编号：INV+YYMMDD+流水号
  - 测试：多类型计费→合并账单→金额核对
  - 测试：PDF 中文渲染正常

- [ ] **24. 催缴流程**
  - 催缴时间线：到期前3天→到期日→逾期1/3/7/15/30天
  - 催缴动作：站内通知（WebSocket）→暂停服务→终止
  - 暂停/终止订阅的自动化调用
  - 可配置催缴规则（system_config 表）
  - 测试：设置short dunning schedule→验证时间线触发

### 原16 → 移到 Wave 6
- [ ] **(25. 账单前端页面 — 见Wave 6)**

---

## Wave 5：支付 & 智简魔方对接（第9-10周 → 7个原子任务）

### 原17 → 拆为2个任务
- [ ] **26. 支付宝支付集成**
  - 支付宝开放平台 SDK 集成（CURL调用）
  - `POST /payments/alipay/precreate` — 创建支付单→返回二维码
  - `POST /payments/notify` — 异步回调处理（验签+幂等+更新状态）
  - 支付超时处理：30分钟未支付→关闭订单
  - 退款API：`POST /payments/:id/refund`
  - 测试：模拟回调→验证账单状态变化

- [ ] **27. 微信支付集成**
  - 微信支付 Native SDK 集成
  - `POST /payments/wechat/precreate` — 创建支付→返回code_url
  - `POST /payments/notify` — 回调处理（与支付宝统一返回格式）
  - 前端支付组件：展示二维码（vue-qrcode）
  - 轮询支付状态：每3秒 `GET /invoices/:id` 检查状态
  - 测试：模拟微信回调→验证

### 原18 → 拆为2个任务
- [ ] **28. 智简魔方通用 API 客户端**
  - ZJMFAdapter 抽象基类（HTTP 请求封装、签名、认证、重试）
  - ZJMFV10Adapter：魔方v10 API 实现
  - ZJMFFinanceAdapter：魔方财务 API 实现
  - 连接管理器（ConnectionManager）：多实例管理、健康检查
  - 测试：mock 智简魔方 API→验证请求格式正确

- [ ] **29. 上游产品/库存同步实现**
  - 定时任务（每5分钟）：调用智简魔方产品列表→增量更新本地 products
  - 定时任务（每1分钟）：拉取库存→更新本地库存表
  - 字段映射：智简魔方字段→本地字段（可配置映射规则）
  - 同步日志记录（zjmf_sync_logs）+ 失败重试
  - 测试：mock 上游返回→验证本地数据同步正确

### 原19 → 拆为2个任务
- [ ] **30. 下游订单下发实现**
  - 订单审核通过→构建智简魔方开通请求→调用远程API
  - 异步轮询：每30秒查开通状态→更新本地 subscription
  - Webhook 端点：接收智简魔方状态回调→验签→更新
  - 实体映射维护（zjmf_entity_mappings）
  - 测试：mock 下游返回→验证本地状态更新

- [ ] **31. 智简魔方同步管理前端**
  - 同步仪表盘：连接状态、最后同步时间、失败统计
  - 同步日志页：el-table 展示（方向/实体/状态/错误/时间）
  - 手动触发同步按钮（产品/库存/订单状态）
  - 异常告警列表（失败次数>3红色高亮）
  - Playwright：查看同步日志→手动触发→验证新日志出现

---

## Wave 6：前端优化 & 部署（第11-12周 → 6个原子任务）

### 原16（移至此）
- [ ] **32. 账单管理前端页面**
  - 经销商账单列表（el-table：状态标签+金额+到期日+搜索筛选）
  - 账单详情（el-descriptions 展示） + 明细表格
  - PDF下载按钮 + 支付按钮（支付宝/微信/余额三选）
  - 管理员账单总览（所有经销商）
  - 带宽用量图表（ECharts 折线图 + 时间范围选择器）
  - 余额支付 el-dialog + 支付密码（可选）
  - Playwright：查看账单→支付→验证状态变化

### 原21（原6.1）
- [ ] **33. 经销商门户首页优化**
  - 产品分类导航（卡片式 el-card 网格布局）
  - 快速下单入口 + 促销banner
  - 服务使用概览（活跃服务数、本月消费）
  - 最新订单/账单动态列表

### 原22（原6.2）
- [ ] **34. 管理员 Dashboard 前端**
  - 4个统计卡片（经销商数、订单数、收入、待审）
  - 月收入趋势折线图（ECharts）
  - 产品占比饼图
  - 带宽使用 Top 排名（el-table）
  - 待办事项列表（待审订单+逾期账单+同步异常）

- [ ] **35. 报表页面**
  - 经销商销售报表（el-table + 按时间段筛选 + 导出CSV）
  - 营收报表（按产品类型分组 + 趋势图）
  - 使用 ECharts 展示趋势

### 原23（原6.3）
- [ ] **36. 系统集成测试**
  - 全 API 链路自动化测试（200+ 测试用例）
  - 计费准确性测试（月付/年付/95带宽/按量）
  - 支付回调循环测试（支付宝+微信）
  - 智简魔方对接模拟测试
  - 性能压测（100并发，95%响应<200ms）
  - Playwright E2E 全流程测试

- [ ] **37. 部署上线 + 文档**
  - 生产部署手册（环境要求、配置文件、启动步骤、nginx、systemd）
  - 运维文档（日志、备份、恢复、监控、告警）
  - `.env.production` 示例文件
  - 一键部署脚本（deploy.sh）
  - 验证：按文档在新服务器从头部署→全部服务正常运行

### 高精度审查阶段
- [ ] **38. Momus 高精度审查**
  - 计划合规性审查：每个任务是否完整（References + AC + QA + Commit）
  - 架构审查：组件依赖合理性、数据库设计规范
  - 安全审查：认证、权限、密钥管理、数据加密
  
- [ ] **39. Oracle 独立审查**
  - 隔离的 Oracle 子会话，从架构/安全/完整性角度独立审查
  - 输出审查报告 + 需修复问题列表

---

## 任务统计

| Wave | 原子任务数 | 周数 | 任务/周 | 预估工作量(人天) |
|------|-----------|------|---------|----------------|
| 1 | 8 | 2 | 4 | 16-24 |
| 2 | 8 | 2 | 4 | 16-24 |
| 3 | 6 | 2 | 3 | 12-18 |
| 4 | 4 (+计费特殊) | 2 | 2-3 | 12-16 |
| 5 | 7 | 2 | 3-4 | 14-21 |
| 6 | 8 (含审查) | 2 | 4 | 16-24 |
| **合计** | **41** | **12** | **~3.5** | **86-127** |

> 2人团队每人每周5天 → 12周 = 120人天，与估算匹配。
