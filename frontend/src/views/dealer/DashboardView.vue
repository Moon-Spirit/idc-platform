<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '@/stores/auth'
import { getDashboardStats, type DashboardStats } from '@/api/dashboard'
import { listOrders, type Order } from '@/api/order'
import { listInvoices, type Invoice } from '@/api/invoice'
import { useCartStore } from '@/stores/cart'

// ── Stores ─────────────────────────────────────────────────────────────────

const authStore = useAuthStore()
const cartStore = useCartStore()
const router = useRouter()

// ── Types ──────────────────────────────────────────────────────────────────

interface StatCard {
  label: string
  value: string | number
  icon: string
  color: string
}

// ── State ──────────────────────────────────────────────────────────────────

const loading = ref(true)
const stats = ref<DashboardStats | null>(null)
const recentOrders = ref<Order[]>([])
const recentInvoices = ref<Invoice[]>([])
const error = ref<string | null>(null)

// ── Computed ───────────────────────────────────────────────────────────────

function getStatCards(): StatCard[] {
  if (!stats.value) return []
  return [
    {
      label: '活跃订阅',
      value: stats.value.active_subscriptions,
      icon: 'Monitor',
      color: '#409eff',
    },
    {
      label: '待处理订单',
      value: stats.value.pending_orders,
      icon: 'List',
      color: '#e6a23c',
    },
    {
      label: '未付账单',
      value: stats.value.unpaid_invoices,
      icon: 'Document',
      color: '#f56c6c',
    },
    {
      label: '本月消费',
      value: `¥${Number(stats.value.monthly_spending).toFixed(2)}`,
      icon: 'Coin',
      color: '#67c23a',
    },
  ]
}

// ── Status helpers ─────────────────────────────────────────────────────────

function getOrderStatusTagType(status: string): string {
  const map: Record<string, string> = {
    pending: 'warning',
    approved: 'success',
    rejected: 'danger',
    active: 'success',
    suspended: 'danger',
    terminated: 'info',
  }
  return map[status] ?? 'info'
}

function getOrderStatusLabel(status: string): string {
  const map: Record<string, string> = {
    pending: '待审核',
    approved: '已通过',
    rejected: '已驳回',
    active: '运行中',
    suspended: '已暂停',
    terminated: '已终止',
  }
  return map[status] ?? status
}

function getInvoiceStatusTagType(status: string): string {
  const map: Record<string, string> = {
    unpaid: 'warning',
    paid: 'success',
    overdue: 'danger',
    void: 'info',
  }
  return map[status] ?? 'info'
}

function getInvoiceStatusLabel(status: string): string {
  const map: Record<string, string> = {
    unpaid: '未支付',
    paid: '已支付',
    overdue: '已逾期',
    void: '已作废',
  }
  return map[status] ?? status
}

function formatDate(dateStr: string): string {
  if (!dateStr) return '-'
  const d = new Date(dateStr)
  const year = d.getFullYear()
  const month = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  return `${year}-${month}-${day}`
}

// ── Data fetching ──────────────────────────────────────────────────────────

async function fetchDashboardData() {
  loading.value = true
  error.value = null
  try {
    const [statsData, ordersData, invoicesData] = await Promise.all([
      getDashboardStats(),
      listOrders({ page: 1, per_page: 5 }),
      listInvoices({ page: 1, per_page: 5 }),
    ])
    stats.value = statsData
    recentOrders.value = ordersData.items
    recentInvoices.value = invoicesData.items
  } catch (e) {
    error.value = (e as Error).message ?? '加载仪表盘数据失败'
  } finally {
    loading.value = false
  }
}

// ── Actions ────────────────────────────────────────────────────────────────

const dealerName = authStore.user?.real_name ?? authStore.user?.username ?? ''

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchDashboardData()
  cartStore.fetchCount()
})
</script>

<template>
  <div class="dealer-dashboard" v-loading="loading">
    <!-- Welcome -->
    <div class="welcome-section">
      <h2 class="welcome-title">
        欢迎回来，{{ dealerName || '经销商' }}
      </h2>
      <p class="welcome-subtitle">
        {{ stats ? `账户余额：¥${Number(stats.balance).toFixed(2)}` : '加载中...' }}
      </p>
    </div>

    <!-- Error state -->
    <el-alert
      v-if="error"
      :title="error"
      type="error"
      show-icon
      closable
      class="error-alert"
    />

    <!-- Quick stats cards -->
    <el-row :gutter="20" class="stats-row">
      <el-col
        v-for="(card, index) in getStatCards()"
        :key="index"
        :xs="12"
        :sm="6"
      >
        <el-card shadow="hover" class="stat-card" :style="{ borderTopColor: card.color }">
          <div class="stat-inner">
            <div class="stat-icon" :style="{ color: card.color }">
              <el-icon :size="32">
                <component :is="card.icon" />
              </el-icon>
            </div>
            <div class="stat-info">
              <p class="stat-value">{{ card.value }}</p>
              <p class="stat-label">{{ card.label }}</p>
            </div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Quick actions -->
    <el-card shadow="never" class="actions-card">
      <template #header>
        <span>快捷操作</span>
      </template>
      <div class="action-buttons">
        <el-button
          type="primary"
          size="large"
          @click="router.push({ name: 'ProductList' })"
        >
          <el-icon><ShoppingCart /></el-icon>
          选购产品
        </el-button>
        <el-button
          type="success"
          size="large"
          @click="router.push({ name: 'OrderList' })"
        >
          <el-icon><Tickets /></el-icon>
          查看订单
        </el-button>
        <el-button
          type="warning"
          size="large"
          @click="router.push({ name: 'InvoiceList' })"
        >
          <el-icon><Document /></el-icon>
          我的账单
        </el-button>
      </div>
    </el-card>

    <!-- Recent orders & invoices -->
    <el-row :gutter="20">
      <!-- Recent orders -->
      <el-col :span="14">
        <el-card shadow="never" class="list-card">
          <template #header>
            <div class="card-header">
              <span>最近订单</span>
              <el-link
                type="primary"
                :underline="false"
                @click="router.push({ name: 'OrderList' })"
              >
                查看全部 &raquo;
              </el-link>
            </div>
          </template>
          <div v-if="recentOrders.length === 0" class="empty-hint">暂无订单</div>
          <div
            v-for="order in recentOrders"
            :key="order.id"
            class="list-item"
            @click="router.push({ name: 'OrderDetail', params: { id: order.id } })"
          >
            <div class="list-item-main">
              <span class="list-item-title">{{ order.order_no }}</span>
              <el-tag
                :type="getOrderStatusTagType(order.status)"
                size="small"
                effect="dark"
              >
                {{ getOrderStatusLabel(order.status) }}
              </el-tag>
            </div>
            <div class="list-item-meta">
              <span class="list-item-amount">
                ¥{{ Number(order.total_amount).toFixed(2) }}
              </span>
              <span class="list-item-date">{{ formatDate(order.created_at) }}</span>
            </div>
          </div>
        </el-card>
      </el-col>

      <!-- Recent invoices -->
      <el-col :span="10">
        <el-card shadow="never" class="list-card">
          <template #header>
            <div class="card-header">
              <span>最近账单</span>
              <el-link
                type="primary"
                :underline="false"
                @click="router.push('/dealer/invoices')"
              >
                查看全部 &raquo;
              </el-link>
            </div>
          </template>
          <div v-if="recentInvoices.length === 0" class="empty-hint">暂无账单</div>
          <div
            v-for="invoice in recentInvoices"
            :key="invoice.id"
            class="list-item"
            @click="router.push(`/dealer/invoices/${invoice.id}`)"
          >
            <div class="list-item-main">
              <span class="list-item-title">{{ invoice.invoice_no }}</span>
              <el-tag
                :type="getInvoiceStatusTagType(invoice.status)"
                size="small"
                effect="dark"
              >
                {{ getInvoiceStatusLabel(invoice.status) }}
              </el-tag>
            </div>
            <div class="list-item-meta">
              <span class="list-item-amount">
                ¥{{ Number(invoice.total_amount).toFixed(2) }}
              </span>
              <span class="list-item-date">{{ formatDate(invoice.due_date) }}</span>
            </div>
          </div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<style scoped>
.dealer-dashboard {
  max-width: 1200px;
}

.welcome-section {
  margin-bottom: 24px;
}

.welcome-title {
  font-size: 24px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  margin-bottom: 4px;
}

.welcome-subtitle {
  font-size: 14px;
  color: var(--el-text-color-secondary);
}

.error-alert {
  margin-bottom: 20px;
}

/* ── Stats cards ──────────────────────────────────────────────────────────── */

.stats-row {
  margin-bottom: 20px;
}

.stat-card {
  border-top: 3px solid transparent;
  transition: transform 0.2s, box-shadow 0.2s;
  margin-bottom: 20px;
}

.stat-card:hover {
  transform: translateY(-2px);
}

.stat-inner {
  display: flex;
  align-items: center;
  gap: 16px;
}

.stat-icon {
  flex-shrink: 0;
  width: 52px;
  height: 52px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 12px;
  background-color: rgba(64, 158, 255, 0.08);
}

.stat-info {
  flex: 1;
}

.stat-value {
  font-size: 24px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  line-height: 1.2;
  margin-bottom: 4px;
}

.stat-label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

/* ── Quick actions ─────────────────────────────────────────────────────────── */

.actions-card {
  margin-bottom: 20px;
}

.action-buttons {
  display: flex;
  gap: 16px;
}

.action-buttons .el-button {
  flex: 1;
}

/* ── Lists ─────────────────────────────────────────────────────────────────── */

.list-card {
  margin-bottom: 20px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.empty-hint {
  text-align: center;
  padding: 32px 0;
  color: var(--el-text-color-placeholder);
  font-size: 14px;
}

.list-item {
  display: flex;
  flex-direction: column;
  padding: 12px 0;
  border-bottom: 1px solid var(--el-border-color-lighter);
  cursor: pointer;
  transition: background-color 0.15s;
}

.list-item:last-child {
  border-bottom: none;
}

.list-item:hover {
  background-color: var(--el-fill-color-light);
  margin: 0 -12px;
  padding-left: 12px;
  padding-right: 12px;
  border-radius: 4px;
}

.list-item-main {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
}

.list-item-title {
  font-size: 14px;
  font-weight: 500;
  color: var(--el-text-color-primary);
}

.list-item-meta {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.list-item-amount {
  font-size: 14px;
  font-weight: 600;
  color: #e6a23c;
}

.list-item-date {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}
</style>
