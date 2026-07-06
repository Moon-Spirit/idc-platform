<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { Search, Refresh } from '@element-plus/icons-vue'
import { listInvoices, type Invoice } from '@/api/invoice'

// ── Status config ──────────────────────────────────────────────────────────

function getStatusTagType(status: string): string {
  const map: Record<string, string> = {
    unpaid: 'warning',
    paid: 'success',
    overdue: 'danger',
    void: 'info',
  }
  return map[status] ?? 'info'
}

function getStatusLabel(status: string): string {
  const map: Record<string, string> = {
    unpaid: '未支付',
    paid: '已支付',
    overdue: '已逾期',
    void: '已作废',
  }
  return map[status] ?? status
}

// ── State ──────────────────────────────────────────────────────────────────

const router = useRouter()
const loading = ref(false)
const invoices = ref<Invoice[]>([])
const total = ref(0)

const query = reactive({
  page: 1,
  per_page: 20,
  status: '',
  keyword: '',
  year: undefined as number | undefined,
  month: undefined as number | undefined,
})

const stats = reactive({
  totalOutstanding: 0,
  overdueCount: 0,
  overdueAmount: 0,
})

// ── Computed ───────────────────────────────────────────────────────────────

const statusFilterOptions = [
  { label: '全部', value: '' },
  { label: '未支付', value: 'unpaid' },
  { label: '已支付', value: 'paid' },
  { label: '已逾期', value: 'overdue' },
  { label: '已作废', value: 'void' },
]

// ── Methods ────────────────────────────────────────────────────────────────

async function fetchInvoices() {
  loading.value = true
  try {
    const params: Record<string, unknown> = {
      page: query.page,
      per_page: query.per_page,
    }
    if (query.status) {
      params.status = query.status
    }
    if (query.year) {
      params.year = query.year
    }
    if (query.month) {
      params.month = query.month
    }

    const res = await listInvoices(params)
    invoices.value = res.items
    total.value = res.total

    // Calculate stats from current page data (a real app would have a dedicated stats endpoint)
    stats.totalOutstanding = res.items
      .filter((inv) => inv.status === 'unpaid' || inv.status === 'overdue')
      .reduce((sum, inv) => sum + Number(inv.total_amount) - Number(inv.paid_amount), 0)

    stats.overdueCount = res.items.filter((inv) => inv.status === 'overdue').length
    stats.overdueAmount = res.items
      .filter((inv) => inv.status === 'overdue')
      .reduce((sum, inv) => sum + Number(inv.total_amount) - Number(inv.paid_amount), 0)
  } catch {
    // handled by interceptor
  } finally {
    loading.value = false
  }
}

function handlePageChange(page: number) {
  query.page = page
  fetchInvoices()
}

function handleSizeChange(size: number) {
  query.per_page = size
  query.page = 1
  fetchInvoices()
}

function handleSearch() {
  query.page = 1
  fetchInvoices()
}

function handleReset() {
  query.status = ''
  query.keyword = ''
  query.year = undefined
  query.month = undefined
  query.page = 1
  fetchInvoices()
}

function goToDetail(id: number) {
  router.push({ name: 'AdminInvoiceDetail', params: { id } })
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchInvoices()
})
</script>

<template>
  <div class="invoice-admin">
    <h2 class="page-title">账单管理</h2>

    <!-- Quick stats -->
    <el-row :gutter="16" class="stats-row">
      <el-col :span="8">
        <el-card shadow="never">
          <div class="stat-item">
            <span class="stat-label">当前页应收总额</span>
            <span class="stat-value warning">¥{{ stats.totalOutstanding.toFixed(2) }}</span>
          </div>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card shadow="never">
          <div class="stat-item">
            <span class="stat-label">逾期数量</span>
            <span class="stat-value danger">{{ stats.overdueCount }}</span>
          </div>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card shadow="never">
          <div class="stat-item">
            <span class="stat-label">逾期金额</span>
            <span class="stat-value danger">¥{{ stats.overdueAmount.toFixed(2) }}</span>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Search & filter -->
    <el-card shadow="never" class="search-card">
      <el-form :model="query" inline>
        <el-form-item label="状态">
          <el-select
            v-model="query.status"
            placeholder="选择状态"
            clearable
            style="width: 120px"
            @change="handleSearch"
          >
            <el-option
              v-for="opt in statusFilterOptions"
              :key="opt.value"
              :label="opt.label"
              :value="opt.value"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="年份">
          <el-select
            v-model="query.year"
            placeholder="年份"
            clearable
            style="width: 110px"
            @change="handleSearch"
          >
            <el-option v-for="y in [2026, 2025, 2027]" :key="y" :label="String(y)" :value="y" />
          </el-select>
        </el-form-item>
        <el-form-item label="月份">
          <el-select
            v-model="query.month"
            placeholder="月份"
            clearable
            style="width: 110px"
            @change="handleSearch"
          >
            <el-option v-for="m in 12" :key="m" :label="String(m).padStart(2, '0')" :value="m" />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :icon="Search" @click="handleSearch">搜索</el-button>
          <el-button :icon="Refresh" @click="handleReset">重置</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <!-- Invoice table -->
    <el-card shadow="never" class="table-card">
      <el-table
        :data="invoices"
        v-loading="loading"
        stripe
        style="width: 100%"
        @row-click="(row: Invoice) => goToDetail(row.id)"
      >
        <el-table-column prop="invoice_no" label="账单号" min-width="190">
          <template #default="{ row }: { row: Invoice }">
            <el-link type="primary" :underline="false">
              {{ row.invoice_no }}
            </el-link>
          </template>
        </el-table-column>
        <el-table-column prop="distributor_name" label="经销商" min-width="140" />
        <el-table-column prop="period" label="账期" min-width="170" />
        <el-table-column label="状态" width="100">
          <template #default="{ row }: { row: Invoice }">
            <el-tag :type="getStatusTagType(row.status)" size="small" effect="dark">
              {{ getStatusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="金额" width="130" align="right">
          <template #default="{ row }: { row: Invoice }">
            <span class="amount">¥{{ Number(row.total_amount).toFixed(2) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="已付" width="110" align="right">
          <template #default="{ row }: { row: Invoice }">
            ¥{{ Number(row.paid_amount).toFixed(2) }}
          </template>
        </el-table-column>
        <el-table-column prop="item_count" label="项目" width="60" align="center" />
        <el-table-column prop="due_date" label="到期日" width="110" />
      </el-table>

      <!-- Pagination -->
      <div class="pagination-wrapper">
        <el-pagination
          v-model:current-page="query.page"
          v-model:page-size="query.per_page"
          :page-sizes="[10, 20, 50, 100]"
          :total="total"
          layout="total, sizes, prev, pager, next, jumper"
          @size-change="handleSizeChange"
          @current-change="handlePageChange"
        />
      </div>
    </el-card>
  </div>
</template>

<style scoped>
.invoice-admin {
  max-width: 1200px;
}

.page-title {
  font-size: 22px;
  font-weight: 600;
  margin-bottom: 20px;
  color: var(--el-text-color-primary);
}

.stats-row {
  margin-bottom: 16px;
}

.stat-item {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.stat-label {
  font-size: 14px;
  color: var(--el-text-color-secondary);
}

.stat-value {
  font-size: 24px;
  font-weight: 700;
}

.stat-value.warning {
  color: #e6a23c;
}

.stat-value.danger {
  color: #f56c6c;
}

.search-card,
.table-card {
  margin-bottom: 16px;
}

.amount {
  font-weight: 600;
  color: #e6a23c;
}

.pagination-wrapper {
  margin-top: 20px;
  display: flex;
  justify-content: flex-end;
}
</style>
