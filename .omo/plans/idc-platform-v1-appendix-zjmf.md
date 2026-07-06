# 附录C：智简魔方（ZJMF）对接详细设计

> 智简魔方有两个产品线：**魔方v10 (CBAP)** 和 **魔方财务 (Finance)**。
> 你的业务中上下游都可能用到这两个产品，所以对接层需要兼容两种协议。

---

## C.1 产品概览

| 产品 | 类型 | 语言 | 定位 | API类型 |
|------|------|------|------|---------|
| **魔方v10 (CBAP)** | 开源业务系统 | PHP + MySQL | 订单、产品、客户管理，有插件生态 | REST API + Webhook |
| **魔方财务 (Finance)** | 商业版财务系统 | PHP + MySQL | 计费、账单、支付、代理商结算 | REST API + Webhook |

两者API风格相似（都是智简魔方体系），但接口路径和参数有差异。

---

## C.2 对接架构

```
┌────────────────────────────────────────────────────┐
│                    你的系统                          │
│  ┌──────────────────────────────────────────────┐  │
│  │          智简魔方适配层 (ZJMF Adapter)          │  │
│  │                                               │  │
│  │  ┌──────────────┐  ┌──────────────────────┐   │  │
│  │  │ v10 API Client│  │ Finance API Client   │   │  │
│  │  │ - 产品同步     │  │ - 账单同步            │   │  │
│  │  │ - 订单开通     │  │ - 支付状态同步         │   │  │
│  │  │ - 库存查询     │  │ - 客户同步            │   │  │
│  │  │ - 资源管理     │  │ - 结算数据            │   │  │
│  │  └──────┬───────┘  └──────────┬───────────┘   │  │
│  │         │                     │                │  │
│  │  ┌──────┴─────────────────────┴───────────┐   │  │
│  │  │ 统一连接管理器 (Connection Manager)      │   │  │
│  │  │ - 连接配置管理                           │   │  │
│  │  │ - API密钥加密存储                        │   │  │
│  │  │ - 超时/重试策略                          │   │  │
│  │  │ - 速率限制                               │   │  │
│  │  │ - 健康检查                               │   │  │
│  │  └────────────────────────────────────────┘   │  │
│  └──────────────────────────────────────────────┘  │
└─────────────────────┬──────────────────────────────┘
                      │
         ┌────────────┼────────────┐
         ▼            ▼            ▼
   ┌──────────┐ ┌──────────┐ ┌──────────┐
   │上游魔方v10│ │上游魔方   │ │下游魔方   │
   │          │ │ 财务     │ │ v10/财务  │
   └──────────┘ └──────────┘ └──────────┘
```

---

## C.3 魔方v10 API对接

### 认证方式
智简魔方 API 使用 API Key + IP 白名单的方式进行认证（部分接口可能需要签名）。

```
Headers:
  API-Key: <api_key>
  API-Secret: <api_secret> (部分接口需要)
```

### 需要对接的v10接口

#### 上游：产品/库存同步

| 功能 | 智简魔方API | 同步方式 | 频率 |
|------|-----------|---------|------|
| 获取产品列表 | `GET /api/v1/products` | 定时任务拉取 | 每5分钟 |
| 获取产品详情 | `GET /api/v1/products/:id` | 按需 | - |
| 获取库存 | `GET /api/v1/inventory` | 定时任务拉取 | 每1分钟 |
| 获取服务器列表 | `GET /api/servers` | 定时任务 | 每5分钟 |
| 获取IP资源池 | `GET /api/ips` | 定时任务 | 每5分钟 |

#### 下游：订单下发/开通

| 功能 | 智简魔方API | 触发方式 |
|------|-----------|---------|
| 创建服务器 | `POST /api/servers` | 订单审核通过后即时调用 |
| 查询开通状态 | `GET /api/servers/:id` | 异步轮询（每30秒） |
| 暂停服务器 | `PUT /api/servers/:id/suspend` | 订阅暂停时调用 |
| 恢复服务器 | `PUT /api/servers/:id/activate` | 订阅恢复时调用 |
| 终止服务器 | `DELETE /api/servers/:id` | 订阅终止时调用 |
| 重装系统 | `POST /api/servers/:id/reinstall` | 经销商操作 |
| 开关机/重启 | `POST /api/servers/:id/power` | 经销商操作 |

#### 下单参数示例（POST /api/servers）
```json
{
    "client_id": "remote_client_id",
    "product_id": "remote_product_id",
    "hostname": "srv001.example.com",
    "os": "centos-7.9",
    "bandwidth": 1000,
    "ip_count": 5,
    "notes": "来自分销平台订单 ORD20260701-0001"
}
```

---

## C.4 魔方财务 API对接

### 需要对接的财务接口

#### 上游：财务数据同步

| 功能 | 魔方财务API | 同步方式 |
|------|------------|---------|
| 获取客户列表 | `GET /api/clients` | 定时同步 |
| 获取账单 | `GET /api/invoices` | 定时同步 |
| 获取交易记录 | `GET /api/transactions` | 定时同步 |

#### 下游：订单下发（经销商在智简魔方财务中开单）

| 功能 | 魔方财务API | 触发方式 |
|------|------------|---------|
| 创建客户 | `POST /api/clients` | 新经销商注册时 |
| 创建账单 | `POST /api/invoices` | 本系统计费完成后 |
| 查询账单 | `GET /api/invoices/:id` | 按需 |
| 录入付款 | `POST /api/transactions` | 支付到账后同步 |

#### 账单下发参数示例（POST /api/invoices）
```json
{
    "client_id": "remote_client_id",
    "date": "2026-08-01",
    "duedate": "2026-08-31",
    "paymentmethod": "alipay",
    "items": [
        { "description": "E5-2680v4 独服（8月）", "amount": "800.00", "taxed": false },
        { "description": "带宽95计费（7月）", "amount": "11835.00", "taxed": false }
    ]
}
```

---

## C.5 对接策略汇总

### C.5.1 数据同步矩阵

| 数据方向 | 智简魔方产品 | 同步内容 | 同步策略 | 频率 |
|---------|------------|---------|---------|------|
| 上游→本系统 | v10 | 产品目录 | 定时轮询，增量（按updated_at） | 每5分钟 |
| 上游→本系统 | v10 | 服务器库存 | 定时轮询 | 每1分钟 |
| 上游→本系统 | v10 | IP资源池 | 定时轮询 | 每5分钟 |
| 上游→本系统 | 财务 | 客户信息 | 定时轮询 | 每10分钟 |
| 上游→本系统 | 财务 | 账单/交易 | 定时轮询 | 每10分钟 |
| 本系统→下游 | v10/财务 | 订单开通 | 即时API调用 | 实时 |
| 本系统→下游 | v10/财务 | 账单推送 | 即时API调用 | 实时 |
| 下游→本系统 | v10/财务 | 开通状态 | 异步轮询+Webhook | 每30秒/即时 |

### C.5.2 错误处理策略

| 场景 | 处理方式 |
|------|---------|
| 上游API超时 | 重试3次（间隔1s、2s、4s），全部失败则记录失败日志，标记为"同步异常" |
| 上游返回错误 | 解析错误码，记录到zjmf_sync_logs，触发管理员告警 |
| 下游开通失败 | 订单状态置为"provisioning_failed"，停止自动重试（防止重复扣费），通知人工介入 |
| Webhook验证失败 | 签名校验不通过直接返回403，不处理 |
| 网络中断 | 任务队列积压，恢复后按时间顺序补执行（增量同步不受影响） |

### C.5.3 安全策略

- API密钥在数据库中使用 AES-256 加密存储
- 所有出站API请求限制IP白名单（只允许从指定出口IP调用）
- 入站Webhook请求验证签名（HMAC-SHA256）
- 敏感信息（密码、密钥）不出现在日志中（脱敏处理）

---

## C.6 对接配置示例

### 数据库连接配置（zjmf_connections表数据示例）

| name | type | direction | api_base_url | sync_interval |
|------|------|-----------|-------------|---------------|
| 上游机房v10 | v10 | upstream | https://dcim.zj upstream.com/api | 300 |
| 上游财务 | finance | upstream | https://finance.upstream.com/api | 600 |
| 下游经销商A | v10 | downstream | https://dealer-a.com/api | - |
| 下游经销商B | finance | downstream | https://dealer-b.com/api | - |

### 代码架构（适配层类图）

```
ZJMFAdapter (抽象基类)
├── ZJMFV10Adapter
│   ├── syncProducts()
│   ├── syncInventory()
│   ├── createServer(order)
│   ├── queryServerStatus(remoteId)
│   ├── suspendServer(remoteId)
│   └── terminateServer(remoteId)
└── ZJMFFinanceAdapter
    ├── syncClients()
    ├── syncInvoices()
    ├── createInvoice(invoice)
    ├── recordPayment(transaction)
    └── queryInvoice(remoteId)

ZJMFConnectionManager
├── getAdapter(connectionId) → ZJMFAdapter
├── testConnection(connectionId) → bool
├── getSyncStatus() → SyncStatus[]
└── triggerSync(connectionId, entityType)
```
