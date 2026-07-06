import { test, expect } from '@playwright/test'

test.describe('Basic application smoke test', () => {
  test('should redirect to login page when not authenticated', async ({ page }) => {
    await page.goto('/')
    // Navigation guard should redirect to /login
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
})
