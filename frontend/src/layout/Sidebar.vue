<script setup lang="ts">
import { computed } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAppStore } from '@/stores/app'

const router = useRouter()
const route = useRoute()
const appStore = useAppStore()

const isCollapsed = computed(() => appStore.sidebarCollapsed)

function handleSelect(index: string) {
  router.push(index)
}

const activeIndex = computed(() => route.path)
</script>

<template>
  <el-menu
    :default-active="activeIndex"
    :collapse="isCollapsed"
    :collapse-transition="false"
    background-color="transparent"
    text-color="#bfcbd9"
    active-text-color="#ffffff"
    @select="handleSelect"
    class="sidebar-menu"
  >
    <template v-for="item in appStore.menuRoutes" :key="item.path">
      <el-menu-item :index="item.path" class="sidebar-menu-item">
        <el-icon v-if="item.meta?.icon" class="menu-icon">
          <component :is="item.meta.icon" />
        </el-icon>
        <template #title>
          <span class="menu-title">{{ item.meta?.title }}</span>
        </template>
      </el-menu-item>
    </template>
  </el-menu>
</template>

<style scoped>
.sidebar-menu {
  border-right: none;
  flex: 1;
  overflow-y: auto;
  overflow-x: hidden;
  padding: 8px 0;
}

.sidebar-menu:not(.el-menu--collapse) {
  width: 220px;
}

.sidebar-menu-item {
  margin: 2px 8px;
  border-radius: 6px;
  height: 42px;
  line-height: 42px;
  padding: 0 12px !important;
  transition: background-color 0.2s;
}

.sidebar-menu-item:hover {
  background-color: var(--sidebar-item-hover) !important;
}

.sidebar-menu-item.is-active {
  background-color: var(--sidebar-item-active) !important;
}

.sidebar-menu-item.is-active .menu-icon {
  color: #ffffff;
}

.sidebar-menu-item.is-active .menu-title {
  color: #ffffff;
  font-weight: 600;
}

.menu-icon {
  font-size: 18px;
  margin-right: 8px;
  color: #bfcbd9;
}

.menu-title {
  font-size: 14px;
}

/* Collapsed state */
.el-menu--collapse .sidebar-menu-item {
  margin: 2px 6px;
  padding: 0 8px !important;
  width: calc(100% - 12px);
  display: flex;
  align-items: center;
  justify-content: center;
}

.el-menu--collapse .menu-icon {
  margin-right: 0;
  font-size: 20px;
}
</style>
