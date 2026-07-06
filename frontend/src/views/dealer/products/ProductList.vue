<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import request from '@/api/request'

// ── Types ──────────────────────────────────────────────────────────────────

interface Product {
  id: number
  name: string
  type: string
  specs: Record<string, string>
  status: number
  sort_order: number
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

// ── Type labels ────────────────────────────────────────────────────────────

const TYPE_LABELS: Record<string, string> = {
  bare_metal: '物理机',
  cloud: '云服务器',
  bandwidth: '带宽',
  ip: 'IP',
  addon: '增值服务',
  rack: '机柜',
}

const TYPE_ORDER: Record<string, number> = {
  all: -1,
  bare_metal: 0,
  cloud: 1,
  bandwidth: 2,
  ip: 3,
  addon: 4,
  rack: 5,
}

// ── State ──────────────────────────────────────────────────────────────────

const router = useRouter()
const loading = ref(false)
const products = ref<Product[]>([])
const activeType = ref('all')
const productTypes = ref<string[]>([])

// ── Computed ───────────────────────────────────────────────────────────────

const sortedTypes = ref<{ key: string; label: string }[]>([])

function buildTypeTabs(types: string[]): { key: string; label: string }[] {
  const tabs = [{ key: 'all', label: '全部' }]
  const sorted = [...types].sort(
    (a, b) => (TYPE_ORDER[a] ?? 99) - (TYPE_ORDER[b] ?? 99),
  )
  for (const t of sorted) {
    tabs.push({ key: t, label: TYPE_LABELS[t] ?? t })
  }
  return tabs
}

// ── Methods ────────────────────────────────────────────────────────────────

function getSpecSummary(specs: Record<string, string>): string {
  const parts: string[] = []
  if (specs.cpu) parts.push(specs.cpu)
  if (specs.cores) parts.push(`${specs.cores}核`)
  if (specs.ram) parts.push(specs.ram)
  if (specs.disk) parts.push(specs.disk)
  if (specs.bandwidth) parts.push(specs.bandwidth)
  return parts.join(' | ')
}

function getTypeLabel(type: string): string {
  return TYPE_LABELS[type] ?? type
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

async function fetchProducts() {
  loading.value = true
  try {
    const params: Record<string, unknown> = { status: 1 }
    if (activeType.value !== 'all') {
      params.type = activeType.value
    }
    const res = await request.get<ProductsResponse>('/products', { params })
    products.value = res.data.data.items
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
    sortedTypes.value = [{ key: 'all', label: '全部' }]
  }
}

function handleTypeChange() {
  fetchProducts()
}

function goToProduct(id: number) {
  router.push({ name: 'ProductConfigurator', params: { id } })
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(async () => {
  await fetchProductTypes()
  await fetchProducts()
})
</script>

<template>
  <div class="product-list">
    <!-- Type filter tabs -->
    <el-card shadow="never" class="tabs-card">
      <el-tabs v-model="activeType" @tab-change="handleTypeChange">
        <el-tab-pane
          v-for="tab in sortedTypes"
          :key="tab.key"
          :label="tab.label"
          :name="tab.key"
        />
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
              <span class="product-name">{{ product.name }}</span>
              <el-tag :type="getTypeTagType(product.type)" size="small" effect="plain">
                {{ getTypeLabel(product.type) }}
              </el-tag>
            </div>
            <div class="product-specs">
              <p class="spec-line">{{ getSpecSummary(product.specs) }}</p>
            </div>
            <div class="product-footer">
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

.product-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.product-name {
  font-size: 16px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.product-specs {
  margin-bottom: 12px;
}

.spec-line {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  line-height: 1.6;
  display: -webkit-box;
  -webkit-line-clamp: 3;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.product-footer {
  border-top: 1px solid var(--el-border-color-light);
  padding-top: 10px;
  text-align: right;
}
</style>
