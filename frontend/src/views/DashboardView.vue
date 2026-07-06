<script setup lang="ts">
import { computed } from 'vue'
import { useAuthStore } from '@/stores/auth'
import DealerDashboard from './dealer/DashboardView.vue'

const authStore = useAuthStore()

const isAdmin = computed(() => authStore.user?.role === 'admin')
const isDealer = computed(() => authStore.user?.role === 'dealer')
</script>

<template>
  <!-- Admin dashboard placeholder (not in scope — see T22) -->
  <div v-if="isAdmin" class="dashboard">
    <el-card>
      <template #header>
        <span>仪表盘</span>
      </template>
      <p>欢迎回来，{{ authStore.user?.username ?? '用户' }}</p>
    </el-card>
  </div>

  <!-- Dealer dashboard -->
  <DealerDashboard v-else-if="isDealer" />

  <!-- Fallback for unknown roles -->
  <div v-else class="dashboard">
    <el-card>
      <p>欢迎回来，{{ authStore.user?.username ?? '用户' }}</p>
    </el-card>
  </div>
</template>
