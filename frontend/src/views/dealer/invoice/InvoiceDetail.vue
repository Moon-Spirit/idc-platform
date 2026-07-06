<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import QRCode from 'qrcode'
import {
  getInvoice,
  payByBalance,
  payOnline,
  getInvoicePdfUrl,
  type InvoiceDetail as InvoiceDetailType,
  type InvoiceItem,
} from '@/api/invoice'

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

function getItemTypeLabel(type: string): string {
  const map: Record<string, string> = {
    subscription: '订阅服务',
    bandwidth_95: '带宽95计费',
    addon: '增值服务',
    one_time: '一次性费用',
  }
  return map[type] ?? type
}

// ── Props / Router ─────────────────────────────────────────────────────────

const route = useRoute()
const router = useRouter()

const invoiceId = computed(() => Number(route.params.id))

// ── State ──────────────────────────────────────────────────────────────────

const loading = ref(true)
const invoice = ref<InvoiceDetailType | null>(null)

const qrDialogVisible = ref(false)
const qrCodeDataUrl = ref('')
const currentPaymentMethod = ref<'alipay' | 'wechat'>('alipay')
const currentTradeNo = ref('')
let pollingTimer: ReturnType<typeof setInterval> | null = null

// ── Computed ───────────────────────────────────────────────────────────────

const isPayable = computed(() => {
  return invoice.value?.status === 'unpaid' || invoice.value?.status === 'overdue'
})

const paidInFull = computed(() => {
  if (!invoice.value) return false
  return Number(invoice.value.paid_amount) >= Number(invoice.value.total_amount)
})

// ── Methods ────────────────────────────────────────────────────────────────

async function fetchInvoice() {
  loading.value = true
  try {
    invoice.value = await getInvoice(invoiceId.value)
  } catch {
    ElMessage.error('账单不存在')
    router.push({ name: 'DealerInvoiceList' })
  } finally {
    loading.value = false
  }
}

function downloadPdf() {
  if (!invoice.value) return
  window.open(getInvoicePdfUrl(invoice.value.id), '_blank')
}

async function handlePayByBalance() {
  if (!invoice.value) return
  try {
    await ElMessageBox.confirm(
      `确认使用余额支付账单 ${invoice.value.invoice_no} 吗？\n金额: ¥${Number(invoice.value.total_amount).toFixed(2)}`,
      '余额支付',
      {
        confirmButtonText: '确认支付',
        cancelButtonText: '取消',
        type: 'info',
      },
    )
    const result = await payByBalance(invoice.value.id)
    ElMessage.success(`支付成功！已支付 ¥${Number(result.paid_amount).toFixed(2)}`)
    await fetchInvoice()
  } catch {
    // cancelled or error
  }
}

async function handlePayOnline(method: 'alipay' | 'wechat') {
  if (!invoice.value) return
  try {
    currentPaymentMethod.value = method
    const result = await payOnline(invoice.value.id, method)
    currentTradeNo.value = result.trade_no

    // Generate QR code image
    qrCodeDataUrl.value = await QRCode.toDataURL(result.qr_code_url, {
      width: 256,
      margin: 2,
      color: {
        dark: '#000000',
        light: '#ffffff',
      },
    })
    qrDialogVisible.value = true

    // Start polling invoice status every 3 seconds
    startPolling()
  } catch {
    // handled by interceptor
  }
}

function startPolling() {
  stopPolling()
  pollingTimer = setInterval(async () => {
    try {
      const updated = await getInvoice(invoiceId.value)
      if (updated.status === 'paid') {
        invoice.value = updated
        ElMessage.success('支付成功！')
        stopPolling()
        qrDialogVisible.value = false
      }
    } catch {
      // ignore polling errors
    }
  }, 3000)
}

function stopPolling() {
  if (pollingTimer) {
    clearInterval(pollingTimer)
    pollingTimer = null
  }
}

function handleQrDialogClose() {
  stopPolling()
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchInvoice()
})

onUnmounted(() => {
  stopPolling()
})
</script>

<template>
  <div v-loading="loading" class="invoice-detail">
    <template v-if="invoice">
      <!-- Header -->
      <div class="page-header">
        <el-link :underline="false" @click="router.push({ name: 'DealerInvoiceList' })">
          &laquo; 返回
        </el-link>
        <h2 class="page-title">
          账单详情
          <span class="invoice-no">{{ invoice.invoice_no }}</span>
          <el-tag :type="getStatusTagType(invoice.status)" size="medium" effect="dark" class="status-tag">
            {{ getStatusLabel(invoice.status) }}
          </el-tag>
        </h2>
      </div>

      <!-- Status & Payment actions -->
      <el-card shadow="never" class="status-card">
        <el-row :gutter="16" align="middle">
          <el-col :span="8">
            <div class="status-section">
              <span class="status-label">账单状态</span>
              <el-tag :type="getStatusTagType(invoice.status)" size="large" effect="dark">
                {{ getStatusLabel(invoice.status) }}
              </el-tag>
              <div class="due-date-text">
                到期日: {{ invoice.due_date }}
              </div>
            </div>
          </el-col>
          <el-col :span="16" class="payment-actions">
            <template v-if="isPayable && !paidInFull">
              <el-button
                type="primary"
                size="large"
                @click="handlePayOnline('alipay')"
              >
                支付宝支付
              </el-button>
              <el-button
                type="success"
                size="large"
                @click="handlePayOnline('wechat')"
              >
                微信支付
              </el-button>
              <el-button
                type="warning"
                size="large"
                plain
                @click="handlePayByBalance"
              >
                余额支付
              </el-button>
            </template>
            <el-tag v-else-if="invoice.status === 'paid'" type="success" size="large" effect="plain">
              已支付
            </el-tag>
            <el-tag v-else type="info" size="large" effect="plain">
              已作废
            </el-tag>
          </el-col>
        </el-row>
      </el-card>

      <!-- Invoice info -->
      <el-card shadow="never" class="info-card">
        <template #header>
          <span>账单信息</span>
        </template>
        <el-descriptions :column="2" border>
          <el-descriptions-item label="账单号">
            {{ invoice.invoice_no }}
          </el-descriptions-item>
          <el-descriptions-item label="账期">
            {{ invoice.period }}
          </el-descriptions-item>
          <el-descriptions-item label="到期日">
            {{ invoice.due_date }}
          </el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="getStatusTagType(invoice.status)" size="small">
              {{ getStatusLabel(invoice.status) }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="账单总额">
            <span class="price">¥{{ Number(invoice.total_amount).toFixed(2) }}</span>
          </el-descriptions-item>
          <el-descriptions-item label="已付金额">
            <span>¥{{ Number(invoice.paid_amount).toFixed(2) }}</span>
          </el-descriptions-item>
        </el-descriptions>
      </el-card>

      <!-- Invoice items -->
      <el-card shadow="never" class="items-card">
        <template #header>
          <span>账单明细 ({{ invoice.items.length }})</span>
        </template>
        <el-table :data="invoice.items" stripe size="small">
          <el-table-column label="项目类型" width="140">
            <template #default="{ row }: { row: InvoiceItem }">
              <el-tag size="small" type="info" effect="plain">
                {{ getItemTypeLabel(row.type) }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column label="描述" min-width="260">
            <template #default="{ row }: { row: InvoiceItem }">
              {{ row.description }}
            </template>
          </el-table-column>
          <el-table-column label="金额" width="140" align="right">
            <template #default="{ row }: { row: InvoiceItem }">
              <span class="price">¥{{ Number(row.amount).toFixed(2) }}</span>
            </template>
          </el-table-column>
        </el-table>
        <div class="total-line">
          <span>合计</span>
          <span class="price-large">¥{{ Number(invoice.total_amount).toFixed(2) }}</span>
        </div>
      </el-card>

      <!-- Download PDF -->
      <el-card shadow="never" class="actions-card">
        <el-button type="primary" plain @click="downloadPdf">
          下载 PDF
        </el-button>
      </el-card>
    </template>

    <!-- QR code dialog for online payment -->
    <el-dialog
      v-model="qrDialogVisible"
      :title="`${currentPaymentMethod === 'alipay' ? '支付宝' : '微信'}支付`"
      width="380px"
      align-center
      @close="handleQrDialogClose"
    >
      <div class="qr-container">
        <p class="qr-hint">请使用{{ currentPaymentMethod === 'alipay' ? '支付宝' : '微信' }}扫码支付</p>
        <img v-if="qrCodeDataUrl" :src="qrCodeDataUrl" alt="支付二维码" class="qr-image" />
        <p class="qr-amount">
          支付金额: <span class="price">¥{{ invoice ? Number(invoice.total_amount).toFixed(2) : '0.00' }}</span>
        </p>
        <p class="qr-trade-no" v-if="currentTradeNo">
          交易号: {{ currentTradeNo }}
        </p>
        <p class="qr-status-text">等待支付中...</p>
      </div>
      <template #footer>
        <el-button @click="qrDialogVisible = false">关闭</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<style scoped>
.invoice-detail {
  max-width: 1000px;
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

.invoice-no {
  font-size: 16px;
  font-weight: 400;
  color: var(--el-text-color-secondary);
}

.status-tag {
  margin-left: 4px;
}

.status-card {
  margin-bottom: 16px;
}

.status-section {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.status-label {
  font-size: 14px;
  color: var(--el-text-color-secondary);
}

.due-date-text {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.payment-actions {
  display: flex;
  gap: 12px;
  justify-content: flex-end;
  align-items: center;
}

.info-card,
.items-card,
.actions-card {
  margin-bottom: 16px;
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

.total-line {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 16px;
  font-size: 15px;
  font-weight: 600;
  border-top: 1px solid var(--el-border-color-light);
  margin-top: 8px;
}

.qr-container {
  text-align: center;
  padding: 16px 0;
}

.qr-hint {
  font-size: 14px;
  color: var(--el-text-color-secondary);
  margin-bottom: 16px;
}

.qr-image {
  width: 256px;
  height: 256px;
  border: 1px solid var(--el-border-color-light);
  margin-bottom: 16px;
}

.qr-amount {
  font-size: 16px;
  margin-bottom: 8px;
}

.qr-trade-no {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  margin-bottom: 8px;
}

.qr-status-text {
  font-size: 14px;
  color: #409eff;
  animation: pulse 1.5s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}
</style>
