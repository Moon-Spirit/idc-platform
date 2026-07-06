<script setup lang="ts">
import { ref, reactive, computed, onMounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import request from '@/api/request'
import { addToCart } from '@/api/cart'
import { useAuthStore } from '@/stores/auth'

// ── Types ──────────────────────────────────────────────────────────────────

interface ProductSpecs {
  cpu?: string
  cores?: string | number
  ram?: string
  disk?: string
  bandwidth?: string
  ip?: string
  [key: string]: unknown
}

interface ProductDetail {
  id: number
  name: string
  type: string
  specs: ProductSpecs
  status: number
  description?: string
}

interface ProductPrice {
  product_id: number
  product_name: string
  monthly_price: string
  yearly_price: string
  bandwidth_95_price: string
  setup_fee: string
  price_source: string
}

// ── Props / Router ─────────────────────────────────────────────────────────

const route = useRoute()
const router = useRouter()
const authStore = useAuthStore()

const productId = computed(() => Number(route.params.id))

// ── State ──────────────────────────────────────────────────────────────────

const loading = ref(true)
const product = ref<ProductDetail | null>(null)
const price = ref<ProductPrice | null>(null)

// ── Configuration ──────────────────────────────────────────────────────────

const selectedOs = ref('centos79')
const extraIpCount = ref(0)
const billingCycle = ref<'monthly' | 'yearly'>('monthly')
const quantity = ref(1)

const addons = reactive<Record<string, boolean>>({
  ddos_basic: false,
  backup_10g: false,
  backup_30g: false,
  monitoring: false,
})

interface AddOnOption {
  key: string
  label: string
  monthlyPrice: number
}

const ADDON_OPTIONS: AddOnOption[] = [
  { key: 'ddos_basic', label: 'DDoS基础防护', monthlyPrice: 50 },
  { key: 'backup_10g', label: '备份服务 10GB', monthlyPrice: 20 },
  { key: 'backup_30g', label: '备份服务 30GB', monthlyPrice: 50 },
  { key: 'monitoring', label: '监控服务', monthlyPrice: 30 },
]

const OS_OPTIONS = [
  { value: 'centos79', label: 'CentOS 7.9' },
  { value: 'centos85', label: 'CentOS 8.5' },
  { value: 'ubuntu2204', label: 'Ubuntu 22.04' },
  { value: 'ubuntu2004', label: 'Ubuntu 20.04' },
  { value: 'debian12', label: 'Debian 12' },
  { value: 'windows2022', label: 'Windows Server 2022' },
  { value: 'esxi8', label: 'VMware ESXi 8.0' },
]

// ── Computed prices ────────────────────────────────────────────────────────

const baseUnitPrice = computed(() => {
  if (!price.value) return 0
  const raw = billingCycle.value === 'yearly'
    ? price.value.yearly_price
    : price.value.monthly_price
  return Number(raw) || 0
})

const addonsTotal = computed(() => {
  let total = 0
  for (const opt of ADDON_OPTIONS) {
    if (addons[opt.key]) {
      total += billingCycle.value === 'yearly' ? opt.monthlyPrice * 12 : opt.monthlyPrice
    }
  }
  return total
})

const addonSelectionCount = computed(() => {
  return ADDON_OPTIONS.filter(o => addons[o.key]).length
})

const totalPrice = computed(() => {
  return (baseUnitPrice.value + addonsTotal.value) * quantity.value
})

const priceDisplay = computed(() => {
  return `¥${totalPrice.value.toFixed(2)}`
})

function getCycleLabel(): string {
  return billingCycle.value === 'yearly' ? '年' : '月'
}

function getCycleSavings(): string {
  if (!price.value || billingCycle.value !== 'yearly') return ''
  const monthly = Number(price.value.monthly_price)
  const yearly = Number(price.value.yearly_price)
  if (monthly > 0 && yearly > 0) {
    const saving = monthly * 12 * quantity.value - yearly * quantity.value
    if (saving > 0) {
      return `省 ¥${saving.toFixed(2)}`
    }
  }
  return ''
}

const yearlySavingsPercent = computed(() => {
  if (!price.value) return 0
  const monthly = Number(price.value.monthly_price)
  const yearly = Number(price.value.yearly_price)
  if (monthly <= 0 || yearly <= 0) return 0
  return Math.round((monthly * 12 - yearly) / (monthly * 12) * 100)
})

const yearlyComparison = computed(() => {
  if (!price.value) return 0
  return Number(price.value.monthly_price) * 12 * quantity.value
})

// ── Methods ────────────────────────────────────────────────────────────────

async function fetchProductDetail() {
  loading.value = true
  try {
    const [productRes, priceRes] = await Promise.all([
      request.get<{ data: ProductDetail }>(`/products/${productId.value}`),
      request.get<{ data: ProductPrice }>(`/products/${productId.value}/price`),
    ])
    product.value = productRes.data.data
    price.value = priceRes.data.data
  } catch {
    ElMessage.error('产品不存在或已下架')
    router.push({ name: 'ProductList' })
  } finally {
    loading.value = false
  }
}

function formatSpecLabel(key: string): string {
  const labels: Record<string, string> = {
    cpu: 'CPU',
    cores: '核心数',
    ram: '内存',
    disk: '硬盘',
    bandwidth: '带宽',
    ip: 'IP数量',
  }
  return labels[key] ?? key
}

function getSetupFeeText(): string {
  if (!price.value) return '-'
  const fee = Number(price.value.setup_fee)
  return fee === 0 ? '免费' : `¥${fee.toFixed(2)}`
}

async function handleAddToCart() {
  if (!product.value) return
  try {
    const config: Record<string, unknown> = {}
    if (product.value.type === 'bare_metal' || product.value.type === 'cloud') {
      config.os = selectedOs.value
    }
    if (extraIpCount.value > 0) {
      config.ip_count = extraIpCount.value
    }
    const selectedAddons = ADDON_OPTIONS.filter(o => addons[o.key]).map(o => o.key)
    if (selectedAddons.length > 0) {
      config.addons = selectedAddons
    }

    // Build a label-friendly config display
    const labelConfig: Record<string, unknown> = {}
    if (product.value.type === 'bare_metal' || product.value.type === 'cloud') {
      const osLabel = OS_OPTIONS.find(o => o.value === selectedOs.value)?.label ?? selectedOs.value
      labelConfig.OS = osLabel
    }
    if (extraIpCount.value > 0) {
      labelConfig['额外IP'] = `${extraIpCount.value}个`
    }
    if (selectedAddons.length > 0) {
      labelConfig['增值服务'] = selectedAddons.map(k => ADDON_OPTIONS.find(o => o.key === k)?.label ?? k)
    }

    await addToCart({
      product_id: product.value.id,
      quantity: quantity.value,
      period_months: billingCycle.value === 'yearly' ? 12 : 1,
      config: labelConfig,
    })
    ElMessage.success('已添加到购物车')
  } catch {
    // handled by interceptor
  }
}

async function handleBuyNow() {
  if (!product.value) return
  try {
    const config: Record<string, unknown> = {}
    if (product.value.type === 'bare_metal' || product.value.type === 'cloud') {
      config.os = selectedOs.value
    }
    if (extraIpCount.value > 0) {
      config.ip_count = extraIpCount.value
    }
    const selectedAddons = ADDON_OPTIONS.filter(o => addons[o.key]).map(o => o.key)
    if (selectedAddons.length > 0) {
      config.addons = selectedAddons
    }

    const labelConfig: Record<string, unknown> = {}
    if (product.value.type === 'bare_metal' || product.value.type === 'cloud') {
      const osLabel = OS_OPTIONS.find(o => o.value === selectedOs.value)?.label ?? selectedOs.value
      labelConfig.OS = osLabel
    }
    if (extraIpCount.value > 0) {
      labelConfig['额外IP'] = `${extraIpCount.value}个`
    }
    if (selectedAddons.length > 0) {
      labelConfig['增值服务'] = selectedAddons.map(k => ADDON_OPTIONS.find(o => o.key === k)?.label ?? k)
    }

    await addToCart({
      product_id: product.value.id,
      quantity: quantity.value,
      period_months: billingCycle.value === 'yearly' ? 12 : 1,
      config: labelConfig,
    })
    ElMessage.success('已添加到购物车，正在跳转...')
    router.push({ name: 'CartView' })
  } catch {
    // handled by interceptor
  }
}

function renderSpecValue(key: string, value: unknown): string {
  if (key === 'cores') return `${value}核`
  if (key === 'ram' && typeof value === 'string') return value
  if (key === 'disk' && typeof value === 'string') return value
  return String(value ?? '-')
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchProductDetail()
})
</script>

<template>
  <div v-loading="loading" class="configurator">
    <template v-if="product && price">
      <div class="page-header">
        <el-link :underline="false" @click="router.push({ name: 'ProductList' })">
          &laquo; 返回产品列表
        </el-link>
        <h2 class="page-title">{{ product.name }}</h2>
      </div>

      <el-row :gutter="20">
        <!-- Product specs -->
        <el-col :span="16">
          <el-card shadow="never" class="specs-card">
            <template #header>
              <span>产品信息</span>
            </template>
            <el-descriptions :column="2" border>
              <el-descriptions-item
                v-for="(value, key) in product.specs"
                :key="key"
                :label="formatSpecLabel(key)"
              >
                {{ renderSpecValue(key, value) }}
              </el-descriptions-item>
            </el-descriptions>
          </el-card>

          <!-- Configuration options -->
          <el-card shadow="never" class="config-card">
            <template #header>
              <span>配置选项</span>
            </template>

            <!-- OS selection (for bare_metal / cloud) -->
            <div v-if="product.type === 'bare_metal' || product.type === 'cloud'" class="config-item">
              <label class="config-label">操作系统</label>
              <el-select v-model="selectedOs" style="width: 240px">
                <el-option
                  v-for="opt in OS_OPTIONS"
                  :key="opt.value"
                  :label="opt.label"
                  :value="opt.value"
                />
              </el-select>
            </div>

            <!-- Extra IP quantity -->
            <div class="config-item">
              <label class="config-label">额外IP数量</label>
              <el-input-number
                v-model="extraIpCount"
                :min="0"
                :max="100"
                controls-position="right"
              />
              <span class="config-hint">个 &times; ¥50/月</span>
            </div>

            <!-- Add-ons -->
            <div class="config-item">
              <label class="config-label">增值服务</label>
              <div class="addon-list">
                <el-checkbox
                  v-for="opt in ADDON_OPTIONS"
                  :key="opt.key"
                  v-model="addons[opt.key]"
                  :label="opt.key"
                  class="addon-checkbox"
                >
                  {{ opt.label }} (¥{{ opt.monthlyPrice }}/月)
                </el-checkbox>
              </div>
            </div>

            <!-- Quantity -->
            <div class="config-item">
              <label class="config-label">数量</label>
              <el-input-number
                v-model="quantity"
                :min="1"
                :max="999"
                controls-position="right"
              />
            </div>
          </el-card>
        </el-col>

        <!-- Price sidebar -->
        <el-col :span="8">
          <el-card shadow="never" class="price-card">
            <template #header>
              <span>价格信息</span>
            </template>

            <div class="price-block">
              <div class="price-section-label">标准价格</div>
              <div class="price-row">
                <span class="price-label">月付</span>
                <span class="price-value">¥{{ Number(price.monthly_price).toFixed(2) }}</span>
              </div>
              <div class="price-row">
                <span class="price-label">年付</span>
                <span class="price-value">¥{{ Number(price.yearly_price).toFixed(2) }}</span>
                <span v-if="yearlySavingsPercent > 0" class="savings-pct-badge">
                  年付省{{ yearlySavingsPercent }}%
                </span>
              </div>
              <div v-if="price.bandwidth_95_price && Number(price.bandwidth_95_price) > 0" class="price-row">
                <span class="price-label">带宽95计费</span>
                <span class="price-value">¥{{ Number(price.bandwidth_95_price).toFixed(2) }}</span>
              </div>
              <div class="price-row">
                <span class="price-label">设置费</span>
                <span class="price-value">{{ getSetupFeeText() }}</span>
              </div>
            </div>

            <!-- Price comparison (monthly vs yearly) -->
            <div v-if="billingCycle === 'yearly' && yearlySavingsPercent > 0" class="comparison-block">
              <div class="price-section-label">价格对比</div>
              <div class="price-row">
                <span class="price-label">月付&times;12</span>
                <span class="price-value original">¥{{ yearlyComparison.toFixed(2) }}</span>
              </div>
              <div class="price-row">
                <span class="price-label">年付价格</span>
                <span class="price-value dealer">¥{{ (Number(price.yearly_price) * quantity.value).toFixed(2) }}</span>
              </div>
              <div class="price-row savings-row">
                <span class="price-label">节省</span>
                <span class="price-value savings">
                  -¥{{ (yearlyComparison - Number(price.yearly_price) * quantity.value).toFixed(2) }}
                </span>
              </div>
            </div>

            <!-- Billing cycle toggle -->
            <div class="cycle-toggle">
              <label class="config-label" style="margin-bottom: 8px; display: block">购买周期</label>
              <el-radio-group v-model="billingCycle">
                <el-radio-button value="monthly">月付</el-radio-button>
                <el-radio-button value="yearly">
                  年付
                  <span v-if="yearlySavingsPercent > 0" class="yearly-badge">省{{ yearlySavingsPercent }}%</span>
                </el-radio-button>
              </el-radio-group>
              <div v-if="getCycleSavings()" class="saving-badge">
                {{ getCycleSavings() }}
              </div>
            </div>

            <!-- Total -->
            <el-divider />
            <div class="total-section">
              <div class="total-row">
                <span class="total-label">合计</span>
                <span class="total-price">¥{{ totalPrice.toFixed(2) }}</span>
              </div>
              <div class="total-cycle">/{{ getCycleLabel() }}</div>
            </div>

            <!-- Actions -->
            <div class="action-buttons">
              <el-button type="primary" size="large" style="width: 100%" @click="handleAddToCart">
                加入购物车
              </el-button>
              <el-button type="success" size="large" style="width: 100%; margin-top: 12px" @click="handleBuyNow">
                立即购买
              </el-button>
            </div>
          </el-card>
        </el-col>
      </el-row>
    </template>
  </div>
</template>

<style scoped>
.configurator {
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
}

.specs-card,
.config-card,
.price-card {
  margin-bottom: 16px;
}

.config-item {
  margin-bottom: 18px;
}

.config-label {
  display: block;
  font-size: 14px;
  font-weight: 500;
  color: var(--el-text-color-regular);
  margin-bottom: 6px;
}

.config-hint {
  margin-left: 8px;
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.addon-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.addon-checkbox {
  margin-right: 0;
}

.price-block {
  margin-bottom: 16px;
}

.price-section-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--el-text-color-placeholder);
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: 6px;
  padding-bottom: 4px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.price-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 5px 0;
  font-size: 14px;
}

.price-label {
  color: var(--el-text-color-secondary);
}

.price-value {
  font-weight: 500;
  color: var(--el-text-color-primary);
}

.price-value.original {
  text-decoration: line-through;
  color: var(--el-text-color-placeholder);
  font-weight: 400;
}

.price-value.dealer {
  font-weight: 600;
  color: #e6a23c;
}

.price-value.savings {
  font-weight: 700;
  color: #f56c6c;
}

.savings-row {
  background: rgba(245, 108, 108, 0.06);
  border-radius: 4px;
  padding: 6px 8px;
}

.savings-pct-badge {
  display: inline-block;
  padding: 0 6px;
  background: #f56c6c;
  color: #fff;
  border-radius: 3px;
  font-size: 11px;
  font-weight: 600;
  line-height: 18px;
  margin-left: 4px;
}

.comparison-block {
  background: var(--el-fill-color-lighter);
  border-radius: 6px;
  padding: 8px 10px;
  margin-bottom: 12px;
}

.yearly-badge {
  display: inline-block;
  margin-left: 4px;
  padding: 0 5px;
  background: #f56c6c;
  color: #fff;
  border-radius: 3px;
  font-size: 11px;
  font-weight: 600;
  line-height: 18px;
  vertical-align: middle;
}

.cycle-toggle {
  margin-bottom: 8px;
}

.saving-badge {
  display: inline-block;
  margin-top: 8px;
  padding: 2px 10px;
  background: #f56c6c;
  color: #fff;
  border-radius: 4px;
  font-size: 12px;
}

.total-section {
  margin-bottom: 16px;
}

.total-row {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
}

.total-label {
  font-size: 16px;
  font-weight: 600;
}

.total-price {
  font-size: 28px;
  font-weight: 700;
  color: #e6a23c;
}

.total-cycle {
  text-align: right;
  font-size: 13px;
  color: var(--el-text-color-secondary);
  margin-top: 4px;
}

.action-buttons {
  margin-top: 16px;
}
</style>
