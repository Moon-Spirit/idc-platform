<script setup lang="ts">
import { useAppStore } from '@/stores/app'
import Sidebar from './Sidebar.vue'
import Header from './Header.vue'

const appStore = useAppStore()
</script>

<template>
  <el-container class="layout-container">
    <el-aside :width="appStore.sidebarCollapsed ? '64px' : '220px'" class="layout-aside">
      <div class="sidebar-logo" :class="{ 'is-collapsed': appStore.sidebarCollapsed }">
        <span class="logo-text" v-show="!appStore.sidebarCollapsed">IDC 管理平台</span>
        <span class="logo-icon" v-show="appStore.sidebarCollapsed">IDC</span>
      </div>
      <Sidebar />
    </el-aside>
    <el-container class="layout-right">
      <el-header class="layout-header">
        <Header />
      </el-header>
      <el-main class="layout-main">
        <div class="main-content">
          <router-view />
        </div>
      </el-main>
    </el-container>
  </el-container>
</template>

<style scoped>
.layout-container {
  height: 100vh;
  width: 100%;
}

.layout-aside {
  background-color: var(--sidebar-bg);
  transition: width 0.3s cubic-bezier(0.25, 0.8, 0.25, 1);
  overflow: hidden;
  display: flex;
  flex-direction: column;
  border-right: none;
  z-index: 10;
}

.sidebar-logo {
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--sidebar-bg-dark);
  border-bottom: 1px solid rgba(255, 255, 255, 0.06);
  flex-shrink: 0;
}

.logo-text {
  font-size: 17px;
  font-weight: 700;
  color: #ffffff;
  letter-spacing: 0.5px;
  white-space: nowrap;
}

.logo-icon {
  font-size: 15px;
  font-weight: 800;
  color: var(--sidebar-item-active);
  letter-spacing: 0.5px;
}

.sidebar-logo.is-collapsed {
  padding: 0;
}

.layout-right {
  display: flex;
  flex-direction: column;
  height: 100vh;
  overflow: hidden;
}

.layout-header {
  background-color: var(--header-bg);
  border-bottom: 1px solid var(--header-border);
  display: flex;
  align-items: center;
  padding: 0 24px;
  height: var(--header-height) !important;
  flex-shrink: 0;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.03);
}

.layout-main {
  background-color: var(--main-bg);
  padding: 20px;
  overflow-y: auto;
  flex: 1;
}

.main-content {
  max-width: 1400px;
  margin: 0 auto;
  width: 100%;
}
</style>
