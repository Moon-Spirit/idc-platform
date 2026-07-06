<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '@/stores/auth'
import { getAdminDashboardStats, getRevenueReport, type AdminDashboardStats, type RevenueByProductItem } from '@/api/dashboard'
import VChart from 'vue-echarts'
import { use } from 'echarts/core'
import { LineChart, PieChart, BarChart } from 'echarts/charts'
import { TooltipComponent, LegendComponent, GridComponent } from 'echarts/components'
import { CanvasRenderer } from 'echarts/renderers'
import type { EChartsOption } from 'echarts'

use([LineChart, PieChart, BarChart, TooltipComponent, LegendComponent, GridComponent, CanvasRenderer])

// ── Stores ──────────────────────────────────────────────────────────────────

const authStore = useAuthStore()
const router = useRouter()

// ── State ───────────────────────────────────────────────────────────────────

const loading = ref(true)
const error = ref<string | null>(null)
const stats = ref<AdminDashboardStats | null>(null)
const productBreakdown = ref<RevenueByProductItem[]>([])

// ── Stat cards ──────────────────────────────────────────────────────────────

interface StatCard {
  label: string
  value: string | number
  icon: string
  color: string
  clickable?: boolean
  routeName?: string
}

const statCards = computed<StatCard[]>(() => {
  if (!stats.value) return []
  return [
    {
      label: '经销商总数',
      value: stats.value.total_distributors,
      icon: 'UserFilled',
      color: '#409eff',
      clickable: true,
      routeName: 'DistributorList',
    },
    {
      label: '本月订单',
      value: stats.value.new_orders_this_month,
      icon: 'List',
      color: '#67c23a',
      clickable: true,
      routeName: 'AdminOrderList',
    },
    {
      label: '本月收入',
      value: `¥${Number(stats.value.revenue_this_month).toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`,
      icon: 'Coin',
      color: '#e6a23c',
    },
    {
      label: '待审核订单',
      value: stats.value.pending_orders,
      icon: 'WarningFilled',
      color: '#f56c6c',
      clickable: true,
      routeName: 'AdminOrderReview',
    },
  ]
})

// ── ECharts: Revenue trend (line) ──────────────────────────────────────────

const revenueTrendOption = computed<EChartsOption>(() => {
  const trend = stats.value?.revenue_trend ?? []
  return {
    tooltip: { trigger: 'axis' },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: trend.map((p) => p.month),
      boundaryGap: false,
      axisLabel: { fontSize: 11 },
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        formatter: (v: number) => (v >= 10000 ? `${(v / 10000).toFixed(0)}万` : String(v)),
      },
    },
    series: [
      {
        name: '月收入',
        type: 'line',
        smooth: true,
        lineStyle: { width: 3, color: '#409eff' },
        areaStyle: {
          color: {
            type: 'linear',
            x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(64,158,255,0.35)' },
              { offset: 1, color: 'rgba(64,158,255,0.02)' },
            ],
          },
        },
        itemStyle: { color: '#409eff' },
        data: trend.map((p) => Number(p.amount)),
      },
    ],
  }
})

// ── ECharts: Revenue by product type (pie) ─────────────────────────────────

const productPieOption = computed<EChartsOption>(() => {
  const data = productBreakdown.value.length > 0
    ? productBreakdown.value
    : [
        { product_type: '物理机', amount: '0', percentage: 0 },
        { product_type: '带宽', amount: '0', percentage: 0 },
        { product_type: '云服务器', amount: '0', percentage: 0 },
        { product_type: '增值服务', amount: '0', percentage: 0 },
      ]

  return {
    tooltip: {
      trigger: 'item',
      formatter: (params: unknown) => {
        const p = params as { name: string; value: number; percent: number }
        return `${p.name}: ¥${Number(p.value).toLocaleString()} (${p.percent}%)`
      },
    },
    legend: {
      orient: 'vertical',
      right: '5%',
      top: 'center',
      itemWidth: 12,
      itemHeight: 12,
      textStyle: { fontSize: 12 },
    },
    series: [
      {
        type: 'pie',
        radius: ['40%', '65%'],
        center: ['35%', '50%'],
        avoidLabelOverlap: true,
        itemStyle: { borderRadius: 6, borderColor: '#fff', borderWidth: 2 },
        label: { show: false },
        emphasis: {
          label: { show: true, fontSize: 14, fontWeight: 'bold' },
        },
        data: data.map((d) => ({
          name: d.product_type,
          value: Number(d.amount),
        })),
      },
    ],
  }
})

// ── Todo list items ────────────────────────────────────────────────────────

interface TodoItem {
  icon: string
  iconColor: string
  text: string
  actionLabel: string
  routeName: string
}

const todoList = computed<TodoItem[]>(() => {
  if (!stats.value) return []
  const items: TodoItem[] = [
    {
      icon: 'Clock',
      iconColor: '#e6a23c',
      text: `${stats.value.pending_orders} 笔订单待审核`,
      actionLabel: '立即处理',
      routeName: 'AdminOrderReview',
    },
    {
      icon: 'WarningFilled',
      iconColor: '#f56c6c',
      text: `${stats.value.overdue_invoices} 笔账单逾期`,
      actionLabel: '查看',
      routeName: 'AdminInvoiceList',
    },
  ]
  return items
})

function handleTodoClick(routeName: string): void {
  router.push({ name: routeName })
}

// ── Top 5 bandwidth table ──────────────────────────────────────────────────

const bandwidthColumns = [
  { prop: 'distributor', label: '经销商', minWidth: 160 },
  { prop: 'usage_mbps', label: '使用带宽 (Mbps)', minWidth: 140, align: 'right' as const },
]

// ── Data fetching ──────────────────────────────────────────────────────────

async function fetchDashboardData() {
  loading.value = true
  error.value = null
  try {
    const [statsData, revenueData] = await Promise.all([
      getAdminDashboardStats(),
      getRevenueReport({}).catch(() => null),
    ])
    stats.value = statsData
    if (revenueData) {
      productBreakdown.value = revenueData.by_product_type
    }
  } catch (e) {
    error.value = (e as Error).message ?? '加载仪表盘数据失败'
  } finally {
    loading.value = false
  }
}

function handleStatClick(card: StatCard): void {
  if (card.clickable && card.routeName) {
    router.push({ name: card.routeName })
  }
}

// ── Init ──────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchDashboardData()
})
</script>

<template>
  <div class="admin-dashboard" v-loading="loading">
    <!-- Page header -->
    <div class="page-header">
      <h2>Dashboard</h2>
      <p class="welcome-text">
        欢迎回来，{{ authStore.user?.username ?? '管理员' }}
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

    <!-- ── Stat cards ────────────────────────────────────────────────────── -->
    <el-row :gutter="20" class="stats-row">
      <el-col
        v-for="(card, idx) in statCards"
        :key="idx"
        :xs="12"
        :sm="6"
      >
        <el-card
          shadow="hover"
          class="stat-card"
          :class="{ 'stat-card--clickable': card.clickable }"
          :style="{ borderTopColor: card.color }"
          @click="handleStatClick(card)"
        >
          <div class="stat-inner">
            <div class="stat-icon" :style="{ color: card.color, backgroundColor: card.color + '18' }">
              <el-icon :size="28">
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

    <!-- ── Charts row ────────────────────────────────────────────────────── -->
    <el-row :gutter="20" class="charts-row">
      <!-- Line chart: monthly revenue trend -->
      <el-col :xs="24" :lg="14">
        <el-card shadow="never" class="chart-card">
          <template #header>
            <span>月收入趋势</span>
          </template>
          <VChart
            v-if="stats && stats.revenue_trend.length > 0"
            :option="revenueTrendOption"
            autoresize
            class="chart-container"
          />
          <div v-else class="chart-empty">暂无收入趋势数据</div>
        </el-card>
      </el-col>

      <!-- Pie chart: revenue by product type -->
      <el-col :xs="24" :lg="10">
        <el-card shadow="never" class="chart-card">
          <template #header>
            <span>产品收入占比</span>
          </template>
          <VChart
            :option="productPieOption"
            autoresize
            class="chart-container pie-container"
          />
        </el-card>
      </el-col>
    </el-row>

    <!-- ── Bottom row: Todos + Top 5 bandwidth ──────────────────────────── -->
    <el-row :gutter="20" class="bottom-row">
      <!-- Todo list -->
      <el-col :xs="24" :lg="8">
        <el-card shadow="never" class="todo-card">
          <template #header>
            <span>待办事项</span>
          </template>
          <div v-if="todoList.length === 0" class="empty-hint">暂无待办事项</div>
          <div
            v-for="(todo, idx) in todoList"
            :key="idx"
            class="todo-item"
          >
            <div class="todo-left">
              <el-icon :size="20" :style="{ color: todo.iconColor }">
                <component :is="todo.icon" />
              </el-icon>
              <span class="todo-text">{{ todo.text }}</span>
            </div>
            <el-button
              text
              type="primary"
              size="small"
              @click="handleTodoClick(todo.routeName)"
            >
              {{ todo.actionLabel }} &raquo;
            </el-button>
          </div>
        </el-card>
      </el-col>

      <!-- Top 5 bandwidth users -->
      <el-col :xs="24" :lg="16">
        <el-card shadow="never" class="table-card">
          <template #header>
            <span>带宽使用 Top 5</span>
          </template>
          <el-table
            v-if="stats && stats.bandwidth_top5.length > 0"
            :data="stats.bandwidth_top5"
            stripe
            size="small"
            style="width: 100%"
          >
            <el-table-column
              v-for="col in bandwidthColumns"
              :key="col.prop"
              :prop="col.prop"
              :label="col.label"
              :min-width="col.minWidth"
              :align="col.align ?? 'left'"
            >
              <template #default="{ row }">
                <template v-if="col.prop === 'usage_mbps'">
                  {{ row.usage_mbps.toFixed(1) }}
                </template>
                <template v-else>
                  {{ row[col.prop as keyof typeof row] }}
                </template>
              </template>
            </el-table-column>
            <template #empty>
              <el-empty description="暂无数据" />
            </template>
          </el-table>
          <div v-else class="empty-hint">暂无带宽使用数据</div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<style scoped>
.admin-dashboard {
  max-width: 1400px;
}

.page-header {
  margin-bottom: 20px;
}

.page-header h2 {
  font-size: 22px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  margin: 0 0 4px;
}

.welcome-text {
  font-size: 14px;
  color: var(--el-text-color-secondary);
  margin: 0;
}

.error-alert {
  margin-bottom: 20px;
}

/* ── Stat cards ──────────────────────────────────────────────────────────── */

.stats-row {
  margin-bottom: 20px;
}

.stat-card {
  border-top: 3px solid transparent;
  transition: transform 0.2s, box-shadow 0.2s;
  margin-bottom: 20px;
}

.stat-card--clickable {
  cursor: pointer;
}

.stat-card--clickable:hover {
  transform: translateY(-3px);
}

.stat-inner {
  display: flex;
  align-items: center;
  gap: 14px;
}

.stat-icon {
  flex-shrink: 0;
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 12px;
}

.stat-info {
  flex: 1;
}

.stat-value {
  font-size: 22px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  line-height: 1.2;
  margin: 0 0 2px;
}

.stat-label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  margin: 0;
}

/* ── Charts ────────────────────────────────────────────────────────────── */

.charts-row {
  margin-bottom: 20px;
}

.chart-card {
  margin-bottom: 20px;
}

.chart-container {
  width: 100%;
  height: 320px;
}

.pie-container {
  height: 300px;
}

.chart-empty {
  text-align: center;
  padding: 60px 0;
  color: var(--el-text-color-placeholder);
  font-size: 14px;
}

/* ── Bottom row ────────────────────────────────────────────────────────── */

.bottom-row {
  margin-bottom: 20px;
}

.todo-card,
.table-card {
  margin-bottom: 20px;
}

.empty-hint {
  text-align: center;
  padding: 32px 0;
  color: var(--el-text-color-placeholder);
  font-size: 14px;
}

/* ── Todo items ────────────────────────────────────────────────────────── */

.todo-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 0;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.todo-item:last-child {
  border-bottom: none;
}

.todo-left {
  display: flex;
  align-items: center;
  gap: 10px;
}

.todo-text {
  font-size: 14px;
  color: var(--el-text-color-primary);
}
</style>
