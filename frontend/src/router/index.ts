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

  return true
})

export default router
