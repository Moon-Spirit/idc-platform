<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { Search, Refresh, Plus, Edit, Share } from '@element-plus/icons-vue'
import type { Distributor } from '@/api/distributor'
import { listDistributors } from '@/api/distributor'
import DistributorForm from './DistributorForm.vue'

const router = useRouter()

// ── State ──────────────────────────────────────────────────────────────────

const loading = ref(false)
const distributors = ref<Distributor[]>([])
const total = ref(0)

const query = reactive({
  page: 1,
  per_page: 20,
  status: undefined as number | undefined | null,
  level: undefined as number | undefined,
  keyword: '',
})

const formDialogVisible = ref(false)
const editingId = ref<number>(0)

// ── Methods ───────────────────────────────────────────────────────────────

async function fetchData() {
  loading.value = true
  try {
    const params: Record<string, unknown> = {
      page: query.page,
      per_page: query.per_page,
    }
    if (query.status !== undefined && query.status !== null && query.status >= 0) params.status = query.status
    if (query.level !== undefined && query.level > 0) params.level = query.level
    if (query.keyword.trim()) params.keyword = query.keyword.trim()

    const res = await listDistributors(params)
    distributors.value = res.items
    total.value = res.total
  } catch {
    // error handled by interceptor
  } finally {
    loading.value = false
  }
}

function handlePageChange(page: number) {
  query.page = page
  fetchData()
}

function handleSizeChange(size: number) {
  query.per_page = size
  query.page = 1
  fetchData()
}

function handleSearch() {
  query.page = 1
  fetchData()
}

function handleReset() {
  query.status = undefined
  query.level = undefined
  query.keyword = ''
  query.page = 1
  fetchData()
}

function handleCreate() {
  editingId.value = 0
  formDialogVisible.value = true
}

function handleEdit(id: number) {
  editingId.value = id
  formDialogVisible.value = true
}

function handleViewDetail(id: number) {
  router.push({ name: 'DistributorDetail', params: { id } })
}

function handleViewTree(id: number) {
  router.push({ name: 'DistributorDetail', params: { id }, query: { tab: 'tree' } })
}

function handleFormSuccess() {
  formDialogVisible.value = false
  fetchData()
}

function getStatusType(status: number): string {
  if (status === 1) return 'success'
  if (status === 0) return 'danger'
  return 'info'
}

function getStatusLabel(status: number): string {
  if (status === 1) return '启用'
  if (status === 0) return '停用'
  return '删除'
}

function getLevelLabel(level: number): string {
  const labels: Record<number, string> = {
    1: '一级经销商',
    2: '二级经销商',
    3: '三级经销商',
    4: '四级经销商',
    5: '五级经销商',
  }
  return labels[level] ?? `${level}级`
}

// ── Init ───────────────────────────────────────────────────────────────────

onMounted(() => {
  fetchData()
})
</script>

<template>
  <div class="distributor-list">
    <!-- Search bar -->
    <el-card class="search-card" shadow="never">
      <el-form :model="query" inline>
        <el-form-item label="关键词">
          <el-input
            v-model="query.keyword"
            placeholder="名称/手机/邮箱"
            clearable
            style="width: 200px"
            @keyup.enter="handleSearch"
          />
        </el-form-item>
        <el-form-item label="状态">
          <el-select v-model="query.status" placeholder="全部" clearable style="width: 120px">
            <el-option :value="1" label="启用" />
            <el-option :value="0" label="停用" />
          </el-select>
        </el-form-item>
        <el-form-item label="等级">
          <el-select v-model="query.level" placeholder="全部" clearable style="width: 140px">
            <el-option :value="1" label="一级" />
            <el-option :value="2" label="二级" />
            <el-option :value="3" label="三级" />
            <el-option :value="4" label="四级" />
            <el-option :value="5" label="五级" />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :icon="Search" @click="handleSearch">搜索</el-button>
          <el-button :icon="Refresh" @click="handleReset">重置</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <!-- Toolbar -->
    <el-card class="table-card" shadow="never">
      <div class="toolbar">
        <el-button type="primary" :icon="Plus" @click="handleCreate">新增经销商</el-button>
      </div>

      <!-- Table -->
      <el-table
        :data="distributors"
        v-loading="loading"
        stripe
        style="width: 100%"
        @row-dblclick="(row: Distributor) => handleViewDetail(row.id)"
      >
        <el-table-column prop="id" label="ID" width="70" />
        <el-table-column prop="name" label="名称" min-width="160">
          <template #default="{ row }: { row: Distributor }">
            <el-link type="primary" @click="handleViewDetail(row.id)">{{ row.name }}</el-link>
          </template>
        </el-table-column>
        <el-table-column prop="level" label="等级" width="110">
          <template #default="{ row }: { row: Distributor }">
            <el-tag type="warning" effect="plain" size="small">
              {{ getLevelLabel(row.level) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="contact_person" label="联系人" width="120" />
        <el-table-column prop="contact_phone" label="手机号" width="130" />
        <el-table-column prop="balance" label="余额" width="120" align="right" />
        <el-table-column prop="credit_limit" label="信用额度" width="120" align="right" />
        <el-table-column prop="children_count" label="下级" width="70" align="center" />
        <el-table-column prop="order_count" label="订单" width="70" align="center" />
        <el-table-column prop="status" label="状态" width="80">
          <template #default="{ row }: { row: Distributor }">
            <el-tag :type="getStatusType(row.status)" size="small">
              {{ getStatusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="160" fixed="right">
          <template #default="{ row }: { row: Distributor }">
            <el-button text type="primary" size="small" :icon="Edit" @click="handleEdit(row.id)">
              编辑
            </el-button>
            <el-button text type="primary" size="small" :icon="Share" @click="handleViewTree(row.id)">
              组织树
            </el-button>
          </template>
        </el-table-column>
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

    <!-- Create / Edit Dialog -->
    <DistributorForm
      v-if="formDialogVisible"
      :distributor-id="editingId"
      :visible="formDialogVisible"
      @success="handleFormSuccess"
      @close="formDialogVisible = false"
    />
  </div>
</template>

<style scoped>
.search-card {
  margin-bottom: 16px;
}

.table-card {
  margin-bottom: 16px;
}

.toolbar {
  margin-bottom: 16px;
  display: flex;
  justify-content: flex-start;
}

.pagination-wrapper {
  margin-top: 20px;
  display: flex;
  justify-content: flex-end;
}
</style>
