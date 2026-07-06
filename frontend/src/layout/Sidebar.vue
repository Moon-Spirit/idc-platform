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
    background-color="#304156"
    text-color="#bfcbd9"
    active-text-color="#409eff"
    @select="handleSelect"
  >
    <template v-for="item in appStore.menuRoutes" :key="item.path">
      <el-menu-item :index="item.path">
        <el-icon v-if="item.meta?.icon">
          <component :is="item.meta.icon" />
        </el-icon>
        <template #title>
          <span>{{ item.meta?.title }}</span>
        </template>
      </el-menu-item>
    </template>
  </el-menu>
</template>

<style scoped>
.el-menu {
  border-right: none;
}
</style>
