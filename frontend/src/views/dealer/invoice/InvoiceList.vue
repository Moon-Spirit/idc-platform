<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { Search, Refresh } from '@element-plus/icons-vue'
import { listInvoices, type Invoice } from '@/api/invoice'

// ── Status config ──────────────────────────────────────────────────────────

interface StatusTab {
  key: string
  label: string
}

const STATUS_TABS: StatusTab[] = [
  { key: '', label: '全部' },
  { key: 'unpaid', label: '未支付' },
  { key: 'paid', label: '已支付' },
  { key: 'overdue', label: '已逾期' },
  { key: 'void', label: '已作废' },
]

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
const activeStatus = ref('')

const query = reactive({
  page: 1,
  per_page: 20,
  year: undefined as number | undefined,
  month: undefined as number | undefined,
})

// ── Methods ────────────────────────────────────────────────────────────────

async function fetchInvoices() {
  loading.value = true
  try {
    const params: Record<string, unknown> = {
      page: query.page,
      per_page: query.per_page,
    }
    if (activeStatus.value) {
      params.status = activeStatus.value
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
  } catch {
    // handled by interceptor
  } finally {
    loading.value = false
  }
}

function handleStatusChange() {
  query.page = 1
  fetchInvoices()
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
  query.year = undefined
  query.month = undefined
  activeStatus.value = ''
  query.page = 1
  fetchInvoices()
}

function goToDetail(id: number) {
  router.push({ name: 'DealerInvoiceDetail', params: { id } })
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchInvoices()
})
</script>

<template>
  <div class="invoice-list">
    <h2 class="page-title">我的账单</h2>

    <!-- Status filter tabs -->
    <el-card shadow="never" class="tabs-card">
      <el-tabs v-model="activeStatus" @tab-change="handleStatusChange">
        <el-tab-pane
          v-for="tab in STATUS_TABS"
          :key="tab.key"
          :label="tab.label"
          :name="tab.key"
        />
      </el-tabs>
    </el-card>

    <!-- Period filter -->
    <el-card shadow="never" class="search-card">
      <el-form :model="query" inline>
        <el-form-item label="年份">
          <el-select
            v-model="query.year"
            placeholder="选择年份"
            clearable
            style="width: 120px"
            @change="handleSearch"
          >
            <el-option
              v-for="y in [2026, 2025, 2027]"
              :key="y"
              :label="String(y)"
              :value="y"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="月份">
          <el-select
            v-model="query.month"
            placeholder="选择月份"
            clearable
            style="width: 120px"
            @change="handleSearch"
          >
            <el-option
              v-for="m in 12"
              :key="m"
              :label="String(m).padStart(2, '0')"
              :value="m"
            />
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
        <el-table-column prop="period" label="账期" min-width="180" />
        <el-table-column label="状态" width="100">
          <template #default="{ row }: { row: Invoice }">
            <el-tag :type="getStatusTagType(row.status)" size="small" effect="dark">
              {{ getStatusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="金额" width="140" align="right">
          <template #default="{ row }: { row: Invoice }">
            <span class="amount">¥{{ Number(row.total_amount).toFixed(2) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="已付" width="140" align="right">
          <template #default="{ row }: { row: Invoice }">
            <span>¥{{ Number(row.paid_amount).toFixed(2) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="item_count" label="项目数" width="80" align="center" />
        <el-table-column prop="due_date" label="到期日" width="120" />
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
.invoice-list {
  max-width: 1200px;
}

.page-title {
  font-size: 22px;
  font-weight: 600;
  margin-bottom: 20px;
  color: var(--el-text-color-primary);
}

.tabs-card,
.search-card {
  margin-bottom: 16px;
}

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
