<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Search, Download } from '@element-plus/icons-vue'
import { getRevenueReport, type RevenueReportItem, type RevenueByProductItem } from '@/api/dashboard'
import VChart from 'vue-echarts'
import { use } from 'echarts/core'
import { BarChart, LineChart } from 'echarts/charts'
import { TooltipComponent, LegendComponent, GridComponent } from 'echarts/components'
import { CanvasRenderer } from 'echarts/renderers'
import type { EChartsOption } from 'echarts'

use([BarChart, LineChart, TooltipComponent, LegendComponent, GridComponent, CanvasRenderer])

// ── State ───────────────────────────────────────────────────────────────────

const loading = ref(false)
const error = ref<string | null>(null)
const byMonth = ref<RevenueReportItem[]>([])
const byProductType = ref<RevenueByProductItem[]>([])

// Date range: default to current year
const now = new Date()
const currentYear = now.getFullYear()
const currentMonth = String(now.getMonth() + 1).padStart(2, '0')
const startMonth = ref(`${currentYear}-01`)
const endMonth = ref(`${currentYear}-${currentMonth}`)

// ── Computed: Turn month labels into date strings for API ──────────────────

function monthStart(m: string): string {
  return `${m}-01`
}

function monthEnd(m: string): string {
  return `${m}-01`
}

// ── Bar chart option ────────────────────────────────────────────────────────

const barChartOption = computed<EChartsOption>(() => {
  const data = byMonth.value
  return {
    tooltip: {
      trigger: 'axis',
    },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: data.map((d) => d.month),
      axisLabel: { fontSize: 11, rotate: data.length > 8 ? 45 : 0 },
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        formatter: (v: number) => (v >= 10000 ? `${(v / 10000).toFixed(0)}万` : String(v)),
      },
    },
    series: [
      {
        name: '营收',
        type: 'bar',
        barWidth: '55%',
        itemStyle: {
          color: {
            type: 'linear',
            x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: '#409eff' },
              { offset: 1, color: '#79bbff' },
            ],
          },
          borderRadius: [4, 4, 0, 0],
        },
        data: data.map((d) => Number(d.amount)),
      },
    ],
  }
})

// ── Summary table columns ──────────────────────────────────────────────────

const summaryColumns = [
  { prop: 'product_type', label: '产品类型', minWidth: 140 },
  { prop: 'amount', label: '营收金额', minWidth: 140, align: 'right' as const },
  { prop: 'percentage', label: '占比', minWidth: 100, align: 'right' as const },
]

const productTypeLabel: Record<string, string> = {
  bare_metal: '物理机',
  cloud: '云服务器',
  bandwidth: '带宽',
  addon: '增值服务',
  ip: 'IP资源',
  rack: '机柜租赁',
}

function formatType(t: string): string {
  return productTypeLabel[t] ?? t
}

// ── Data fetching ──────────────────────────────────────────────────────────

async function fetchData() {
  loading.value = true
  error.value = null
  try {
    const data = await getRevenueReport({
      start_date: monthStart(startMonth.value),
      end_date: monthEnd(endMonth.value),
    })
    byMonth.value = data.by_month
    byProductType.value = data.by_product_type
  } catch (e) {
    error.value = (e as Error).message ?? '加载营收报表失败'
  } finally {
    loading.value = false
  }
}

// ── Export (placeholder) ────────────────────────────────────────────────────

function handleExport(): void {
  // Placeholder — real export would trigger a file download
  ElMessage.success('导出功能即将上线')
}

// ── Init ──────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchData()
})
</script>

<template>
  <div class="report-revenue" v-loading="loading">
    <!-- Page header -->
    <div class="page-header">
      <h2>营收报表</h2>
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

    <!-- Filters -->
    <el-card shadow="never" class="filter-card">
      <el-form :model="{ startMonth, endMonth }" inline>
        <el-form-item label="起始月份">
          <el-date-picker
            v-model="startMonth"
            type="month"
            placeholder="选择起始月份"
            value-format="YYYY-MM"
            style="width: 160px"
          />
        </el-form-item>
        <el-form-item label="结束月份">
          <el-date-picker
            v-model="endMonth"
            type="month"
            placeholder="选择结束月份"
            value-format="YYYY-MM"
            style="width: 160px"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :icon="Search" @click="fetchData">
            查询
          </el-button>
        </el-form-item>
        <el-form-item>
          <el-button :icon="Download" @click="handleExport">
            导出报表
          </el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <!-- Bar chart -->
    <el-card shadow="never" class="chart-card">
      <template #header>
        <span>月度营收</span>
      </template>
      <VChart
        v-if="byMonth.length > 0"
        :option="barChartOption"
        autoresize
        class="chart-container"
      />
      <div v-else class="chart-empty">暂无数据，请调整查询条件</div>
    </el-card>

    <!-- Summary table: breakdown by product type -->
    <el-card shadow="never" class="table-card">
      <template #header>
        <span>产品类型营收汇总</span>
      </template>
      <el-table
        :data="byProductType"
        stripe
        size="small"
        style="width: 100%"
      >
        <el-table-column
          v-for="col in summaryColumns"
          :key="col.prop"
          :prop="col.prop"
          :label="col.label"
          :min-width="col.minWidth"
          :align="col.align ?? 'left'"
        >
          <template #default="{ row }">
            <template v-if="col.prop === 'product_type'">
              {{ formatType(row.product_type) }}
            </template>
            <template v-else-if="col.prop === 'amount'">
              ¥{{ Number(row.amount).toLocaleString(undefined, { minimumFractionDigits: 2 }) }}
            </template>
            <template v-else-if="col.prop === 'percentage'">
              {{ (row.percentage * 100).toFixed(1) }}%
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
    </el-card>
  </div>
</template>

<style scoped>
.report-revenue {
  max-width: 1200px;
}

.page-header {
  margin-bottom: 20px;
}

.page-header h2 {
  font-size: 22px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  margin: 0;
}

.error-alert {
  margin-bottom: 20px;
}

.filter-card {
  margin-bottom: 20px;
}

.chart-card {
  margin-bottom: 20px;
}

.chart-container {
  width: 100%;
  height: 380px;
}

.chart-empty {
  text-align: center;
  padding: 80px 0;
  color: var(--el-text-color-placeholder);
  font-size: 14px;
}

.table-card {
  margin-bottom: 20px;
}
</style>
