<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { getCart, removeCartItem, updateCartItem, submitOrder } from '@/api/cart'
import type { CartItem } from '@/api/cart'

// ── State ──────────────────────────────────────────────────────────────────

const router = useRouter()
const loading = ref(false)
const cartItems = ref<CartItem[]>([])
const totalAmount = ref('0.00')
const remark = ref('')

// ── Computed ───────────────────────────────────────────────────────────────

const isEmpty = computed(() => cartItems.value.length === 0)

const subtotal = computed(() => {
  return cartItems.value.reduce((sum, item) => {
    return sum + Number(item.subtotal)
  }, 0)
})

function getConfigDisplay(item: CartItem): string {
  const parts: string[] = []
  if (item.config && typeof item.config === 'object') {
    for (const [key, value] of Object.entries(item.config)) {
      if (Array.isArray(value)) {
        parts.push(`${key}: ${value.join(', ')}`)
      } else {
        parts.push(`${key}: ${String(value)}`)
      }
    }
  }
  return parts.join(' | ')
}

function getPeriodLabel(months: number): string {
  if (months >= 12 && months % 12 === 0) {
    const years = months / 12
    return years === 1 ? '年付' : `${years}年付`
  }
  return `${months}个月`
}

async function fetchCart() {
  loading.value = true
  try {
    const data = await getCart()
    cartItems.value = data.items
    totalAmount.value = data.total_amount
  } catch {
    // handled by interceptor
  } finally {
    loading.value = false
  }
}

async function handleRemove(item: CartItem) {
  try {
    await ElMessageBox.confirm(`确定要将「${item.product_name}」从购物车移除吗？`, '确认移除', {
      type: 'warning',
      confirmButtonText: '确认',
      cancelButtonText: '取消',
    })
    await removeCartItem(item.id)
    ElMessage.success('已移除')
    await fetchCart()
  } catch {
    // cancelled
  }
}

async function handleQuantityChange(item: CartItem, newQty: number) {
  if (newQty < 1) return
  try {
    await updateCartItem(item.id, { quantity: newQty })
    await fetchCart()
  } catch {
    // handled by interceptor
  }
}

async function handleSubmitOrder() {
  if (isEmpty.value) {
    ElMessage.warning('购物车为空')
    return
  }
  try {
    await ElMessageBox.confirm(
      `共 ${cartItems.value.length} 件商品，合计 ¥${subtotal.value.toFixed(2)}，确认提交订单？`,
      '确认提交',
      {
        confirmButtonText: '确认',
        cancelButtonText: '取消',
        type: 'info',
      },
    )
    const result = await submitOrder({
      billing_cycle: undefined,
      remark: remark.value || undefined,
    })
    ElMessage.success('订单提交成功')
    router.push({ name: 'OrderDetail', params: { id: result.id } })
  } catch {
    // cancelled or error
  }
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchCart()
})
</script>

<template>
  <div class="cart-view">
    <h2 class="page-title">我的购物车</h2>

    <!-- Empty state -->
    <el-card v-if="isEmpty && !loading" shadow="never">
      <el-empty description="购物车是空的">
        <el-button type="primary" @click="router.push({ name: 'ProductList' })">
          去逛逛
        </el-button>
      </el-empty>
    </el-card>

    <!-- Cart table -->
    <template v-if="!isEmpty || loading">
      <el-card shadow="never" class="cart-card">
        <el-table
          :data="cartItems"
          v-loading="loading"
          stripe
          style="width: 100%"
        >
          <el-table-column label="产品" min-width="200">
            <template #default="{ row }: { row: CartItem }">
              <div class="product-info">
                <span class="product-name">{{ row.product_name }}</span>
                <el-tag size="small" type="info" effect="plain">
                  {{ row.product_type }}
                </el-tag>
              </div>
              <div v-if="getConfigDisplay(row)" class="config-info">
                {{ getConfigDisplay(row) }}
              </div>
            </template>
          </el-table-column>
          <el-table-column label="周期" width="100">
            <template #default="{ row }: { row: CartItem }">
              {{ getPeriodLabel(row.period_months) }}
            </template>
          </el-table-column>
          <el-table-column label="单价" width="120" align="right">
            <template #default="{ row }: { row: CartItem }">
              ¥{{ Number(row.unit_price).toFixed(2) }}
            </template>
          </el-table-column>
          <el-table-column label="数量" width="140" align="center">
            <template #default="{ row }: { row: CartItem }">
              <el-input-number
                :model-value="row.quantity"
                :min="1"
                :max="999"
                size="small"
                controls-position="right"
                style="width: 100px"
                @change="(val: number) => handleQuantityChange(row, val)"
              />
            </template>
          </el-table-column>
          <el-table-column label="小计" width="130" align="right">
            <template #default="{ row }: { row: CartItem }">
              <span class="subtotal-price">¥{{ Number(row.subtotal).toFixed(2) }}</span>
            </template>
          </el-table-column>
          <el-table-column label="操作" width="80" align="center">
            <template #default="{ row }: { row: CartItem }">
              <el-button text type="danger" size="small" @click="handleRemove(row)">
                移除
              </el-button>
            </template>
          </el-table-column>
        </el-table>
      </el-card>

      <!-- Order summary -->
      <el-row :gutter="16" class="summary-row">
        <el-col :span="16">
          <el-card shadow="never">
            <template #header>
              <span>备注</span>
            </template>
            <el-input
              v-model="remark"
              type="textarea"
              :rows="2"
              placeholder="订单备注（可选）"
              maxlength="500"
              show-word-limit
            />
          </el-card>
        </el-col>
        <el-col :span="8">
          <el-card shadow="never" class="summary-card">
            <template #header>
              <span>订单摘要</span>
            </template>
            <div class="summary-item">
              <span>商品数量</span>
              <span>{{ cartItems.length }} 件</span>
            </div>
            <div class="summary-item" v-for="item in cartItems" :key="item.id">
              <span class="summary-sub-item">{{ item.product_name }} &times; {{ item.quantity }}</span>
              <span>¥{{ Number(item.subtotal).toFixed(2) }}</span>
            </div>
            <el-divider />
            <div class="summary-total">
              <span>合计</span>
              <span class="total-amount">¥{{ subtotal.toFixed(2) }}</span>
            </div>
            <div class="summary-actions">
              <el-button style="width: 100%" @click="router.push({ name: 'ProductList' })">
                继续选购
              </el-button>
              <el-button
                type="primary"
                style="width: 100%; margin-top: 12px"
                size="large"
                :disabled="isEmpty"
                @click="handleSubmitOrder"
              >
                提交订单
              </el-button>
            </div>
          </el-card>
        </el-col>
      </el-row>
    </template>
  </div>
</template>

<style scoped>
.cart-view {
  max-width: 1200px;
}

.page-title {
  font-size: 22px;
  font-weight: 600;
  margin-bottom: 20px;
  color: var(--el-text-color-primary);
}

.cart-card {
  margin-bottom: 16px;
}

.product-info {
  display: flex;
  align-items: center;
  gap: 8px;
}

.product-name {
  font-weight: 500;
}

.config-info {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  margin-top: 4px;
}

.subtotal-price {
  font-weight: 600;
  color: #e6a23c;
}

.summary-row {
  margin-top: 0;
}

.summary-card {
  margin-bottom: 16px;
}

.summary-item {
  display: flex;
  justify-content: space-between;
  padding: 6px 0;
  font-size: 14px;
}

.summary-sub-item {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.summary-total {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 16px;
}

.total-amount {
  font-size: 24px;
  font-weight: 700;
  color: #e6a23c;
}

.summary-actions {
  margin-top: 16px;
}
</style>
