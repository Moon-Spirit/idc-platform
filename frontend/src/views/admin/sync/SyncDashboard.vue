<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import {
  Refresh,
  Connection,
  WarningFilled,
  SuccessFilled,
  CircleCloseFilled,
  VideoPause,
} from '@element-plus/icons-vue'
import type { SyncConnection } from '@/api/sync'
import { getSyncStatus, triggerProductSync, triggerInventorySync } from '@/api/sync'
import { ElMessage, ElMessageBox } from 'element-plus'

const router = useRouter()

// ── State ──────────────────────────────────────────────────────────────────

const loading = ref(false)
const data = ref<{
  connections: SyncConnection[]
  total_connections: number
  mock_mode: boolean
}>({
  connections: [],
  total_connections: 0,
  mock_mode: false,
})

const syncingProduct = ref<Set<number>>(new Set())
const syncingInventory = ref<Set<number>>(new Set())

// ── Derived stats ──────────────────────────────────────────────────────────

const activeConnections = computed(() =>
  data.value.connections.filter((c) => c.status === 1),
)

const errorConnections = computed(() =>
  data.value.connections.filter(
    (c) => c.sync_status === 'error' && c.status === 1,
  ),
)

const totalPendingItems = computed(() =>
  data.value.connections.reduce((sum, c) => sum + (c.pending || 0), 0),
)

// ── Methods ────────────────────────────────────────────────────────────────

async function fetchStatus() {
  loading.value = true
  try {
    data.value = await getSyncStatus()
  } catch {
    // handled by interceptor
  } finally {
    loading.value = false
  }
}

async function handleTriggerProduct(conn: SyncConnection) {
  try {
    await ElMessageBox.confirm(
      `确定要触发「${conn.name}」的产品同步吗？`,
      '确认操作',
      { confirmButtonText: '确认', cancelButtonText: '取消', type: 'warning' },
    )
  } catch {
    return
  }

  syncingProduct.value.add(conn.id)
  try {
    const result = await triggerProductSync(conn.id)
    if (result.success) {
      ElMessage.success(
        `产品同步成功: ${result.items_synced ?? 0} 项 (新建 ${result.items_created ?? 0})`,
      )
    } else {
      ElMessage.error(result.error ?? '同步失败')
    }
    await fetchStatus()
  } catch {
    // handled by interceptor
  } finally {
    syncingProduct.value.delete(conn.id)
  }
}

async function handleTriggerInventory(conn: SyncConnection) {
  try {
    await ElMessageBox.confirm(
      `确定要触发「${conn.name}」的库存同步吗？`,
      '确认操作',
      { confirmButtonText: '确认', cancelButtonText: '取消', type: 'warning' },
    )
  } catch {
    return
  }

  syncingInventory.value.add(conn.id)
  try {
    const result = await triggerInventorySync(conn.id)
    if (result.success) {
      ElMessage.success(
        `库存同步成功: ${result.items_synced ?? 0} 项 (错误 ${result.items_error ?? 0})`,
      )
    } else {
      ElMessage.error(result.error ?? '同步失败')
    }
    await fetchStatus()
  } catch {
    // handled by interceptor
  } finally {
    syncingInventory.value.delete(conn.id)
  }
}

function handleViewLogs() {
  router.push({ name: 'SyncLogs' })
}

function connectionStatusType(conn: SyncConnection): string {
  if (conn.status === 0) return 'info'
  if (conn.sync_status === 'error') return 'danger'
  if (conn.sync_status === 'syncing') return 'warning'
  return 'success'
}

function connectionStatusLabel(conn: SyncConnection): string {
  if (conn.status === 0) return '已停用'
  if (conn.sync_status === 'error') return '异常'
  if (conn.sync_status === 'syncing') return '同步中'
  return '正常'
}

function connectionStatusIcon(conn: SyncConnection): string {
  if (conn.status === 0) return 'VideoPause'
  if (conn.sync_status === 'error') return 'CircleCloseFilled'
  if (conn.sync_status === 'syncing') return 'WarningFilled'
  return 'SuccessFilled'
}

function typeLabel(type: string): string {
  if (type === 'v10') return 'V10'
  if (type === 'finance') return 'Finance'
  return type
}

function directionLabel(dir: string): string {
  if (dir === 'upstream') return '上游'
  if (dir === 'downstream') return '下游'
  return dir
}

function formatTime(t: string): string {
  if (!t) return '—'
  return t
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchStatus()
})
</script>

<template>
  <div class="sync-dashboard">
    <!-- Header -->
    <div class="page-header">
      <h2>ZJMF 同步管理</h2>
      <div class="header-actions">
        <el-button :icon="Refresh" :loading="loading" @click="fetchStatus">
          刷新状态
        </el-button>
        <el-button type="primary" @click="handleViewLogs">
          查看同步日志
        </el-button>
      </div>
    </div>

    <!-- Stats cards -->
    <el-row :gutter="16" class="stats-row">
      <el-col :span="6">
        <el-card shadow="never" class="stat-card">
          <div class="stat-value">{{ data.total_connections }}</div>
          <div class="stat-label">总连接数</div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="never" class="stat-card stat-success">
          <div class="stat-value">{{ activeConnections.length }}</div>
          <div class="stat-label">活跃连接</div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="never" class="stat-card stat-danger">
          <div class="stat-value">{{ errorConnections.length }}</div>
          <div class="stat-label">异常连接</div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="never" class="stat-card stat-warning">
          <div class="stat-value">{{ totalPendingItems }}</div>
          <div class="stat-label">待处理项</div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Mock mode badge -->
    <el-alert
      v-if="data.mock_mode"
      title="当前处于模拟模式 — 数据为示例数据"
      type="warning"
      show-icon
      :closable="false"
      class="mock-alert"
    />

    <!-- Connection cards -->
    <el-row :gutter="16">
      <el-col
        v-for="conn in data.connections"
        :key="conn.id"
        :xs="24"
        :sm="12"
        :lg="8"
        class="connection-card-col"
      >
        <el-card shadow="never" class="connection-card">
          <!-- Header -->
          <div class="conn-header">
            <div class="conn-title-row">
              <el-icon class="conn-icon"><Connection /></el-icon>
              <span class="conn-name">{{ conn.name }}</span>
              <el-tag
                :type="connectionStatusType(conn)"
                size="small"
                effect="dark"
              >
                <template #default>
                  <el-icon style="margin-right: 4px; vertical-align: middle">
                    <component :is="connectionStatusIcon(conn)" />
                  </el-icon>
                  {{ connectionStatusLabel(conn) }}
                </template>
              </el-tag>
            </div>
            <div class="conn-subtitle">
              <el-tag size="small" effect="plain" type="info">{{ typeLabel(conn.type) }}</el-tag>
              <el-tag size="small" effect="plain" type="info">{{ directionLabel(conn.direction) }}</el-tag>
              <span class="conn-url">{{ conn.api_base_url }}</span>
            </div>
          </div>

          <!-- Sync info -->
          <div class="conn-body">
            <div class="info-row">
              <span class="info-label">上次同步</span>
              <span class="info-value">{{ formatTime(conn.last_sync_at) }}</span>
            </div>
            <div class="info-row" v-if="conn.last_error">
              <span class="info-label">错误信息</span>
              <span class="info-value info-error">{{ conn.last_error }}</span>
            </div>
            <div class="info-row">
              <span class="info-label">待处理</span>
              <span class="info-value">{{ conn.pending }} 项</span>
            </div>
            <div class="info-row">
              <span class="info-label">同步间隔</span>
              <span class="info-value">{{ conn.sync_interval }}s</span>
            </div>
          </div>

          <!-- Actions -->
          <div class="conn-actions" v-if="conn.status === 1">
            <el-button
              size="small"
              type="primary"
              plain
              :loading="syncingProduct.has(conn.id)"
              :disabled="syncingInventory.has(conn.id)"
              @click="handleTriggerProduct(conn)"
            >
              同步产品
            </el-button>
            <el-button
              size="small"
              type="success"
              plain
              :loading="syncingInventory.has(conn.id)"
              :disabled="syncingProduct.has(conn.id)"
              @click="handleTriggerInventory(conn)"
            >
              同步库存
            </el-button>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Empty state -->
    <el-empty
      v-if="!loading && data.connections.length === 0"
      description="暂无同步连接"
    />
  </div>
</template>

<style scoped>
.sync-dashboard {
  padding-bottom: 24px;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.page-header h2 {
  margin: 0;
  font-size: 20px;
  font-weight: 600;
}

.header-actions {
  display: flex;
  gap: 8px;
}

/* Stats */
.stats-row {
  margin-bottom: 20px;
}

.stat-card {
  text-align: center;
  cursor: default;
}

.stat-value {
  font-size: 32px;
  font-weight: 700;
  line-height: 1.2;
}

.stat-label {
  font-size: 13px;
  color: #909399;
  margin-top: 4px;
}

.stat-success .stat-value {
  color: #67c23a;
}

.stat-danger .stat-value {
  color: #f56c6c;
}

.stat-warning .stat-value {
  color: #e6a23c;
}

/* Mock alert */
.mock-alert {
  margin-bottom: 16px;
}

/* Connection cards */
.connection-card-col {
  margin-bottom: 16px;
}

.connection-card {
  height: 100%;
}

.conn-header {
  margin-bottom: 16px;
  padding-bottom: 12px;
  border-bottom: 1px solid #ebeef5;
}

.conn-title-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.conn-icon {
  font-size: 18px;
  color: #409eff;
}

.conn-name {
  font-size: 15px;
  font-weight: 600;
  flex: 1;
}

.conn-subtitle {
  display: flex;
  align-items: center;
  gap: 6px;
}

.conn-url {
  font-size: 12px;
  color: #909399;
  margin-left: 4px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.conn-body {
  margin-bottom: 12px;
}

.info-row {
  display: flex;
  justify-content: space-between;
  padding: 4px 0;
  font-size: 13px;
}

.info-label {
  color: #909399;
}

.info-value {
  font-weight: 500;
}

.info-error {
  color: #f56c6c;
  max-width: 200px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.conn-actions {
  display: flex;
  gap: 8px;
  padding-top: 8px;
  border-top: 1px solid #ebeef5;
}
</style>
