# 附录B：API接口详细定义

> 通用约定：
> - 基础路径：`/api/v1`
> - 请求体：`application/json`
> - 响应格式：`{ "code": 0, "message": "success", "data": {...} }`
> - 分页：请求参数 `?page=1&per_page=20`，响应 `data: { "items": [...], "total": 100, "page": 1, "per_page": 20 }`
> - 错误码：`400`参数错误 `401`未认证 `403`无权限 `404`不存在 `500`服务器错误
> - 认证：`Authorization: Bearer <jwt_token>`

---

## B.1 认证模块

### POST /api/v1/auth/login
登录获取JWT Token

Request:
```json
{
    "username": "admin",
    "password": "******"
}
```
Response:
```json
{
    "code": 0,
    "message": "success",
    "data": {
        "token": "eyJhbGciOiJIUzI1NiIs...",
        "expires_in": 86400,
        "user": {
            "id": 1,
            "username": "admin",
            "role": "admin",
            "distributor_id": null,
            "permissions": ["order:approve", "product:manage", ...]
        }
    }
}
```
错误码：`400` 参数缺失 | `401` 用户名或密码错误 | `403` 账号已禁用

### POST /api/v1/auth/logout
登出（将当前token加入黑名单）

Headers: `Authorization: Bearer <token>`
Response: `{ "code": 0, "message": "success" }`

### GET /api/v1/auth/me
获取当前登录用户信息

### POST /api/v1/auth/refresh
刷新Token（旧Token未过期时）
Request: `{ "refresh_token": "..." }`

---

## B.2 经销商管理（管理员）

### GET /api/v1/distributors
经销商列表（分页+筛选）

Query: `?page=1&per_page=20&status=1&level=1&keyword=名称/手机/邮箱&parent_id=`
Response:
```json
{
    "code": 0,
    "data": {
        "items": [{
            "id": 1,
            "name": "北京XX科技",
            "level": 1,
            "parent_id": null,
            "parent_name": null,
            "contact_person": "张三",
            "contact_phone": "138xxxx",
            "balance": "10000.00",
            "credit_limit": "50000.00",
            "status": 1,
            "children_count": 3,
            "order_count": 45,
            "total_revenue": "123456.78",
            "created_at": "2026-01-01T00:00:00Z"
        }],
        "total": 50,
        "page": 1,
        "per_page": 20
    }
}
```

### POST /api/v1/distributors
创建经销商

```json
{
    "name": "深圳XX网络",
    "level": 2,
    "parent_id": 1,
    "price_template_id": null,
    "contact_person": "李四",
    "contact_phone": "139xxxx",
    "contact_email": "li@example.com",
    "credit_limit": "30000.00",
    "remark": ""
}
```

### GET /api/v1/distributors/:id
经销商详情（包含统计信息：订单数、收入、活跃服务数、下级数）

### PUT /api/v1/distributors/:id
修改经销商信息

### GET /api/v1/distributors/:id/tree
递归组织树（所有下级，无限层级）

Response:
```json
{
    "code": 0,
    "data": {
        "id": 1,
        "name": "北京XX科技",
        "children": [
            {
                "id": 2,
                "name": "深圳XX网络",
                "children": [
                    { "id": 3, "name": "广州XX工作室", "children": [] }
                ]
            }
        ]
    }
}
```

### GET /api/v1/distributors/:id/children
直接下级列表（仅下一级）

### GET /api/v1/distributors/:id/statistics
经销商统计：本月订单数、本月收入、订阅数、逾期账单数

---

## B.3 价格模板

### GET /api/v1/price-templates
价格模板列表（分页）

### POST /api/v1/price-templates
创建价格模板（可指定继承父模板）

```json
{
    "name": "二级经销商标准价",
    "distributor_id": 2,
    "parent_id": 1,
    "effective_date": "2026-01-01",
    "expiry_date": null
}
```

### GET /api/v1/price-templates/:id
模板详情（含产品定价列表）

### PUT /api/v1/price-templates/:id
### DELETE /api/v1/price-templates/:id

### GET /api/v1/price-templates/:id/products
模板内产品定价列表
Response:
```json
{
    "code": 0,
    "data": [{
        "product_id": 1,
        "product_name": "E5-2680v4 独服",
        "inherited": false,
        "monthly_price": "800.00",
        "yearly_price": "8000.00",
        "bandwidth_95_price": "45.00",
        "setup_fee": "0.00"
    }, {
        "product_id": 2,
        "product_name": "10G带宽",
        "inherited": true,
        "parent_monthly_price": "100.00",  // 继承自父模板
        "monthly_price": "100.00"
    }]
}
```

### PUT /api/v1/price-templates/:id/products/:product_id
设置/覆盖某个产品的定价
```json
{
    "monthly_price": "750.00",
    "yearly_price": "7500.00",
    "bandwidth_95_price": "40.00",
    "setup_fee": "0.00"
}
```

---

## B.4 产品目录

### GET /api/v1/products
产品列表（分页+按类型筛选）

Query: `?type=bare_metal&status=1&keyword=`
Response:
```json
{
    "code": 0,
    "data": {
        "items": [{
            "id": 1,
            "name": "E5-2680v4 独服",
            "type": "bare_metal",
            "specs": {
                "cpu": "E5-2680v4",
                "cores": 14,
                "ram": "64GB DDR4 ECC",
                "disk": "2x480GB SSD",
                "bandwidth": "10G口",
                "ip": "/29 5个"
            },
            "status": 1,
            "sort_order": 1
        }]
    }
}
```

### GET /api/v1/products/:id
产品详情

### POST /api/v1/products
创建产品（管理员）

### PUT /api/v1/products/:id

### DELETE /api/v1/products/:id

### GET /api/v1/products/:id/price
当前用户看到的价格（根据经销商等级自动计算）

Response（管理员看到基价，经销商看到模板价）:
```json
{
    "code": 0,
    "data": {
        "product_id": 1,
        "product_name": "E5-2680v4 独服",
        "monthly_price": "800.00",
        "yearly_price": "8000.00",
        "bandwidth_95_price": "45.00",
        "setup_fee": "0.00",
        "price_source": "price_template:2"  // 来自哪个价格模板
    }
}
```

### GET /api/v1/products/types
产品类型枚举列表
Response: `["bare_metal", "cloud", "bandwidth", "ip", "addon", "rack"]`

---

## B.5 购物车与订单

### POST /api/v1/cart/items
添加产品到购物车
```json
{
    "product_id": 1,
    "quantity": 1,
    "period_months": 12,
    "config": { // 产品配置（可选）
        "bandwidth": "10G",
        "ip_count": 5,
        "os": "CentOS 7.9"
    }
}
```

### GET /api/v1/cart
查看购物车
Response:
```json
{
    "code": 0,
    "data": {
        "items": [{
            "id": 1,
            "product_id": 1,
            "product_name": "E5-2680v4 独服",
            "product_type": "bare_metal",
            "quantity": 1,
            "period_months": 12,
            "unit_price": "800.00",
            "subtotal": "9600.00",
            "config": { "bandwidth": "10G", "ip_count": 5 }
        }],
        "total_amount": "9600.00"
    }
}
```

### DELETE /api/v1/cart/items/:id
删除购物车项

### PUT /api/v1/cart/items/:id
修改购物车项数量或配置

### POST /api/v1/orders
提交订单（购物车→订单）
```json
{
    "billing_cycle": "yearly",     // 覆盖购物车中的周期
    "remark": ""
}
```

### GET /api/v1/orders
订单列表（分页+状态筛选）

Query: `?status=pending&distributor_id=1&page=1&per_page=20`
Response:
```json
{
    "code": 0,
    "data": {
        "items": [{
            "id": 1,
            "order_no": "ORD20260701-0001",
            "distributor_id": 1,
            "distributor_name": "北京XX科技",
            "status": "pending",
            "total_amount": "9600.00",
            "final_amount": "9600.00",
            "billing_cycle": "yearly",
            "item_count": 2,
            "created_at": "2026-07-01T10:30:00Z"
        }]
    }
}
```

### GET /api/v1/orders/:id
订单详情（含订单项、状态流转历史）
```json
{
    "code": 0,
    "data": {
        "id": 1,
        "order_no": "ORD20260701-0001",
        "status": "pending",
        "items": [
            { "product_name": "E5-2680v4", "quantity": 1, "unit_price": "800.00", "subtotal": "9600.00" }
        ],
        "total_amount": "9600.00",
        "timeline": [
            { "status": "pending", "time": "2026-07-01T10:30:00Z", "operator": null },
            { "status": "approved", "time": "2026-07-01T11:00:00Z", "operator": "admin" }
        ]
    }
}
```

### PUT /api/v1/orders/:id/approve
订单审核通过（管理员）
Request: `{ "remark": "资源充足，审核通过" }`

### PUT /api/v1/orders/:id/reject
订单驳回
Request: `{ "reason": "库存不足，请联系客服" }`

### PUT /api/v1/orders/:id/cancel
取消订单（经销商或管理员）

---

## B.6 订阅服务

### GET /api/v1/subscriptions
订阅列表（分页+状态筛选）

Query: `?status=active&distributor_id=1`
Response:
```json
{
    "code": 0,
    "data": {
        "items": [{
            "id": 1,
            "sub_no": "SUB20260701-0001",
            "product_name": "E5-2680v4 独服",
            "distributor_name": "北京XX科技",
            "status": "active",
            "start_date": "2026-07-01",
            "next_billing_date": "2026-08-01",
            "billing_cycle": "monthly",
            "auto_renew": true,
            "provision_status": "done",
            "remote_system": "zjmf_v10",
            "remote_resource_id": "vm-12345"
        }]
    }
}
```

### GET /api/v1/subscriptions/:id
订阅详情

### PUT /api/v1/subscriptions/:id/suspend
暂停服务
Request: `{ "reason": "逾期未付款" }`

### PUT /api/v1/subscriptions/:id/activate
恢复服务
Request: `{ "reason": "已付款" }`

### PUT /api/v1/subscriptions/:id/terminate
终止服务

### POST /api/v1/subscriptions/:id/upgrade
升降配
```json
{
    "specs": { "ram": "128GB", "disk": "4x480GB SSD" },
    "effective_date": "immediate"  // immediate | next_billing
}
```

### GET /api/v1/subscriptions/:id/bandwidth
带宽使用数据
Query: `?start_date=2026-07-01&end_date=2026-07-31&granularity=hour`
Response:
```json
{
    "code": 0,
    "data": {
        "port_id": "eth0",
        "data_points": [
            { "time": "2026-07-01T00:00:00Z", "bytes_in": 123456789, "bytes_out": 987654321, "in_rate_mbps": 32.9, "out_rate_mbps": 263.2 },
            ...
        ],
        "monthly_95th": 263.2,  // 当前月95分位值
        "bandwidth_limit": 1000 // Mbps
    }
}
```

---

## B.7 账单 & 支付

### GET /api/v1/invoices
账单列表（分页+状态筛选）

Query: `?status=unpaid&distributor_id=1&year=2026&month=7`
Response:
```json
{
    "code": 0,
    "data": {
        "items": [{
            "id": 1,
            "invoice_no": "INV20260801-0001",
            "period": "2026-08-01 ~ 2026-08-31",
            "total_amount": "1200.00",
            "paid_amount": "0.00",
            "status": "unpaid",
            "due_date": "2026-08-31",
            "item_count": 3
        }]
    }
}
```

### GET /api/v1/invoices/:id
账单详情（含明细项）
```json
{
    "code": 0,
    "data": {
        "id": 1,
        "invoice_no": "INV20260801-0001",
        "status": "unpaid",
        "items": [
            { "type": "subscription", "description": "E5-2680v4 独服（8月）", "amount": "800.00" },
            { "type": "bandwidth_95", "description": "10G带宽95计费（7月）263Mbps×45元", "amount": "11835.00" },
            { "type": "addon", "description": "额外IP ×5（8月）", "amount": "250.00" }
        ],
        "total_amount": "12885.00",
        "paid_amount": "0.00"
    }
}
```

### GET /api/v1/invoices/:id/pdf
下载PDF账单

### PUT /api/v1/invoices/:id/pay
余额支付
Response: `{ "code": 0, "data": { "balance_before": "20000.00", "balance_after": "7115.00", "paid_amount": "12885.00" } }`

### POST /api/v1/invoices/:id/pay-online
发起在线支付
```json
{
    "method": "alipay"  // alipay | wechat
}
```
Response（支付宝）:
```json
{
    "code": 0,
    "data": {
        "qr_code_url": "https://qr.alipay.com/...",
        "trade_no": "20260701220014..."
    }
}
```

### POST /api/v1/payments/notify
支付异步回调（支付宝/微信）
```
// 支付宝回调格式（application/x-www-form-urlencoded）
```

Response: `success`（返回纯文本给支付宝）或 `fail`

### POST /api/v1/payments/alipay/precreate
### POST /api/v1/payments/wechat/precreate

---

## B.8 智简魔方集成

### POST /api/v1/sync/zjmf/products
手动触发上游产品同步

### POST /api/v1/sync/zjmf/inventory
手动触发库存同步

### GET /api/v1/sync/zjmf/status
查看智简魔方对接状态
```json
{
    "code": 0,
    "data": {
        "connections": [
            { "id": 1, "type": "v10", "direction": "upstream", "status": "active", "last_sync": "2026-07-01T10:00:00Z" },
            { "id": 2, "type": "finance", "direction": "downstream", "status": "active", "last_sync": "2026-07-01T10:00:00Z" }
        ],
        "sync_stats": {
            "total_syncs": 1520,
            "failed_last_24h": 3,
            "pending_orders": 2
        }
    }
}
```

### GET /api/v1/sync/logs
同步日志列表
Query: `?status=failed&entity_type=order&page=1&per_page=20`

### WEBHOOK POST /api/v1/webhooks/zjmf
智简魔方回调端点（接收状态变更通知）
```json
{
    "event": "order.status_changed",
    "order_id": "12345",
    "status": "active",
    "timestamp": "2026-07-01T10:30:00Z",
    "sign": "md5_signature"
}
```
Response: `{ "code": 0, "message": "received" }`

---

## B.9 仪表盘 & 报表

### GET /api/v1/dashboard/stats
管理员Dashboard统计数据
Response:
```json
{
    "code": 0,
    "data": {
        "total_distributors": 48,
        "new_orders_this_month": 156,
        "revenue_this_month": "234567.89",
        "pending_orders": 12,
        "overdue_invoices": 8,
        "active_subscriptions": 342,
        "bandwidth_top5": [
            { "distributor": "北京XX科技", "usage_mbps": 892 },
            ...
        ],
        "revenue_trend": [
            { "month": "2026-01", "amount": "180000.00" },
            ...
        ]
    }
}
```

### GET /api/v1/reports/distributor-sales
经销商销售报表
Query: `?distributor_id=1&start_date=2026-01-01&end_date=2026-07-31`

### GET /api/v1/reports/revenue
营收报表
Query: `?start_date=&end_date=&group_by=month|product_type`

---

## B.10 WebSocket

### WS /api/v1/ws/notifications
用户通知通道（连接时需带token参数）

连接：`wss://host/api/v1/ws/notifications?token=<jwt_token>`
消息格式：
```json
// 服务器→客户端
{
    "type": "order.status_changed",
    "data": { "order_id": 1, "new_status": "active" }
}
{
    "type": "invoice.overdue",
    "data": { "invoice_id": 1, "due_date": "..." }
}
```

### WS /api/v1/ws/bandwidth/:sub_id
带宽实时数据推送（每5秒推一次当前速率）

连接：`wss://host/api/v1/ws/bandwidth/123?token=<jwt_token>`
消息格式：
```json
{
    "type": "bandwidth_sample",
    "data": {
        "time": "2026-07-01T10:30:05Z",
        "in_rate_mbps": 45.2,
        "out_rate_mbps": 123.8
    }
}
```
