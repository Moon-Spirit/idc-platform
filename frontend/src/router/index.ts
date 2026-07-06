import { createRouter, createWebHistory, type RouteRecordRaw } from 'vue-router'
import Layout from '@/layout/Layout.vue'

const routes: RouteRecordRaw[] = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/LoginView.vue'),
    meta: { title: '登录', noAuth: true },
  },
  {
    path: '/',
    name: 'Layout',
    component: Layout,
    redirect: '/dashboard',
    children: [
      {
        path: 'dashboard',
        name: 'Dashboard',
        component: () => import('@/views/DashboardView.vue'),
        meta: { title: '仪表盘', icon: 'Odometer' },
      },
      // ── Admin: Distributors ────────────────────────────────────────────
      {
        path: 'admin/distributors',
        name: 'DistributorList',
        component: () => import('@/views/admin/distributor/DistributorList.vue'),
        meta: { title: '经销商管理', icon: 'UserFilled' },
      },
      {
        path: 'admin/distributors/:id',
        name: 'DistributorDetail',
        component: () => import('@/views/admin/distributor/DistributorDetail.vue'),
        meta: { title: '经销商详情', hidden: true },
      },
      // ── Admin: Orders ──────────────────────────────────────────────────
      {
        path: 'admin/orders',
        name: 'AdminOrderList',
        component: () => import('@/views/admin/order/OrderReview.vue'),
        meta: { title: '订单管理', icon: 'List' },
      },
      {
        path: 'admin/orders/pending',
        name: 'AdminOrderReview',
        component: () => import('@/views/admin/order/OrderReview.vue'),
        meta: { title: '待审订单', hidden: true },
      },
      {
        path: 'admin/orders/:id',
        name: 'AdminOrderDetail',
        component: () => import('@/views/dealer/order/OrderDetail.vue'),
        meta: { title: '订单详情', hidden: true },
      },
      // ── Dealer: Products ───────────────────────────────────────────────
      {
        path: 'dealer/products',
        name: 'ProductList',
        component: () => import('@/views/dealer/products/ProductList.vue'),
        meta: { title: '产品中心', icon: 'Goods' },
      },
      {
        path: 'dealer/products/:id',
        name: 'ProductConfigurator',
        component: () => import('@/views/dealer/products/ProductConfigurator.vue'),
        meta: { title: '产品详情', hidden: true },
      },
      // ── Dealer: Dashboard ──────────────────────────────────────────────
      {
        path: 'dealer/dashboard',
        name: 'DealerDashboard',
        component: () => import('@/views/dealer/DashboardView.vue'),
        meta: { title: '经销商首页', hidden: true },
      },
      // ── Dealer: Cart ───────────────────────────────────────────────────
      {
        path: 'dealer/cart',
        name: 'CartView',
        component: () => import('@/views/dealer/CartView.vue'),
        meta: { title: '购物车', icon: 'ShoppingCart' },
      },
      // ── Dealer: Orders ─────────────────────────────────────────────────
      {
        path: 'dealer/orders',
        name: 'OrderList',
        component: () => import('@/views/dealer/order/OrderList.vue'),
        meta: { title: '我的订单', icon: 'Tickets' },
      },
      {
        path: 'dealer/orders/:id',
        name: 'OrderDetail',
        component: () => import('@/views/dealer/order/OrderDetail.vue'),
        meta: { title: '订单详情', hidden: true },
      },
      // ── Admin: Invoices ─────────────────────────────────────────────────
      {
        path: 'admin/invoices',
        name: 'AdminInvoiceList',
        component: () => import('@/views/admin/invoice/InvoiceAdmin.vue'),
        meta: { title: '账单管理', icon: 'Document', role: 'admin' },
      },
      {
        path: 'admin/invoices/:id',
        name: 'AdminInvoiceDetail',
        component: () => import('@/views/dealer/invoice/InvoiceDetail.vue'),
        meta: { title: '账单详情', hidden: true, role: 'admin' },
      },
      // ── Dealer: Invoices ────────────────────────────────────────────────
      {
        path: 'dealer/invoices',
        name: 'DealerInvoiceList',
        component: () => import('@/views/dealer/invoice/InvoiceList.vue'),
        meta: { title: '我的账单', icon: 'Document', role: 'dealer' },
      },
      {
        path: 'dealer/invoices/:id',
        name: 'DealerInvoiceDetail',
        component: () => import('@/views/dealer/invoice/InvoiceDetail.vue'),
        meta: { title: '账单详情', hidden: true, role: 'dealer' },
      },
      // ── Admin: Dashboard ────────────────────────────────────────────────
      {
        path: 'admin/dashboard',
        name: 'AdminDashboard',
        component: () => import('@/views/admin/AdminDashboard.vue'),
        meta: { title: '管理后台', icon: 'Odometer', role: 'admin' },
      },
      // ── Admin: Reports ──────────────────────────────────────────────────
      {
        path: 'admin/reports/revenue',
        name: 'ReportRevenue',
        component: () => import('@/views/admin/report/ReportRevenue.vue'),
        meta: { title: '营收报表', icon: 'TrendCharts', role: 'admin' },
      },
      {
        path: 'admin/reports/distributors',
        name: 'ReportDistributor',
        component: () => import('@/views/admin/report/ReportDistributor.vue'),
        meta: { title: '经销商销售报表', icon: 'DataAnalysis', role: 'admin' },
      },
      // ── Admin: Sync ──────────────────────────────────────────────────────
      {
        path: 'admin/sync',
        name: 'SyncDashboard',
        component: () => import('@/views/admin/sync/SyncDashboard.vue'),
        meta: { title: 'ZJMF 同步管理', icon: 'Connection' },
      },
      {
        path: 'admin/sync/logs',
        name: 'SyncLogs',
        component: () => import('@/views/admin/sync/SyncLogs.vue'),
        meta: { title: '同步日志', hidden: true },
      },
      // ── Dealer: Bandwidth ───────────────────────────────────────────────
      {
        path: 'dealer/bandwidth',
        name: 'DealerBandwidth',
        component: () => import('@/views/dealer/BandwidthView.vue'),
        meta: { title: '带宽监控', icon: 'Monitor', role: 'dealer' },
      },
    ],
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

router.beforeEach(async (to, _from) => {
  const { useAuthStore } = await import('@/stores/auth')
  const authStore = useAuthStore()

  if (to.meta.noAuth) {
    return true
  }

  if (!authStore.token) {
    return { name: 'Login', query: { redirect: to.fullPath } }
  }

  // Ensure user info is loaded for role check
  if (!authStore.user) {
    try {
      await authStore.fetchUserInfo()
    } catch {
      return { name: 'Login' }
    }
  }

  // Role-based route guard
  const requiredRole = to.meta.role as string | undefined
  if (requiredRole && authStore.user?.role !== requiredRole) {
    return { name: 'Dashboard' }
  }

  return true
})

export default router
