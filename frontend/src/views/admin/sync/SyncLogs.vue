<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { Refresh, Search, ArrowLeft } from '@element-plus/icons-vue'
import type { SyncLogItem, SyncConnection } from '@/api/sync'
import { getSyncLogs, getSyncStatus } from '@/api/sync'

const router = useRouter()

// ── State ──────────────────────────────────────────────────────────────────

const loading = ref(false)
const logs = ref<SyncLogItem[]>([])
const total = ref(0)
const connections = ref<SyncConnection[]>([])

const query = reactive({
  connection_id: undefined as number | undefined,
  entity_type: '',
  status: '',
  direction: '',
  date_range: null as [string, string] | null,
  page: 1,
  per_page: 20,
})

// ── Data ───────────────────────────────────────────────────────────────────

async function fetchConnections() {
  try {
    const data = await getSyncStatus()
    connections.value = data.connections
  } catch {
    // non-critical
  }
}

async function fetchLogs() {
  loading.value = true
  try {
    const params: Record<string, unknown> = {
      page: query.page,
      per_page: query.per_page,
    }
    if (query.connection_id) params.connection_id = query.connection_id
    if (query.entity_type) params.entity_type = query.entity_type
    if (query.status) params.status = query.status
    if (query.direction) params.direction = query.direction
    if (query.date_range) {
      params.date_from = query.date_range[0]
      params.date_to = query.date_range[1]
    }

    const res = await getSyncLogs(params)
    logs.value = res.items
    total.value = res.total
  } catch {
    // handled by interceptor
  } finally {
    loading.value = false
  }
}

// ── Entity type options (derived from data) ────────────────────────────────

const entityTypes = ['product', 'inventory', 'client', 'invoice', 'webhook']

// ── Handlers ───────────────────────────────────────────────────────────────

function handleSearch() {
  query.page = 1
  fetchLogs()
}

function handleReset() {
  query.connection_id = undefined
  query.entity_type = ''
  query.status = ''
  query.direction = ''
  query.date_range = null
  query.page = 1
  fetchLogs()
}

function handlePageChange(page: number) {
  query.page = page
  fetchLogs()
}

function handleSizeChange(size: number) {
  query.per_page = size
  query.page = 1
  fetchLogs()
}

function handleBack() {
  router.push({ name: 'SyncDashboard' })
}

// ── Formatting helpers ─────────────────────────────────────────────────────

function statusType(status: string): 'success' | 'danger' | 'warning' | 'info' {
  if (status === 'success') return 'success'
  if (status === 'failed') return 'danger'
  if (status === 'pending') return 'warning'
  return 'info'
}

function statusLabel(status: string): string {
  const map: Record<string, string> = {
    success: '成功',
    failed: '失败',
    pending: '待处理',
  }
  return map[status] ?? status
}

function directionLabel(dir: string): string {
  const map: Record<string, string> = {
    inbound: '入站',
    outbound: '出站',
  }
  return map[dir] ?? dir
}

function formatJson(str: string): string {
  if (!str) return '—'
  try {
    const parsed = JSON.parse(str)
    return JSON.stringify(parsed, null, 2)
  } catch {
    return str
  }
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchConnections()
  fetchLogs()
})
</script>

<template>
  <div class="sync-logs">
    <!-- Header -->
    <div class="page-header">
      <div class="header-left">
        <el-button text :icon="ArrowLeft" @click="handleBack">
          返回仪表盘
        </el-button>
        <h2>同步日志</h2>
      </div>
      <el-button :icon="Refresh" :loading="loading" @click="fetchLogs">
        刷新
      </el-button>
    </div>

    <!-- Filters -->
    <el-card shadow="never" class="filter-card">
      <el-form :model="query" inline>
        <el-form-item label="连接">
          <el-select
            v-model="query.connection_id"
            placeholder="全部"
            clearable
            style="width: 180px"
          >
            <el-option
              v-for="conn in connections"
              :key="conn.id"
              :value="conn.id"
              :label="conn.name"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="状态">
          <el-select
            v-model="query.status"
            placeholder="全部"
            clearable
            style="width: 120px"
          >
            <el-option value="success" label="成功" />
            <el-option value="failed" label="失败" />
            <el-option value="pending" label="待处理" />
          </el-select>
        </el-form-item>
        <el-form-item label="实体类型">
          <el-select
            v-model="query.entity_type"
            placeholder="全部"
            clearable
            style="width: 140px"
          >
            <el-option
              v-for="t in entityTypes"
              :key="t"
              :value="t"
              :label="t"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="方向">
          <el-select
            v-model="query.direction"
            placeholder="全部"
            clearable
            style="width: 120px"
          >
            <el-option value="inbound" label="入站" />
            <el-option value="outbound" label="出站" />
          </el-select>
        </el-form-item>
        <el-form-item label="时间范围">
          <el-date-picker
            v-model="query.date_range"
            type="datetimerange"
            range-separator="至"
            start-placeholder="开始时间"
            end-placeholder="结束时间"
            value-format="YYYY-MM-DD HH:mm:ss"
            style="width: 340px"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :icon="Search" @click="handleSearch">
            查询
          </el-button>
          <el-button :icon="Refresh" @click="handleReset">
            重置
          </el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <!-- Log table -->
    <el-card shadow="never" class="table-card">
      <el-table
        :data="logs"
        v-loading="loading"
        stripe
        style="width: 100%"
        :row-key="(row: SyncLogItem) => row.id"
      >
        <el-table-column type="expand" width="50">
          <template #default="{ row }: { row: SyncLogItem }">
            <div class="log-detail">
              <div class="detail-section">
                <div class="detail-title">请求数据</div>
                <pre class="detail-json">{{ formatJson(row.request_data) }}</pre>
              </div>
              <div class="detail-section">
                <div class="detail-title">响应数据</div>
                <pre class="detail-json">{{ formatJson(row.response_data) }}</pre>
              </div>
              <div v-if="row.error_message" class="detail-section">
                <div class="detail-title" style="color: #f56c6c">错误信息</div>
                <pre class="detail-json detail-error">{{ row.error_message }}</pre>
              </div>
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="id" label="ID" width="70" />
        <el-table-column prop="connection_name" label="连接" min-width="140" />
        <el-table-column prop="direction" label="方向" width="80">
          <template #default="{ row }: { row: SyncLogItem }">
            <span>{{ directionLabel(row.direction) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="entity_type" label="实体类型" width="100" />
        <el-table-column prop="action" label="操作" width="100" />
        <el-table-column prop="status" label="状态" width="90">
          <template #default="{ row }: { row: SyncLogItem }">
            <el-tag :type="statusType(row.status)" size="small" effect="dark">
              {{ statusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="retry_count" label="重试" width="70" align="center" />
        <el-table-column prop="created_at" label="时间" width="180" />
      </el-table>

      <!-- Pagination -->
      <div class="pagination-wrapper">
        <el-pagination
          v-model:current-page="query.page"
          v-model:page-size="query.per_page"
          :page-sizes="[10, 20, 50, 100]"
          :total="total"
          layout="total, sizes, prev, pager, next, jumper"
          @size-change="handleSizeChange"
          @current-change="handlePageChange"
        />
      </div>
    </el-card>

    <!-- Empty state -->
    <el-empty
      v-if="!loading && logs.length === 0"
      description="暂无同步日志"
    />
  </div>
</template>

<style scoped>
.sync-logs {
  padding-bottom: 24px;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.header-left h2 {
  margin: 0;
  font-size: 20px;
  font-weight: 600;
}

.filter-card {
  margin-bottom: 16px;
}

.table-card {
  margin-bottom: 16px;
}

.pagination-wrapper {
  margin-top: 20px;
  display: flex;
  justify-content: flex-end;
}

/* Expandable detail */
.log-detail {
  padding: 16px 24px;
  background: #fafafa;
}

.detail-section {
  margin-bottom: 16px;
}

.detail-section:last-child {
  margin-bottom: 0;
}

.detail-title {
  font-size: 13px;
  font-weight: 600;
  color: #606266;
  margin-bottom: 8px;
}

.detail-json {
  margin: 0;
  padding: 12px;
  background: #fff;
  border: 1px solid #e4e7ed;
  border-radius: 4px;
  font-size: 12px;
  font-family: 'Menlo', 'Monaco', 'Courier New', monospace;
  line-height: 1.6;
  max-height: 300px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-all;
}

.detail-error {
  color: #f56c6c;
  border-color: #fbc4c4;
  background: #fef0f0;
}
</style>
