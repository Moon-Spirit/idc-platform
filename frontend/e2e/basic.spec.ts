import { test, expect } from '@playwright/test'

test.describe('Basic application smoke test', () => {
  test('should redirect to login page when not authenticated', async ({ page }) => {
    await page.goto('/')
    await expect(page).toHaveURL(/\/login/)
  })

  test('should show login page with title', async ({ page }) => {
    await page.goto('/login')
    await expect(page.locator('.login-title')).toHaveText('IDC 分销平台')
  })

  test('should have username and password fields on login page', async ({ page }) => {
    await page.goto('/login')
    await expect(page.locator('input[placeholder="请输入用户名"]')).toBeVisible()
    await expect(page.locator('input[placeholder="请输入密码"]')).toBeVisible()
  })

  test('should show validation error on empty login', async ({ page }) => {
    await page.goto('/login')
    await page.locator('button:has-text("登 录")').click()
    await expect(page.locator('.el-form-item__error')).toHaveCount(2)
  })

  test.describe('Login flow', () => {
    const mockUser = {
      id: 1,
      username: 'admin',
      role: 'admin',
      real_name: '管理员',
      email: 'admin@idc.com',
      phone: '13800138000',
      distributor_id: null,
      permissions: ['dashboard:view'],
    }

    const mockLoginResponse = {
      code: 0,
      message: 'success',
      data: {
        token: 'mock-jwt-token',
        expires_in: 86400,
        user: mockUser,
      },
    }

    test('should login successfully and redirect to original page', async ({ page }) => {
      await page.goto('/dashboard')
      await expect(page).toHaveURL(/\/login\?redirect=%2Fdashboard/)

      await page.route('**/api/v1/auth/login', async (route) => {
        await route.fulfill({ json: mockLoginResponse })
      })

      await page.fill('input[placeholder="请输入用户名"]', 'admin')
      await page.fill('input[placeholder="请输入密码"]', 'admin123')
      await page.locator('button:has-text("登 录")').click()

      await expect(page).toHaveURL(/\/dashboard/)
      await expect(page.locator('.user-info')).toContainText('admin')
    })

    test('should show error on wrong credentials', async ({ page }) => {
      await page.goto('/login')

      await page.route('**/api/v1/auth/login', async (route) => {
        await route.fulfill({
          status: 401,
          json: { code: 401, message: 'Invalid username or password', data: null },
        })
      })

      await page.fill('input[placeholder="请输入用户名"]', 'admin')
      await page.fill('input[placeholder="请输入密码"]', 'wrong')
      await page.locator('button:has-text("登 录")').click()

      await expect(page.locator('.el-alert--error')).toContainText('Invalid username or password')
      await expect(page).toHaveURL(/\/login/)
    })

    test('should logout and redirect to login', async ({ page }) => {
      await page.goto('/login')
      await page.route('**/api/v1/auth/login', async (route) => {
        await route.fulfill({ json: mockLoginResponse })
      })

      await page.route('**/api/v1/auth/logout', async (route) => {
        await route.fulfill({ json: { code: 0, message: 'success', data: 'Logged out' } })
      })

      await page.fill('input[placeholder="请输入用户名"]', 'admin')
      await page.fill('input[placeholder="请输入密码"]', 'admin123')
      await page.locator('button:has-text("登 录")').click()
      await expect(page).toHaveURL(/\/dashboard/)

      await page.locator('.user-info').click()
      await page.locator('.el-dropdown-menu__item:has-text("退出登录")').click()

      await expect(page).toHaveURL(/\/login/)
    })
  })
})
