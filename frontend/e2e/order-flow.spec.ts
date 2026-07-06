import { test, expect, type Page } from '@playwright/test'

// ── Mock data ──────────────────────────────────────────────────────────────

const MOCK_USER = {
  id: 1,
  username: 'admin',
  role: 'admin',
  real_name: '管理员',
  email: 'admin@idc.com',
  phone: '13800138000',
  distributor_id: null,
  permissions: ['order:approve', 'product:manage'],
}

const MOCK_LOGIN_RESPONSE = {
  code: 0,
  message: 'success',
  data: {
    token: 'mock-jwt-token',
    expires_in: 86400,
    user: MOCK_USER,
  },
}

const MOCK_PRODUCT_TYPES = {
  code: 0,
  data: ['bare_metal', 'cloud', 'bandwidth', 'ip', 'addon'],
}

const MOCK_PRODUCTS = {
  code: 0,
  data: {
    items: [
      {
        id: 1,
        name: 'E5-2680v4 独服',
        type: 'bare_metal',
        specs: {
          cpu: 'E5-2680v4',
          cores: 14,
          ram: '64GB DDR4 ECC',
          disk: '2×480GB SSD',
          bandwidth: '10G口',
          ip: '/29 5个',
        },
        status: 1,
        sort_order: 1,
      },
      {
        id: 2,
        name: '10G带宽',
        type: 'bandwidth',
        specs: {
          bandwidth: '10Gbps',
          port: '万兆口',
        },
        status: 1,
        sort_order: 2,
      },
    ],
    total: 2,
    page: 1,
    per_page: 20,
  },
}

const MOCK_PRODUCT_DETAIL = {
  code: 0,
  data: {
    id: 1,
    name: 'E5-2680v4 独服',
    type: 'bare_metal',
    specs: {
      cpu: 'E5-2680v4',
      cores: 14,
      ram: '64GB DDR4 ECC',
      disk: '2×480GB SSD',
      bandwidth: '10G口',
      ip: '/29 5个',
    },
    status: 1,
  },
}

const MOCK_PRODUCT_PRICE = {
  code: 0,
  data: {
    product_id: 1,
    product_name: 'E5-2680v4 独服',
    monthly_price: '800.00',
    yearly_price: '8000.00',
    bandwidth_95_price: '45.00',
    setup_fee: '0.00',
    price_source: 'base',
  },
}

const MOCK_CART_DATA = {
  code: 0,
  data: {
    items: [
      {
        id: 1,
        product_id: 1,
        product_name: 'E5-2680v4 独服',
        product_type: 'bare_metal',
        quantity: 1,
        period_months: 1,
        unit_price: '800.00',
        subtotal: '800.00',
        config: { OS: 'CentOS 7.9' },
      },
    ],
    total_amount: '800.00',
  },
}

const MOCK_CART_ADD = {
  code: 0,
  data: { id: 1 },
}

const MOCK_ORDER_LIST = {
  code: 0,
  data: {
    items: [
      {
        id: 1,
        order_no: 'ORD20260701-0001',
        distributor_id: 1,
        distributor_name: '北京XX科技',
        status: 'pending',
        total_amount: '9600.00',
        final_amount: '9600.00',
        billing_cycle: 'yearly',
        item_count: 2,
        created_at: '2026-07-01T10:30:00Z',
      },
    ],
    total: 1,
    page: 1,
    per_page: 20,
  },
}

const MOCK_ORDER_DETAIL = {
  code: 0,
  data: {
    id: 1,
    order_no: 'ORD20260701-0001',
    distributor_id: 1,
    distributor_name: '北京XX科技',
    status: 'pending',
    total_amount: '9600.00',
    final_amount: '9600.00',
    billing_cycle: 'yearly',
    item_count: 2,
    remark: '',
    created_at: '2026-07-01T10:30:00Z',
    updated_at: '2026-07-01T10:30:00Z',
    items: [
      {
        product_name: 'E5-2680v4 独服',
        quantity: 1,
        unit_price: '8000.00',
        subtotal: '8000.00',
      },
      {
        product_name: '/29 IP段',
        quantity: 1,
        unit_price: '600.00',
        subtotal: '600.00',
      },
    ],
    timeline: [
      { status: 'pending', time: '2026-07-01T10:30:00Z', operator: null },
    ],
  },
}

const MOCK_ORDER_SUBMIT = {
  code: 0,
  data: { id: 1, order_no: 'ORD20260701-0001' },
}

const MOCK_EMPTY_CART = {
  code: 0,
  data: {
    items: [],
    total_amount: '0.00',
  },
}

// ── Helpers ────────────────────────────────────────────────────────────────

async function loginAsAdmin(page: Page) {
  await page.goto('/login')

  await page.route('**/api/v1/auth/login', async (route) => {
    await route.fulfill({ json: MOCK_LOGIN_RESPONSE })
  })

  await page.route('**/api/v1/auth/me', async (route) => {
    await route.fulfill({ json: { code: 0, data: MOCK_USER } })
  })

  await page.fill('input[placeholder="请输入用户名"]', 'admin')
  await page.fill('input[placeholder="请输入密码"]', 'admin123')
  await page.locator('button:has-text("登 录")').click()
  await expect(page).toHaveURL(/\/dashboard/, { timeout: 5000 })
}

// ── Tests ──────────────────────────────────────────────────────────────────

test.describe('Order flow: browse → add to cart → submit order → view order', () => {
  test('full order flow', async ({ page }) => {
    // Step 1: Login
    await loginAsAdmin(page)

    // Step 2: Navigate to product list
    await page.route('**/api/v1/products/types', async (route) => {
      await route.fulfill({ json: MOCK_PRODUCT_TYPES })
    })
    await page.route('**/api/v1/products?*', async (route) => {
      await route.fulfill({ json: MOCK_PRODUCTS })
    })

    await page.goto('/dealer/products')
    await expect(page.locator('.product-list')).toBeVisible({ timeout: 5000 })
    await expect(page.locator('.el-tabs__item').first()).toContainText('全部')

    // Step 3: Click on the first product to go to configurator
    await page.route('**/api/v1/products/1', async (route) => {
      await route.fulfill({ json: MOCK_PRODUCT_DETAIL })
    })
    await page.route('**/api/v1/products/1/price', async (route) => {
      await route.fulfill({ json: MOCK_PRODUCT_PRICE })
    })

    await page.locator('.product-card').first().click()
    await expect(page.locator('.configurator')).toBeVisible({ timeout: 5000 })
    await expect(page.locator('.page-title')).toContainText('E5-2680v4')

    // Step 4: Add to cart
    await page.route('**/api/v1/cart/items', async (route) => {
      await route.fulfill({ json: MOCK_CART_ADD })
    })

    await page.locator('button:has-text("加入购物车")').click()
    await expect(page.locator('.el-message')).toContainText('已添加到购物车')

    // Step 5: Navigate to cart
    await page.route('**/api/v1/cart', async (route) => {
      await route.fulfill({ json: MOCK_CART_DATA })
    })

    await page.goto('/dealer/cart')
    await expect(page.locator('.cart-view')).toBeVisible({ timeout: 5000 })
    await expect(page.locator('.el-table')).toContainText('E5-2680v4 独服')
    await expect(page.locator('.el-table')).toContainText('¥800.00')

    // Step 6: Submit order
    await page.route('**/api/v1/orders', async (route) => {
      if (route.request().method() === 'POST') {
        await route.fulfill({ json: MOCK_ORDER_SUBMIT })
      } else {
        await route.fulfill({ json: MOCK_ORDER_LIST })
      }
    })
    await page.route('**/api/v1/orders/1', async (route) => {
      await route.fulfill({ json: MOCK_ORDER_DETAIL })
    })

    // Click submit order
    await page.locator('button:has-text("提交订单")').click()

    // Confirm dialog
    await expect(page.locator('.el-message-box')).toBeVisible()
    await page.locator('.el-message-box button:has-text("确认")').click()

    // Should navigate to order detail
    await expect(page.locator('.order-detail')).toBeVisible({ timeout: 5000 })
    await expect(page.locator('.order-no')).toContainText('ORD20260701-0001')
    await expect(page.locator('.el-steps')).toBeVisible()

    // Step 7: View order list
    await page.route('**/api/v1/orders?*', async (route) => {
      await route.fulfill({ json: MOCK_ORDER_LIST })
    })

    await page.goto('/dealer/orders')
    await expect(page.locator('.order-list')).toBeVisible({ timeout: 5000 })
    await expect(page.locator('.el-table')).toContainText('ORD20260701-0001')
    await expect(page.locator('.el-tag--warning')).toContainText('待审核')

    // Step 8: Admin orders page
    await page.route('**/api/v1/orders*', async (route) => {
      await route.fulfill({ json: MOCK_ORDER_LIST })
    })

    await page.goto('/admin/orders')
    await expect(page.locator('.order-review')).toBeVisible({ timeout: 5000 })
    await expect(page.locator('.el-tag--warning')).toContainText('待审核')

    // Step 9: Approve order from admin panel
    await page.goto('/admin/orders/1')
    await expect(page.locator('.order-detail')).toBeVisible({ timeout: 5000 })

    await page.route('**/api/v1/orders/1/approve', async (route) => {
      await route.fulfill({ json: { code: 0, message: 'success', data: null } })
    })

    // Re-fetch order detail after approve (will return updated order)
    await page.route('**/api/v1/orders/1', async (route) => {
      await route.fulfill({
        json: {
          code: 0,
          data: {
            ...MOCK_ORDER_DETAIL.data,
            status: 'approved',
            timeline: [
              { status: 'pending', time: '2026-07-01T10:30:00Z', operator: null },
              { status: 'approved', time: '2026-07-01T11:00:00Z', operator: 'admin' },
            ],
          },
        },
      })
    })

    await page.locator('button:has-text("审核通过")').click()
    await expect(page.locator('.el-message-box')).toBeVisible()
    await page.locator('.el-message-box button:has-text("确认通过")').click()
    await expect(page.locator('.el-message')).toContainText('已通过')

    // Verify the stepper moved
    await expect(page.locator('.el-step.is-success')).toBeVisible()
  })
})
