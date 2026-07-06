<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import VChart from 'vue-echarts'
import { use } from 'echarts/core'
import { CanvasRenderer } from 'echarts/renderers'
import { LineChart } from 'echarts/charts'
import {
  GridComponent,
  TooltipComponent,
  LegendComponent,
  DataZoomComponent,
} from 'echarts/components'
import type { EChartsOption } from 'echarts'
import {
  listSubscriptions,
  getBandwidthData,
  type Subscription,
  type BandwidthDataPoint,
} from '@/api/subscription'

// Register ECharts components
use([
  CanvasRenderer,
  LineChart,
  GridComponent,
  TooltipComponent,
  LegendComponent,
  DataZoomComponent,
])

// ── Types ──────────────────────────────────────────────────────────────────

interface DayData {
  date: string
  avgInRate: number
  avgOutRate: number
  maxInRate: number
  maxOutRate: number
}

// ── State ──────────────────────────────────────────────────────────────────

const loading = ref(false)
const subscriptions = ref<Subscription[]>([])
const selectedSubId = ref<number | null>(null)
const bandwidthData = ref<BandwidthDataPoint[]>([])
const monthly95th = ref(0)
const bandwidthLimit = ref(0)
const selectedDays = ref(30)

// ── Computed ───────────────────────────────────────────────────────────────

const dailyData = computed<DayData[]>(() => {
  if (!bandwidthData.value.length) return []

  const map = new Map<string, number[]>()
  const mapOut = new Map<string, number[]>()

  for (const dp of bandwidthData.value) {
    const day = dp.time.substring(0, 10)
    if (!map.has(day)) map.set(day, [])
    if (!mapOut.has(day)) mapOut.set(day, [])
    map.get(day)!.push(dp.in_rate_mbps)
    mapOut.get(day)!.push(dp.out_rate_mbps)
  }

  const result: DayData[] = []
  for (const [date, rates] of map) {
    const outRates = mapOut.get(date) ?? []
    const avgIn = rates.reduce((a, b) => a + b, 0) / rates.length
    const avgOut = outRates.reduce((a, b) => a + b, 0) / outRates.length
    result.push({
      date,
      avgInRate: Math.round(avgIn * 100) / 100,
      avgOutRate: Math.round(avgOut * 100) / 100,
      maxInRate: Math.round(Math.max(...rates) * 100) / 100,
      maxOutRate: Math.round(Math.max(...outRates) * 100) / 100,
    })
  }

  result.sort((a, b) => a.date.localeCompare(b.date))
  return result
})

const chartOptions = computed<EChartsOption>(() => {
  const dates = dailyData.value.map((d) => d.date)
  return {
    tooltip: {
      trigger: 'axis',
      formatter: (params: unknown) => {
        const items = Array.isArray(params) ? params : [params]
        return items
          .map(
            (p: Record<string, unknown>) =>
              `${p.seriesName}: ${Number(p.value as number).toFixed(2)} Mbps`,
          )
          .join('<br/>')
      },
    },
    legend: {
      data: ['入带宽 (平均)', '出带宽 (平均)', '入带宽 (峰值)', '出带宽 (峰值)'],
      top: 0,
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '10%',
      containLabel: true,
    },
    xAxis: {
      type: 'category',
      data: dates,
      axisLabel: {
        rotate: 45,
        fontSize: 11,
      },
    },
    yAxis: {
      type: 'value',
      name: 'Mbps',
      min: 0,
    },
    dataZoom: [
      {
        type: 'inside',
        start: 0,
        end: 100,
      },
      {
        type: 'slider',
        start: 0,
        end: 100,
        bottom: 0,
      },
    ],
    series: [
      {
        name: '入带宽 (平均)',
        type: 'line',
        smooth: true,
        data: dailyData.value.map((d) => d.avgInRate),
        itemStyle: { color: '#409eff' },
        areaStyle: {
          color: {
            type: 'linear',
            x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(64, 158, 255, 0.3)' },
              { offset: 1, color: 'rgba(64, 158, 255, 0.05)' },
            ],
          },
        },
      },
      {
        name: '出带宽 (平均)',
        type: 'line',
        smooth: true,
        data: dailyData.value.map((d) => d.avgOutRate),
        itemStyle: { color: '#67c23a' },
      },
      {
        name: '入带宽 (峰值)',
        type: 'line',
        smooth: true,
        lineStyle: { type: 'dashed' },
        data: dailyData.value.map((d) => d.maxInRate),
        itemStyle: { color: '#e6a23c' },
        symbol: 'none',
      },
      {
        name: '出带宽 (峰值)',
        type: 'line',
        smooth: true,
        lineStyle: { type: 'dashed' },
        data: dailyData.value.map((d) => d.maxOutRate),
        itemStyle: { color: '#f56c6c' },
        symbol: 'none',
      },
    ],
  }
})

// ── Methods ────────────────────────────────────────────────────────────────

async function fetchSubscriptions() {
  try {
    const res = await listSubscriptions({ page: 1, per_page: 100 })
    subscriptions.value = res.items

    // Auto-select first bandwidth product or first active sub
    const bwSub = res.items.find(
      (sub) => sub.product_type === 'bandwidth' || sub.product_type === undefined,
    )
    if (bwSub) {
      selectedSubId.value = bwSub.id
    } else if (res.items.length > 0) {
      selectedSubId.value = res.items[0].id
    }
  } catch {
    // handled by interceptor
  }
}

async function fetchBandwidthData() {
  if (!selectedSubId.value) return

  loading.value = true
  try {
    // Calculate date range
    const endDate = new Date()
    const startDate = new Date()
    startDate.setDate(startDate.getDate() - selectedDays.value)

    const params: Record<string, string> = {
      start_date: startDate.toISOString().substring(0, 10),
      end_date: endDate.toISOString().substring(0, 10),
      granularity: 'hour',
    }

    const data = await getBandwidthData(selectedSubId.value, params)
    bandwidthData.value = data.data_points
    monthly95th.value = data.monthly_95th
    bandwidthLimit.value = data.bandwidth_limit
  } catch {
    ElMessage.error('获取带宽数据失败')
  } finally {
    loading.value = false
  }
}

function handleSubChange() {
  fetchBandwidthData()
}

function handleDaysChange(days: number) {
  selectedDays.value = days
  fetchBandwidthData()
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(async () => {
  await fetchSubscriptions()
  if (selectedSubId.value) {
    await fetchBandwidthData()
  }
})
</script>

<template>
  <div class="bandwidth-view">
    <h2 class="page-title">带宽监控</h2>

    <!-- Controls -->
    <el-card shadow="never" class="controls-card">
      <el-row :gutter="16" align="middle">
        <el-col :span="8">
          <el-form-item label="选择产品">
            <el-select
              v-model="selectedSubId"
              placeholder="选择订阅产品"
              style="width: 100%"
              @change="handleSubChange"
            >
              <el-option
                v-for="sub in subscriptions"
                :key="sub.id"
                :label="sub.product_name"
                :value="sub.id"
              />
            </el-select>
          </el-form-item>
        </el-col>
        <el-col :span="8">
          <el-form-item label="时间范围">
            <el-radio-group
              :model-value="selectedDays"
              @change="handleDaysChange"
            >
              <el-radio-button :value="7">7天</el-radio-button>
              <el-radio-button :value="30">30天</el-radio-button>
              <el-radio-button :value="90">90天</el-radio-button>
            </el-radio-group>
          </el-form-item>
        </el-col>
        <el-col :span="8">
          <div class="stats-summary">
            <el-tag type="warning" size="large" effect="plain">
              95分位值: {{ monthly95th.toFixed(2) }} Mbps
            </el-tag>
            <el-tag v-if="bandwidthLimit > 0" type="info" size="large" effect="plain" style="margin-left: 8px;">
              带宽上限: {{ bandwidthLimit }} Mbps
            </el-tag>
          </div>
        </el-col>
      </el-row>
    </el-card>

    <!-- Bandwidth chart -->
    <el-card shadow="never" class="chart-card">
      <template #header>
        <span>带宽使用趋势</span>
      </template>
      <div v-loading="loading" class="chart-container">
        <VChart
          v-if="dailyData.length > 0"
          :option="chartOptions"
          autoresize
          style="width: 100%; height: 400px"
        />
        <el-empty v-else description="暂无带宽数据" />
      </div>
    </el-card>

    <!-- Stats cards -->
    <el-row :gutter="16" class="stats-row">
      <el-col :span="6">
        <el-card shadow="never">
          <div class="stat-card">
            <span class="stat-label">月95分位值</span>
            <span class="stat-value warning">{{ monthly95th.toFixed(2) }} Mbps</span>
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="never">
          <div class="stat-card">
            <span class="stat-label">日均入带宽</span>
            <span class="stat-value primary">
              {{ dailyData.length > 0 ? dailyData.reduce((s, d) => s + d.avgInRate, 0).toFixed(2) : '0.00' }} Mbps
            </span>
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="never">
          <div class="stat-card">
            <span class="stat-label">日均出带宽</span>
            <span class="stat-value success">
              {{ dailyData.length > 0 ? dailyData.reduce((s, d) => s + d.avgOutRate, 0).toFixed(2) : '0.00' }} Mbps
            </span>
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="never">
          <div class="stat-card">
            <span class="stat-label">带宽上限</span>
            <span class="stat-value">{{ bandwidthLimit > 0 ? `${bandwidthLimit} Mbps` : '-' }}</span>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Daily data table -->
    <el-card shadow="never" class="table-card">
      <template #header>
        <span>日度明细</span>
      </template>
      <el-table :data="dailyData" stripe size="small" v-loading="loading" max-height="400">
        <el-table-column prop="date" label="日期" width="120" />
        <el-table-column label="平均入带宽 (Mbps)" width="160" align="right">
          <template #default="{ row }: { row: DayData }">
            {{ row.avgInRate.toFixed(2) }}
          </template>
        </el-table-column>
        <el-table-column label="平均出带宽 (Mbps)" width="160" align="right">
          <template #default="{ row }: { row: DayData }">
            {{ row.avgOutRate.toFixed(2) }}
          </template>
        </el-table-column>
        <el-table-column label="峰值入带宽 (Mbps)" width="160" align="right">
          <template #default="{ row }: { row: DayData }">
            {{ row.maxInRate.toFixed(2) }}
          </template>
        </el-table-column>
        <el-table-column label="峰值出带宽 (Mbps)" width="160" align="right">
          <template #default="{ row }: { row: DayData }">
            {{ row.maxOutRate.toFixed(2) }}
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<style scoped>
.bandwidth-view {
  max-width: 1200px;
}

.page-title {
  font-size: 22px;
  font-weight: 600;
  margin-bottom: 20px;
  color: var(--el-text-color-primary);
}

.controls-card {
  margin-bottom: 16px;
}

.controls-card .el-form-item {
  margin-bottom: 0;
}

.stats-summary {
  display: flex;
  justify-content: flex-end;
  align-items: center;
}

.chart-card {
  margin-bottom: 16px;
}

.chart-container {
  min-height: 400px;
}

.stats-row {
  margin-bottom: 16px;
}

.stat-card {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.stat-label {
  font-size: 14px;
  color: var(--el-text-color-secondary);
}

.stat-value {
  font-size: 20px;
  font-weight: 700;
}

.stat-value.warning {
  color: #e6a23c;
}

.stat-value.primary {
  color: #409eff;
}

.stat-value.success {
  color: #67c23a;
}

.table-card {
  margin-bottom: 16px;
}
</style>
