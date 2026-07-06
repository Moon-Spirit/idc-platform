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
