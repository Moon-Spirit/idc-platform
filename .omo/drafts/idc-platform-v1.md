---
slug: idc-platform-v1
status: approved
intent: clear
pending-action: write .omo/plans/idc-platform-v1.md
approach: IDC分销平台 — 自有资源+分销一体，上下游对接智简魔方，第一期经销商门户+计费引擎+智简魔方双向对接，Drogon(C++)+Vue3+Element Plus+PostgreSQL
---

# Draft: idc-platform-v1

## Components (topology ledger)
<!-- Lock the SHAPE before depth. One row per top-level component that can succeed or fail independently. -->
| id | outcome (one line) | status | evidence path |
|----|-------------------|--------|---------------|
| C1 前端门户 | Vue3 SPA，经销商门户+管理员后台+代理商管理 | active | 用户确认第一期范围 |
| C2 Drogon API后端 | C++17 RESTful API，JWT认证，RBAC权限 | active | Drogon框架推荐并获确认 |
| C3 产品目录&定价引擎 | 物理机+云产品管理，多级价格模板 | active | 用户确认混合产品+深度代理+多级价格 |
| C4 订单&订阅管理 | 购物车、下单流、订单生命周期、服务实例管理 | active | 用户确认经销商门户包含下单 |
| C5 计费引擎 | 月付/年付/95带宽/按量/增值，账单生成+催缴 | active | 用户确认复杂计费模型+第一期包含 |
| C6 支付网关 | 支付宝+微信支付，预充值余额 | active | 用户确认 |
| C7 经销商/代理体系 | 多级组织树，价格继承，佣金结算，子账号 | active | 用户确认深度代理模式 |
| C8 智简魔方适配层 | 上游库存同步+下游订单下发，定时+实时双模式 | active | 用户确认双向对接+第一期包含 |

## Open assumptions (announced defaults)
<!-- Record any default you adopt instead of asking, so the user can veto it at the gate. -->
| assumption | adopted default | rationale | reversible? |
|-----------|----------------|-----------|-------------|
| 后端框架 | Drogon (C++17) | 小团队最佳平衡：ORM成熟、WS内建、社区活跃 | 中（改框架需重写所有路由和ORM） |
| 前端UI库 | Element Plus | 最成熟的中文后台生态，大量模板、中文文档完善 | 是（组件封装后替换成本低） |
| 数据库 | PostgreSQL 16 + TimescaleDB | PG功能全面，TimescaleDB处理95带宽时序计费 | 否（数据结构依赖PG特性） |
| 缓存/队列 | Redis 7 | 会话、价格缓存、乐观锁、WebSocket广播 | 是（可替换为KeyDB等兼容方案） |
| 部署方式 | Nginx + Drogon API（前后端分离） | 性能最优，独立部署灵活，SPA路由正常 | 是 |
| 认证方式 | JWT + RBAC | 无状态、易扩展、支持多级权限 | 是（可加session层） |
| 支付SDK | 支付宝官方SDK + 微信支付官方SDK | 中国大陆标准方案 | 否（需重新对接） |
| 智简魔方对接 | REST API定时同步+实时Webhook | 上游轮询同步库存，下游订单实时下发+回调 | 部分可逆（取决于智简魔方API能力） |

## Findings (cited - path:lines)
- 工作区状态：空项目，仅有.codegraph索引目录
- 智简魔方CBAP v10：开源PHP+MySQL业务系统，提供RESTful API，支持插件扩展
  - API文档：https://www.idcsmart.com/wiki_list/930.html
  - 模块开发文档：https://www.idcsmart.com/wiki_list/1262.html
- 95分位带宽计费标准：5min采样，去顶5%，取最高剩余值；DrPeering行业白皮书确认
- Drogon HTTP/2 benchmark: 1060万req/s @ 64连接，C++框架第一

## Decisions (with rationale)
1. Drogon: 小团队最佳C++ Web框架，ORM成熟度最高，社区14k+⭐，中文资源丰富
2. 第一期单体应用，后续按需拆微服务：小团队不适合一开始就分布式
3. TimescaleDB hypertable保存带宽采样数据，确保95计费查询性能
4. Nginx独立部署，不把静态资源内嵌到C++二进制中

## Scope IN
- 第一期：经销商门户 + 产品目录 + 多级定价 + 订单 + 计费（月付/95带宽/按量）+ 支付 + 智简魔方双向对接

## Scope OUT (Must NOT have)
- ❌ 终端客户自助门户（第一期只服务经销商）
- ❌ 移动端App
- ❌ 智能监控告警（智简魔方DCIMS处理）
- ❌ 多维BI分析
- ❌ 多语言国际化
- ❌ 第三方API开放平台

## Open questions
- 已全部澄清，无剩余问题

## Approval gate
status: approved
<!-- 用户已批准方案，开始生成正式计划 -->
