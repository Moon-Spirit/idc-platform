<script setup lang="ts">
import { useAuthStore } from '@/stores/auth'

const authStore = useAuthStore()
</script>

<template>
  <div class="dashboard">
    <el-card>
      <template #header>
        <span>仪表盘</span>
      </template>
      <p>欢迎回来，{{ authStore.user?.username ?? '用户' }}</p>
    </el-card>
  </div>
</template>
