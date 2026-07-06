<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { Search, Refresh } from '@element-plus/icons-vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { listOrders, approveOrder, rejectOrder, type Order, type OrderStatus } from '@/api/order'

// ── Status config ──────────────────────────────────────────────────────────

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

// ── State ──────────────────────────────────────────────────────────────────

const router = useRouter()
const route = useRoute()

const loading = ref(false)
const orders = ref<Order[]>([])
const total = ref(0)

const activeTab = ref(route.path.includes('pending') ? 'pending' : 'all')

const query = reactive({
  page: 1,
  per_page: 20,
  keyword: '',
})

const rejectDialogVisible = ref(false)
const rejectingOrder = ref<Order | null>(null)
const rejectReason = ref('')

const pendingCount = ref(0)

// ── Methods ────────────────────────────────────────────────────────────────

async function fetchOrders() {
  loading.value = true
  try {
    const params: Record<string, unknown> = {
      page: query.page,
      per_page: query.per_page,
    }
    if (activeTab.value === 'pending') {
      params.status = 'pending'
    }
    if (query.keyword.trim()) {
      params.keyword = query.keyword.trim()
    }

    const res = await listOrders(params)
    orders.value = res.items
    total.value = res.total

    // Also fetch pending count for badge
    const pendingRes = await listOrders({ page: 1, per_page: 1, status: 'pending' })
    pendingCount.value = pendingRes.total
  } catch {
    // handled by interceptor
  } finally {
    loading.value = false
  }
}

function handleTabChange() {
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
  query.page = 1
  fetchOrders()
}

function goToDetail(id: number) {
  router.push({ name: 'AdminOrderDetail', params: { id } })
}

async function handleApprove(order: Order) {
  try {
    await ElMessageBox.confirm(
      `确定要审核通过订单 ${order.order_no} 吗？`,
      '审核通过',
      {
        confirmButtonText: '确认通过',
        cancelButtonText: '取消',
        type: 'info',
      },
    )
    await approveOrder(order.id)
    ElMessage.success('已通过')
    await fetchOrders()
  } catch {
    // cancelled or error
  }
}

function openRejectDialog(order: Order) {
  rejectingOrder.value = order
  rejectReason.value = ''
  rejectDialogVisible.value = true
}

async function handleReject() {
  if (!rejectingOrder.value || !rejectReason.value.trim()) {
    ElMessage.warning('请填写驳回原因')
    return
  }
  try {
    await rejectOrder(rejectingOrder.value.id, rejectReason.value.trim())
    ElMessage.success('已驳回')
    rejectDialogVisible.value = false
    rejectingOrder.value = null
    await fetchOrders()
  } catch {
    // handled by interceptor
  }
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
  <div class="order-review">
    <h2 class="page-title">订单管理</h2>

    <!-- Tabs: All orders / Pending -->
    <el-card shadow="never" class="tabs-card">
      <el-tabs v-model="activeTab" @tab-change="handleTabChange">
        <el-tab-pane label="全部订单" name="all" />
        <el-tab-pane label="待审核" name="pending">
          <template #label>
            <span>
              待审核
              <el-tag v-if="pendingCount > 0" type="danger" size="small" class="pending-badge">
                {{ pendingCount }}
              </el-tag>
            </span>
          </template>
        </el-tab-pane>
      </el-tabs>
    </el-card>

    <!-- Search -->
    <el-card shadow="never" class="search-card">
      <el-form :model="query" inline>
        <el-form-item label="关键词">
          <el-input
            v-model="query.keyword"
            placeholder="订单号/经销商"
            clearable
            style="width: 220px"
            @keyup.enter="handleSearch"
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
            <el-tag
              :type="getStatusTagType(row.status)"
              size="small"
              :effect="row.status === 'pending' ? 'dark' : 'plain'"
            >
              {{ getStatusLabel(row.status) }}
            </el-tag>
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
        <el-table-column label="操作" width="180" fixed="right">
          <template #default="{ row }: { row: Order }">
            <el-button
              v-if="row.status === 'pending'"
              type="success"
              size="small"
              @click.stop="handleApprove(row)"
            >
              通过
            </el-button>
            <el-button
              v-if="row.status === 'pending'"
              type="danger"
              size="small"
              @click.stop="openRejectDialog(row)"
            >
              驳回
            </el-button>
            <el-button
              v-else
              text
              type="primary"
              size="small"
              @click.stop="goToDetail(row.id)"
            >
              查看
            </el-button>
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

    <!-- Reject dialog -->
    <el-dialog
      v-model="rejectDialogVisible"
      title="驳回订单"
      width="450px"
    >
      <p v-if="rejectingOrder" class="reject-order-info">
        订单号: {{ rejectingOrder.order_no }}
      </p>
      <el-form>
        <el-form-item label="驳回原因" required>
          <el-input
            v-model="rejectReason"
            type="textarea"
            :rows="4"
            placeholder="请填写驳回原因"
            maxlength="500"
            show-word-limit
          />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="rejectDialogVisible = false">取消</el-button>
        <el-button type="danger" :disabled="!rejectReason.trim()" @click="handleReject">
          确认驳回
        </el-button>
      </template>
    </el-dialog>
  </div>
</template>

<style scoped>
.order-review {
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

.pending-badge {
  margin-left: 4px;
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

.reject-order-info {
  font-size: 14px;
  color: var(--el-text-color-secondary);
  margin-bottom: 16px;
}
</style>
