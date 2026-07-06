<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { Search, Refresh } from '@element-plus/icons-vue'
import { listOrders, type Order, type OrderStatus } from '@/api/order'

// ── Status config ──────────────────────────────────────────────────────────

interface StatusTab {
  key: string
  label: string
}

const STATUS_TABS: StatusTab[] = [
  { key: '', label: '全部' },
  { key: 'pending', label: '待审核' },
  { key: 'approved', label: '已通过' },
  { key: 'rejected', label: '已驳回' },
  { key: 'active', label: '运行中' },
  { key: 'suspended', label: '已暂停' },
]

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

function getCycleLabel(cycle: string): string {
  const map: Record<string, string> = {
    monthly: '月付',
    yearly: '年付',
    quarterly: '季度付',
  }
  return map[cycle] ?? cycle
}

// ── State ──────────────────────────────────────────────────────────────────

const router = useRouter()
const loading = ref(false)
const orders = ref<Order[]>([])
const total = ref(0)
const activeStatus = ref('')

const query = reactive({
  page: 1,
  per_page: 20,
  keyword: '',
  start_date: '',
  end_date: '',
})

// ── Methods ────────────────────────────────────────────────────────────────

async function fetchOrders() {
  loading.value = true
  try {
    const params: Record<string, unknown> = {
      page: query.page,
      per_page: query.per_page,
    }
    if (activeStatus.value) {
      params.status = activeStatus.value
    }
    if (query.keyword.trim()) {
      params.keyword = query.keyword.trim()
    }
    if (query.start_date) {
      params.start_date = query.start_date
    }
    if (query.end_date) {
      params.end_date = query.end_date
    }

    const res = await listOrders(params)
    orders.value = res.items
    total.value = res.total
  } catch {
    // handled by interceptor
  } finally {
    loading.value = false
  }
}

function handleStatusChange() {
  query.page = 1
  fetchOrders()
}

function handlePageChange(page: number) {
  query.page = page
  fetchOrders()
}

function handleSizeChange(size: number) {
  query.per_page = size
  query.page = 1
  fetchOrders()
}

function handleSearch() {
  query.page = 1
  fetchOrders()
}

function handleReset() {
  query.keyword = ''
  query.start_date = ''
  query.end_date = ''
  activeStatus.value = ''
  query.page = 1
  fetchOrders()
}

function goToDetail(id: number) {
  router.push({ name: 'OrderDetail', params: { id } })
}

function formatDate(dateStr: string): string {
  if (!dateStr) return '-'
  const d = new Date(dateStr)
  const year = d.getFullYear()
  const month = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  const hours = String(d.getHours()).padStart(2, '0')
  const minutes = String(d.getMinutes()).padStart(2, '0')
  return `${year}-${month}-${day} ${hours}:${minutes}`
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchOrders()
})
</script>

<template>
  <div class="order-list">
    <h2 class="page-title">我的订单</h2>

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

    <!-- Search & filter -->
    <el-card shadow="never" class="search-card">
      <el-form :model="query" inline>
        <el-form-item label="关键词">
          <el-input
            v-model="query.keyword"
            placeholder="订单号/产品名"
            clearable
            style="width: 200px"
            @keyup.enter="handleSearch"
          />
        </el-form-item>
        <el-form-item label="开始日期">
          <el-date-picker
            v-model="query.start_date"
            type="date"
            placeholder="选择日期"
            value-format="YYYY-MM-DD"
            style="width: 150px"
          />
        </el-form-item>
        <el-form-item label="结束日期">
          <el-date-picker
            v-model="query.end_date"
            type="date"
            placeholder="选择日期"
            value-format="YYYY-MM-DD"
            style="width: 150px"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :icon="Search" @click="handleSearch">搜索</el-button>
          <el-button :icon="Refresh" @click="handleReset">重置</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <!-- Order table -->
    <el-card shadow="never" class="table-card">
      <el-table
        :data="orders"
        v-loading="loading"
        stripe
        style="width: 100%"
        @row-click="(row: Order) => goToDetail(row.id)"
      >
        <el-table-column prop="order_no" label="订单号" min-width="180">
          <template #default="{ row }: { row: Order }">
            <el-link type="primary" :underline="false">
              {{ row.order_no }}
            </el-link>
          </template>
        </el-table-column>
        <el-table-column prop="distributor_name" label="经销商" min-width="140" />
        <el-table-column label="状态" width="100">
          <template #default="{ row }: { row: Order }">
            <el-tag :type="getStatusTagType(row.status)" size="small" effect="dark">
              {{ getStatusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="周期" width="80">
          <template #default="{ row }: { row: Order }">
            {{ getCycleLabel(row.billing_cycle) }}
          </template>
        </el-table-column>
        <el-table-column prop="item_count" label="商品数" width="80" align="center" />
        <el-table-column label="总金额" width="130" align="right">
          <template #default="{ row }: { row: Order }">
            <span class="amount">¥{{ Number(row.total_amount).toFixed(2) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="创建时间" width="160">
          <template #default="{ row }: { row: Order }">
            {{ formatDate(row.created_at) }}
          </template>
        </el-table-column>
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
.order-list {
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
