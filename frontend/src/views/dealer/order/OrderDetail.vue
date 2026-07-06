<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { getOrder, cancelOrder, approveOrder, rejectOrder } from '@/api/order'
import type { OrderDetail as OrderDetailType, OrderItem, TimelineEntry, OrderStatus } from '@/api/order'
import { useAuthStore } from '@/stores/auth'

// ── Status config ──────────────────────────────────────────────────────────

const STATUS_STEPS: { status: OrderStatus; label: string }[] = [
  { status: 'pending', label: '待审核' },
  { status: 'approved', label: '审核通过' },
  { status: 'active', label: '运行中' },
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

// ── Props / Router ─────────────────────────────────────────────────────────

const route = useRoute()
const router = useRouter()
const authStore = useAuthStore()

const orderId = computed(() => Number(route.params.id))
const isAdmin = computed(() => authStore.user?.role === 'admin')

// ── State ──────────────────────────────────────────────────────────────────

const loading = ref(true)
const order = ref<OrderDetailType | null>(null)
const rejectDialogVisible = ref(false)
const rejectReason = ref('')

// ── Computed ───────────────────────────────────────────────────────────────

const currentStepIndex = computed(() => {
  if (!order.value) return 0
  const status = order.value.status
  if (status === 'rejected' || status === 'suspended' || status === 'terminated') {
    // Find the closest matching step
    const idx = STATUS_STEPS.findIndex(s => s.status === status)
    return idx >= 0 ? idx : 0
  }
  const idx = STATUS_STEPS.findIndex(s => s.status === status)
  return idx >= 0 ? idx : 0
})

const canCancel = computed(() => {
  return order.value?.status === 'pending'
})

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

// ── Methods ────────────────────────────────────────────────────────────────

async function fetchOrder() {
  loading.value = true
  try {
    order.value = await getOrder(orderId.value)
  } catch {
    ElMessage.error('订单不存在')
    router.push({ name: 'OrderList' })
  } finally {
    loading.value = false
  }
}

async function handleCancel() {
  if (!order.value) return
  try {
    await ElMessageBox.confirm(
      `确定要取消订单 ${order.value.order_no} 吗？`,
      '确认取消',
      {
        confirmButtonText: '确认取消',
        cancelButtonText: '暂不',
        type: 'warning',
      },
    )
    await cancelOrder(order.value.id)
    ElMessage.success('订单已取消')
    await fetchOrder()
  } catch {
    // cancelled or error
  }
}

async function handleApprove() {
  if (!order.value) return
  try {
    await ElMessageBox.confirm(
      `确定要通过订单 ${order.value.order_no} 吗？`,
      '确认通过',
      {
        confirmButtonText: '确认通过',
        cancelButtonText: '取消',
        type: 'info',
      },
    )
    await approveOrder(order.value.id)
    ElMessage.success('订单已通过')
    await fetchOrder()
  } catch {
    // cancelled or error
  }
}

function openRejectDialog() {
  rejectReason.value = ''
  rejectDialogVisible.value = true
}

async function handleReject() {
  if (!order.value || !rejectReason.value.trim()) {
    ElMessage.warning('请填写驳回原因')
    return
  }
  try {
    await rejectOrder(order.value.id, rejectReason.value.trim())
    ElMessage.success('订单已驳回')
    rejectDialogVisible.value = false
    await fetchOrder()
  } catch {
    // handled by interceptor
  }
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchOrder()
})
</script>

<template>
  <div v-loading="loading" class="order-detail">
    <template v-if="order">
      <!-- Header -->
      <div class="page-header">
        <el-link :underline="false" @click="router.push(isAdmin ? { name: 'AdminOrderReview' } : { name: 'OrderList' })">
          &laquo; 返回
        </el-link>
        <h2 class="page-title">
          订单详情
          <span class="order-no">{{ order.order_no }}</span>
          <el-tag :type="getStatusTagType(order.status)" size="medium" effect="dark" class="status-tag">
            {{ getStatusLabel(order.status) }}
          </el-tag>
        </h2>
      </div>

      <!-- Status stepper -->
      <el-card shadow="never" class="stepper-card">
        <el-steps :active="currentStepIndex" align-center finish-status="success">
          <el-step
            v-for="step in STATUS_STEPS"
            :key="step.status"
            :title="step.label"
          />
        </el-steps>
        <!-- For rejected/suspended/terminated, show a status alert -->
        <el-alert
          v-if="order.status === 'rejected'"
          title="订单已驳回"
          type="error"
          show-icon
          :closable="false"
          class="status-alert"
        />
        <el-alert
          v-else-if="order.status === 'suspended'"
          title="订单已暂停"
          type="warning"
          show-icon
          :closable="false"
          class="status-alert"
        />
        <el-alert
          v-else-if="order.status === 'terminated'"
          title="订单已终止"
          type="info"
          show-icon
          :closable="false"
          class="status-alert"
        />

        <!-- Admin actions -->
        <div v-if="isAdmin && order.status === 'pending'" class="admin-actions">
          <el-button type="success" size="large" @click="handleApprove">审核通过</el-button>
          <el-button type="danger" size="large" @click="openRejectDialog">驳回</el-button>
        </div>
        <!-- Dealer cancel -->
        <div v-else-if="canCancel" class="admin-actions">
          <el-button type="danger" size="large" plain @click="handleCancel">取消订单</el-button>
        </div>
      </el-card>

      <el-row :gutter="16">
        <!-- Order info -->
        <el-col :span="12">
          <el-card shadow="never">
            <template #header>
              <span>订单信息</span>
            </template>
            <el-descriptions :column="1" border>
              <el-descriptions-item label="订单号">
                {{ order.order_no }}
              </el-descriptions-item>
              <el-descriptions-item label="经销商">
                {{ order.distributor_name ?? '-' }}
              </el-descriptions-item>
              <el-descriptions-item label="计费周期">
                {{ order.billing_cycle === 'yearly' ? '年付' : '月付' }}
              </el-descriptions-item>
              <el-descriptions-item label="总金额">
                <span class="price">¥{{ Number(order.total_amount).toFixed(2) }}</span>
              </el-descriptions-item>
              <el-descriptions-item label="实付金额">
                <span class="price">¥{{ Number(order.final_amount).toFixed(2) }}</span>
              </el-descriptions-item>
              <el-descriptions-item label="创建时间">
                {{ formatDate(order.created_at) }}
              </el-descriptions-item>
              <el-descriptions-item label="更新时间">
                {{ formatDate(order.updated_at ?? '') }}
              </el-descriptions-item>
              <el-descriptions-item label="备注">
                {{ order.remark ?? '-' }}
              </el-descriptions-item>
            </el-descriptions>
          </el-card>
        </el-col>

        <!-- Order items -->
        <el-col :span="12">
          <el-card shadow="never">
            <template #header>
              <span>订单项 ({{ order.items.length }})</span>
            </template>
            <el-table :data="order.items" stripe size="small">
              <el-table-column label="产品" min-width="140">
                <template #default="{ row }: { row: OrderItem }">
                  {{ row.product_name }}
                </template>
              </el-table-column>
              <el-table-column label="数量" width="60" align="center">
                <template #default="{ row }: { row: OrderItem }">
                  {{ row.quantity }}
                </template>
              </el-table-column>
              <el-table-column label="单价" width="100" align="right">
                <template #default="{ row }: { row: OrderItem }">
                  ¥{{ Number(row.unit_price).toFixed(2) }}
                </template>
              </el-table-column>
              <el-table-column label="小计" width="110" align="right">
                <template #default="{ row }: { row: OrderItem }">
                  <span class="price">¥{{ Number(row.subtotal).toFixed(2) }}</span>
                </template>
              </el-table-column>
            </el-table>
            <div class="order-total-line">
              <span>合计</span>
              <span class="price-large">¥{{ Number(order.total_amount).toFixed(2) }}</span>
            </div>
          </el-card>
        </el-col>
      </el-row>

      <!-- Status timeline -->
      <el-card shadow="never" class="timeline-card">
        <template #header>
          <span>状态时间线</span>
        </template>
        <el-timeline>
          <el-timeline-item
            v-for="(entry, idx) in order.timeline"
            :key="idx"
            :timestamp="formatDate(entry.time)"
            :type="getStatusTagType(entry.status) as 'primary' | 'success' | 'warning' | 'danger' | 'info'"
          >
            {{ getStatusLabel(entry.status) }}
            <span v-if="entry.operator"> - {{ entry.operator }}</span>
          </el-timeline-item>
          <el-timeline-item
            v-if="order.timeline.length === 0"
            timestamp="-"
            type="info"
          >
            暂无记录
          </el-timeline-item>
        </el-timeline>
      </el-card>
    </template>

    <!-- Reject reason dialog -->
    <el-dialog
      v-model="rejectDialogVisible"
      title="驳回订单"
      width="450px"
    >
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
.order-detail {
  max-width: 1200px;
}

.page-header {
  margin-bottom: 20px;
}

.page-title {
  font-size: 22px;
  font-weight: 600;
  margin-top: 8px;
  color: var(--el-text-color-primary);
  display: flex;
  align-items: center;
  gap: 12px;
}

.order-no {
  font-size: 16px;
  font-weight: 400;
  color: var(--el-text-color-secondary);
}

.status-tag {
  margin-left: 4px;
}

.stepper-card {
  margin-bottom: 16px;
}

.status-alert {
  margin-top: 20px;
}

.admin-actions {
  margin-top: 20px;
  display: flex;
  gap: 12px;
  justify-content: center;
}

.price {
  font-weight: 600;
  color: #e6a23c;
}

.price-large {
  font-size: 18px;
  font-weight: 700;
  color: #e6a23c;
}

.order-total-line {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 16px;
  font-size: 15px;
  font-weight: 600;
  border-top: 1px solid var(--el-border-color-light);
  margin-top: 8px;
}

.timeline-card {
  margin-top: 16px;
  margin-bottom: 16px;
}
</style>
