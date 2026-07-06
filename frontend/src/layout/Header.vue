<script setup lang="ts">
import { useRouter } from 'vue-router'
import { useAppStore } from '@/stores/app'
import { useAuthStore } from '@/stores/auth'
import { Expand, Fold } from '@element-plus/icons-vue'

const router = useRouter()
const appStore = useAppStore()
const authStore = useAuthStore()

function toggleSidebar() {
  appStore.toggleSidebar()
}

function handleLogout() {
  authStore.logout()
  router.push({ name: 'Login' })
}
</script>

<template>
  <div class="header-inner">
    <div class="header-left">
      <el-button :icon="appStore.sidebarCollapsed ? Expand : Fold" text @click="toggleSidebar">
        {{ appStore.sidebarCollapsed ? '展开' : '折叠' }}
      </el-button>
      <span class="header-title">IDC 分销平台</span>
    </div>
    <div class="header-right">
      <el-dropdown trigger="click">
        <span class="user-info">
          <el-icon><UserFilled /></el-icon>
          {{ authStore.user?.username ?? '未登录' }}
        </span>
        <template #dropdown>
          <el-dropdown-menu>
            <el-dropdown-item @click="handleLogout">退出登录</el-dropdown-item>
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
  gap: 12px;
}

.header-title {
  font-size: 16px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.header-right {
  display: flex;
  align-items: center;
}

.user-info {
  display: flex;
  align-items: center;
  gap: 6px;
  cursor: pointer;
  color: var(--el-text-color-regular);
}
</style>
