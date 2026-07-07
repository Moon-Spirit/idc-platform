<script setup lang="ts">
import { onMounted, computed } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAppStore } from '@/stores/app'
import { useAuthStore } from '@/stores/auth'
import { useCartStore } from '@/stores/cart'
import { Expand, Fold } from '@element-plus/icons-vue'

const router = useRouter()
const route = useRoute()
const appStore = useAppStore()
const authStore = useAuthStore()
const cartStore = useCartStore()

function toggleSidebar() {
  appStore.toggleSidebar()
}

function handleLogout() {
  authStore.logout()
}

function goToCart() {
  router.push({ name: 'CartView' })
}

const breadcrumbLabel = computed(() => {
  const matched = route.matched
  const labels: string[] = []
  for (const r of matched) {
    const title = r.meta?.title as string | undefined
    if (title && title !== 'Layout') {
      labels.push(title)
    }
  }
  return labels.length > 0 ? labels[labels.length - 1] : ''
})

const userDisplayName = computed(() => {
  return authStore.user?.username ?? '未登录'
})

const userRole = computed(() => {
  const role = authStore.user?.role
  if (role === 'admin') return '管理员'
  if (role === 'dealer') return '经销商'
  return ''
})

onMounted(() => {
  if (authStore.isLoggedIn) {
    cartStore.fetchCount()
  }
})
</script>

<template>
  <div class="header-inner">
    <div class="header-left">
      <el-button
        :icon="appStore.sidebarCollapsed ? Expand : Fold"
        text
        class="collapse-btn"
        @click="toggleSidebar"
      />
      <el-divider direction="vertical" class="header-divider" />
      <span class="breadcrumb-label">{{ breadcrumbLabel }}</span>
    </div>
    <div class="header-right">
      <!-- Cart badge (dealer only) -->
      <el-badge
        v-if="authStore.user?.role === 'dealer'"
        :value="cartStore.itemCount"
        :hidden="cartStore.itemCount === 0"
        class="cart-badge"
      >
        <el-button text size="large" class="header-icon-btn" @click="goToCart">
          <el-icon :size="20"><ShoppingCart /></el-icon>
        </el-button>
      </el-badge>

      <el-divider
        v-if="authStore.user?.role === 'dealer'"
        direction="vertical"
        class="header-divider"
      />

      <el-dropdown trigger="click" class="user-dropdown">
        <span class="user-info">
          <el-avatar :size="32" class="user-avatar">
            {{ userDisplayName.charAt(0).toUpperCase() }}
          </el-avatar>
          <span class="user-detail">
            <span class="user-name">{{ userDisplayName }}</span>
            <span v-if="userRole" class="user-role">{{ userRole }}</span>
          </span>
          <el-icon class="dropdown-arrow"><ArrowDown /></el-icon>
        </span>
        <template #dropdown>
          <el-dropdown-menu>
            <el-dropdown-item @click="handleLogout" divided>
              <el-icon><SwitchButton /></el-icon>
              退出登录
            </el-dropdown-item>
          </el-dropdown-menu>
        </template>
      </el-dropdown>
    </div>
  </div>
</template>

<style scoped>
.header-inner {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.collapse-btn {
  font-size: 18px;
  color: var(--text-regular);
  padding: 6px;
  border-radius: 6px;
  transition: background-color 0.2s;
}
.collapse-btn:hover {
  background-color: var(--border-color-lighter);
}

.header-divider {
  height: 20px;
  border-color: var(--border-color);
}

.breadcrumb-label {
  font-size: 15px;
  font-weight: 600;
  color: var(--text-primary);
}

.header-right {
  display: flex;
  align-items: center;
  gap: 4px;
}

.header-icon-btn {
  padding: 6px;
  border-radius: 6px;
  transition: background-color 0.2s;
  color: var(--text-regular);
}
.header-icon-btn:hover {
  background-color: var(--border-color-lighter);
}

.cart-badge {
  margin-right: 0;
}

/* ── User dropdown ──────────────────────────────────────────────────────── */
.user-dropdown {
  cursor: pointer;
  padding: 4px 8px;
  border-radius: 8px;
  transition: background-color 0.2s;
}
.user-dropdown:hover {
  background-color: var(--border-color-lighter);
}

.user-info {
  display: flex;
  align-items: center;
  gap: 8px;
}

.user-avatar {
  background-color: var(--primary-color);
  color: #fff;
  font-weight: 600;
  font-size: 14px;
  flex-shrink: 0;
}

.user-detail {
  display: flex;
  flex-direction: column;
  line-height: 1.3;
}

.user-name {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-primary);
}

.user-role {
  font-size: 11px;
  color: var(--text-secondary);
}

.dropdown-arrow {
  font-size: 12px;
  color: var(--text-placeholder);
}
</style>
