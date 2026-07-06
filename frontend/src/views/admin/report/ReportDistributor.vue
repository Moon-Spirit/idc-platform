<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Search, Download } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import { getDistributorSalesReport, type DistributorSalesRow, type DistributorSalesSummary } from '@/api/dashboard'
import { listDistributors, type Distributor } from '@/api/distributor'

// ── State ───────────────────────────────────────────────────────────────────

const loading = ref(false)
const error = ref<string | null>(null)
const salesItems = ref<DistributorSalesRow[]>([])
const summary = ref<DistributorSalesSummary | null>(null)
const total = ref(0)

// Distributor selector
const distributors = ref<Distributor[]>([])
const distributorLoading = ref(false)
const selectedDistributorId = ref<number | null>(null)

// Date range
const now = new Date()
const currentYear = now.getFullYear()
const currentMonth = String(now.getMonth() + 1).padStart(2, '0')
const startDate = ref(`${currentYear}-01-01`)
const endDate = ref(`${currentYear}-${currentMonth}-01`)

// Pagination
const page = ref(1)
const perPage = ref(20)

// ── Status helpers ─────────────────────────────────────────────────────────

function getStatusTagType(status: string): string {
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

function getStatusLabel(status: string): string {
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

// ── Table columns ──────────────────────────────────────────────────────────

const salesColumns = [
  { prop: 'order_no', label: '订单号', minWidth: 180 },
  { prop: 'product_name', label: '产品', minWidth: 160 },
  { prop: 'quantity', label: '数量', width: 80, align: 'center' as const },
  { prop: 'unit_price', label: '单价', width: 120, align: 'right' as const },
  { prop: 'total_amount', label: '总金额', width: 130, align: 'right' as const },
  { prop: 'status', label: '状态', width: 100, align: 'center' as const },
  { prop: 'created_at', label: '创建时间', minWidth: 170 },
]

// ── Data fetching: distributors ────────────────────────────────────────────

async function fetchDistributors() {
  distributorLoading.value = true
  try {
    const res = await listDistributors({ page: 1, per_page: 200 })
    distributors.value = res.items
  } catch {
    // Silently fail — the selector will just be empty
  } finally {
    distributorLoading.value = false
  }
}

// ── Data fetching: sales report ────────────────────────────────────────────

async function fetchSalesData() {
  if (!selectedDistributorId.value) {
    ElMessage.warning('请先选择经销商')
    return
  }
  loading.value = true
  error.value = null
  try {
    const data = await getDistributorSalesReport({
      distributor_id: selectedDistributorId.value,
      start_date: startDate.value,
      end_date: endDate.value,
      page: page.value,
      per_page: perPage.value,
    })
    salesItems.value = data.items
    summary.value = data.summary
    total.value = data.total
  } catch (e) {
    error.value = (e as Error).message ?? '加载经销商销售报表失败'
  } finally {
    loading.value = false
  }
}

// ── Pagination ──────────────────────────────────────────────────────────────

function handlePageChange(newPage: number): void {
  page.value = newPage
  fetchSalesData()
}

function handleSizeChange(newSize: number): void {
  perPage.value = newSize
  page.value = 1
  fetchSalesData()
}

// ── CSV download (placeholder) ──────────────────────────────────────────────

function downloadCsv(): void {
  if (salesItems.value.length === 0) {
    ElMessage.warning('暂无数据可导出')
    return
  }
  // Placeholder — real CSV would be generated from salesItems
  ElMessage.success('CSV导出功能即将上线')
}

// ── Init ──────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchDistributors()
})
</script>

<template>
  <div class="report-distributor" v-loading="loading">
    <!-- Page header -->
    <div class="page-header">
      <h2>经销商销售报表</h2>
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
      <el-form inline>
        <el-form-item label="经销商">
          <el-select
            v-model="selectedDistributorId"
            placeholder="请选择经销商"
            filterable
            remote
            :remote-method="(_q: string) => { /* search would re-fetch distributors */ }"
            :loading="distributorLoading"
            style="width: 240px"
            clearable
          >
            <el-option
              v-for="d in distributors"
              :key="d.id"
              :label="d.name"
              :value="d.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="起始日期">
          <el-date-picker
            v-model="startDate"
            type="date"
            placeholder="选择起始日期"
            value-format="YYYY-MM-DD"
            style="width: 160px"
          />
        </el-form-item>
        <el-form-item label="结束日期">
          <el-date-picker
            v-model="endDate"
            type="date"
            placeholder="选择结束日期"
            value-format="YYYY-MM-DD"
            style="width: 160px"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :icon="Search" @click="fetchSalesData">
            查询
          </el-button>
        </el-form-item>
        <el-form-item>
          <el-button :icon="Download" @click="downloadCsv">
            下载CSV
          </el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <!-- Summary info -->
    <el-row :gutter="20" class="summary-row" v-if="summary">
      <el-col :span="8">
        <el-card shadow="hover" class="summary-card">
          <div class="summary-inner">
            <div class="summary-label">总订单数</div>
            <div class="summary-value">{{ summary.total_orders }}</div>
          </div>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card shadow="hover" class="summary-card">
          <div class="summary-inner">
            <div class="summary-label">总营收</div>
            <div class="summary-value">
              ¥{{ Number(summary.total_revenue).toLocaleString(undefined, { minimumFractionDigits: 2 }) }}
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card shadow="hover" class="summary-card">
          <div class="summary-inner">
            <div class="summary-label">续费率</div>
            <div class="summary-value">{{ (summary.renewal_rate * 100).toFixed(1) }}%</div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Sales data table -->
    <el-card shadow="never" class="table-card">
      <template #header>
        <span>销售明细</span>
      </template>

      <el-table
        :data="salesItems"
        stripe
        size="small"
        style="width: 100%"
        @row-click="(_row: DistributorSalesRow) => {}"
      >
        <el-table-column
          v-for="col in salesColumns"
          :key="col.prop"
          :prop="col.prop"
          :label="col.label"
          :min-width="col.minWidth"
          :width="col.width"
          :align="col.align ?? 'left'"
        >
          <template #default="{ row }">
            <template v-if="col.prop === 'status'">
              <el-tag :type="getStatusTagType(row.status)" size="small" effect="dark">
                {{ getStatusLabel(row.status) }}
              </el-tag>
            </template>
            <template v-else-if="col.prop === 'unit_price' || col.prop === 'total_amount'">
              ¥{{ Number(row[col.prop as keyof DistributorSalesRow] as string).toLocaleString(undefined, { minimumFractionDigits: 2 }) }}
            </template>
            <template v-else-if="col.prop === 'created_at'">
              {{ row.created_at.slice(0, 10) }}
            </template>
            <template v-else>
              {{ row[col.prop as keyof DistributorSalesRow] }}
            </template>
          </template>
        </el-table-column>
        <template #empty>
          <el-empty description="暂无销售数据，请查询" />
        </template>
      </el-table>

      <!-- Pagination -->
      <div class="pagination-wrap" v-if="total > perPage">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="perPage"
          :total="total"
          :page-sizes="[10, 20, 50, 100]"
          layout="total, sizes, prev, pager, next"
          @current-change="handlePageChange"
          @size-change="handleSizeChange"
        />
      </div>
    </el-card>
  </div>
</template>

<style scoped>
.report-distributor {
  max-width: 1400px;
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

/* ── Summary cards ────────────────────────────────────────────────────────── */

.summary-row {
  margin-bottom: 20px;
}

.summary-card {
  margin-bottom: 20px;
}

.summary-inner {
  text-align: center;
  padding: 8px 0;
}

.summary-label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  margin-bottom: 8px;
}

.summary-value {
  font-size: 26px;
  font-weight: 700;
  color: var(--el-text-color-primary);
}

/* ── Table ────────────────────────────────────────────────────────────────── */

.table-card {
  margin-bottom: 20px;
}

.pagination-wrap {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}
</style>
