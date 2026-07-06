<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import request from '@/api/request'
import { addToCart } from '@/api/cart'
import { useCartStore } from '@/stores/cart'
import { useAuthStore } from '@/stores/auth'

// ── Types ──────────────────────────────────────────────────────────────────

interface Product {
  id: number
  name: string
  type: string
  specs: Record<string, string>
  status: number
  sort_order: number
}

interface ProductPrice {
  product_id: number
  monthly_price: string
  yearly_price: string
  setup_fee: string
  bandwidth_95_price: string | null
}

interface ProductTypesResponse {
  data: string[]
}

interface ProductsResponse {
  data: {
    items: Product[]
    total: number
    page: number
    per_page: number
  }
}

// ── Type config ────────────────────────────────────────────────────────────

interface TypeTab {
  key: string
  label: string
  icon: string
}

const TYPE_CONFIG: Record<string, { label: string; icon: string; order: number }> = {
  bare_metal: { label: '服务器', icon: 'Monitor', order: 0 },
  cloud: { label: '云服务器', icon: 'Cloudy', order: 1 },
  bandwidth: { label: '带宽', icon: 'Connection', order: 2 },
  ip: { label: 'IP', icon: 'Link', order: 3 },
  addon: { label: '增值服务', icon: 'Tools', order: 4 },
  rack: { label: '机柜', icon: 'HomeFilled', order: 5 },
}

function getTypeLabel(type: string): string {
  return TYPE_CONFIG[type]?.label ?? type
}

function getTypeIcon(type: string): string {
  return TYPE_CONFIG[type]?.icon ?? 'Goods'
}

function getTypeTagType(type: string): string {
  const map: Record<string, string> = {
    bare_metal: '',
    cloud: 'success',
    bandwidth: 'warning',
    ip: 'info',
    addon: 'danger',
    rack: '',
  }
  return map[type] ?? ''
}

// ── State ──────────────────────────────────────────────────────────────────

const router = useRouter()
const cartStore = useCartStore()
const authStore = useAuthStore()

const loading = ref(false)
const products = ref<Product[]>([])
const prices = ref<Map<number, ProductPrice>>(new Map())
const activeType = ref('all')
const productTypes = ref<string[]>([])
const sortedTypes = ref<TypeTab[]>([])

// ── Methods ────────────────────────────────────────────────────────────────

function buildTypeTabs(types: string[]): TypeTab[] {
  const tabs: TypeTab[] = [{ key: 'all', label: '全部', icon: 'Grid' }]
  const sorted = [...types].sort(
    (a, b) => (TYPE_CONFIG[a]?.order ?? 99) - (TYPE_CONFIG[b]?.order ?? 99),
  )
  for (const t of sorted) {
    tabs.push({ key: t, label: getTypeLabel(t), icon: getTypeIcon(t) })
  }
  return tabs
}

function getSpecSummary(specs: Record<string, string>): string {
  const parts: string[] = []
  if (specs.cpu) parts.push(specs.cpu)
  if (specs.cores) parts.push(`${specs.cores}核`)
  if (specs.ram) parts.push(specs.ram)
  if (specs.disk) parts.push(specs.disk)
  if (specs.bandwidth) parts.push(specs.bandwidth)
  return parts.join(' | ')
}

function getPriceDisplay(productId: number): { monthly: string; yearly: string } | null {
  const p = prices.value.get(productId)
  if (!p) return null
  return {
    monthly: `¥${Number(p.monthly_price).toFixed(2)}`,
    yearly: `¥${Number(p.yearly_price).toFixed(2)}`,
  }
}

function getYearlySavingsPercent(productId: number): number | null {
  const p = prices.value.get(productId)
  if (!p) return null
  const monthly = Number(p.monthly_price)
  const yearly = Number(p.yearly_price)
  if (monthly <= 0 || yearly <= 0) return null
  const saving = (monthly * 12 - yearly) / (monthly * 12) * 100
  return Math.round(saving)
}

function getSetupFee(productId: number): string {
  const p = prices.value.get(productId)
  if (!p) return '-'
  const fee = Number(p.setup_fee)
  return fee === 0 ? '免费' : `¥${fee.toFixed(2)}`
}

async function fetchProducts() {
  loading.value = true
  try {
    const params: Record<string, unknown> = { status: 1 }
    if (activeType.value !== 'all') {
      params.type = activeType.value
    }
    const res = await request.get<ProductsResponse>('/products', { params })
    const items = res.data.data.items
    products.value = items

    // Fetch prices for each product in parallel
    const pricePromises = items.map(async (prod) => {
      try {
        const priceRes = await request.get<{ data: ProductPrice }>(`/products/${prod.id}/price`)
        return { id: prod.id, price: priceRes.data.data }
      } catch {
        return null
      }
    })
    const priceResults = await Promise.all(pricePromises)
    const priceMap = new Map<number, ProductPrice>()
    for (const r of priceResults) {
      if (r) priceMap.set(r.id, r.price)
    }
    prices.value = priceMap
  } catch {
    // handled by interceptor
  } finally {
    loading.value = false
  }
}

async function fetchProductTypes() {
  try {
    const res = await request.get<ProductTypesResponse>('/products/types')
    productTypes.value = res.data.data
    sortedTypes.value = buildTypeTabs(productTypes.value)
  } catch {
    sortedTypes.value = [{ key: 'all', label: '全部', icon: 'Grid' }]
  }
}

function handleTypeChange() {
  fetchProducts()
}

function goToProduct(id: number) {
  router.push({ name: 'ProductConfigurator', params: { id } })
}

async function handleQuickAdd(product: Product, event: Event) {
  event.stopPropagation()
  try {
    await addToCart({
      product_id: product.id,
      quantity: 1,
      period_months: 1,
    })
    ElMessage.success(`「${product.name}」已添加到购物车`)
    await cartStore.incrementBy(1)
  } catch {
    // handled by interceptor
  }
}

function getDealerLevelLabel(): string {
  const level = authStore.user?.distributor_id ? '签约' : '标准'
  return level
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(async () => {
  await fetchProductTypes()
  await fetchProducts()
})
</script>

<template>
  <div class="product-list">
    <!-- Type filter tabs with icons -->
    <el-card shadow="never" class="tabs-card">
      <el-tabs v-model="activeType" @tab-change="handleTypeChange">
        <el-tab-pane
          v-for="tab in sortedTypes"
          :key="tab.key"
          :name="tab.key"
        >
          <template #label>
            <span class="tab-label">
              <el-icon :size="16"><component :is="tab.icon" /></el-icon>
              {{ tab.label }}
            </span>
          </template>
        </el-tab-pane>
      </el-tabs>
    </el-card>

    <!-- Product grid -->
    <el-card shadow="never" class="products-card">
      <div v-if="loading" v-loading="loading" class="loading-placeholder" />
      <div v-else-if="products.length === 0" class="empty-state">
        <el-empty description="暂无产品" />
      </div>
      <el-row v-else :gutter="20">
        <el-col
          v-for="product in products"
          :key="product.id"
          :xs="24"
          :sm="12"
          :md="8"
          :lg="6"
          class="product-col"
        >
          <el-card
            shadow="hover"
            class="product-card"
            @click="goToProduct(product.id)"
          >
            <div class="product-header">
              <div class="product-name-row">
                <el-icon :size="18" class="type-icon" :class="`icon-${product.type}`">
                  <component :is="getTypeIcon(product.type)" />
                </el-icon>
                <span class="product-name">{{ product.name }}</span>
              </div>
              <el-tag :type="getTypeTagType(product.type)" size="small" effect="plain">
                {{ getTypeLabel(product.type) }}
              </el-tag>
            </div>

            <div class="product-specs">
              <p class="spec-line">{{ getSpecSummary(product.specs) }}</p>
            </div>

            <!-- Price display -->
            <div class="product-prices" v-if="getPriceDisplay(product.id)">
              <div class="price-row">
                <span class="price-label">月付</span>
                <span class="price-monthly">{{ getPriceDisplay(product.id)!.monthly }}</span>
              </div>
              <div class="price-row">
                <span class="price-label">年付</span>
                <span class="price-yearly">{{ getPriceDisplay(product.id)!.yearly }}</span>
                <span
                  v-if="getYearlySavingsPercent(product.id)"
                  class="savings-tag"
                >
                  省{{ getYearlySavingsPercent(product.id) }}%
                </span>
              </div>
              <div class="price-row price-footnote">
                <span class="price-label">设置费</span>
                <span class="setup-fee">{{ getSetupFee(product.id) }}</span>
              </div>
            </div>

            <!-- Price tooltip -->
            <div class="price-tooltip-row" v-if="getPriceDisplay(product.id)">
              <el-tooltip
                placement="bottom"
                trigger="click"
                :content="`经销商 tier 专属定价。年付相比月付可省更多。设置费${getSetupFee(product.id)}。`"
              >
                <el-button text size="small" type="info">
                  <el-icon><InfoFilled /></el-icon>
                  价格说明
                </el-button>
              </el-tooltip>
            </div>

            <div class="product-footer">
              <el-button
                type="primary"
                size="small"
                @click="(e: Event) => handleQuickAdd(product, e)"
              >
                快速加入
              </el-button>
              <el-button text type="primary" size="small">
                查看详情 &raquo;
              </el-button>
            </div>
          </el-card>
        </el-col>
      </el-row>
    </el-card>
  </div>
</template>

<style scoped>
.tabs-card {
  margin-bottom: 16px;
}

.tab-label {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.products-card {
  min-height: 300px;
}

.loading-placeholder {
  height: 200px;
}

.empty-state {
  padding: 60px 0;
}

.product-col {
  margin-bottom: 20px;
}

.product-card {
  cursor: pointer;
  transition: transform 0.2s, box-shadow 0.2s;
}

.product-card:hover {
  transform: translateY(-2px);
}

/* ── Product header ─────────────────────────────────────────────────────── */

.product-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 10px;
}

.product-name-row {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
}

.type-icon {
  flex-shrink: 0;
}

.icon-bare_metal { color: #409eff; }
.icon-cloud { color: #67c23a; }
.icon-bandwidth { color: #e6a23c; }
.icon-ip { color: #909399; }
.icon-addon { color: #f56c6c; }

.product-name {
  font-size: 15px;
  font-weight: 600;
  color: var(--el-text-color-primary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

/* ── Specs ───────────────────────────────────────────────────────────────── */

.product-specs {
  margin-bottom: 10px;
}

.spec-line {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  line-height: 1.6;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

/* ── Prices ──────────────────────────────────────────────────────────────── */

.product-prices {
  background: var(--el-fill-color-lighter);
  border-radius: 6px;
  padding: 8px 10px;
  margin-bottom: 6px;
}

.price-row {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  line-height: 1.8;
}

.price-label {
  color: var(--el-text-color-secondary);
  min-width: 32px;
}

.price-monthly {
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.price-yearly {
  font-weight: 600;
  color: #e6a23c;
}

.savings-tag {
  display: inline-block;
  padding: 0 6px;
  background: #f56c6c;
  color: #fff;
  border-radius: 3px;
  font-size: 11px;
  font-weight: 600;
  line-height: 18px;
}

.price-footnote {
  font-size: 12px;
}

.setup-fee {
  color: var(--el-text-color-secondary);
}

/* ── Price tooltip ───────────────────────────────────────────────────────── */

.price-tooltip-row {
  margin-bottom: 8px;
}

/* ── Footer ──────────────────────────────────────────────────────────────── */

.product-footer {
  border-top: 1px solid var(--el-border-color-light);
  padding-top: 8px;
  display: flex;
  justify-content: flex-end;
  align-items: center;
  gap: 8px;
}
</style>
