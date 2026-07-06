<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { ArrowLeft, Edit } from '@element-plus/icons-vue'
import type { DistributorDetail as DistributorDetailType, DistributorTreeNode, DistributorStats, Distributor } from '@/api/distributor'
import { getDistributor, getDistributorTree, getDistributorStatistics, getDirectChildren } from '@/api/distributor'
import DistributorTree from '@/components/distributor/DistributorTree.vue'
import DistributorForm from './DistributorForm.vue'

const route = useRoute()
const router = useRouter()

const distributorId = computed(() => Number(route.params.id))
const activeTab = ref((route.query.tab as string) || 'info')

// ── State ─────────────────────────────────────────────────────────────────

const loading = ref(true)
const distributor = ref<DistributorDetailType | null>(null)

const treeLoading = ref(false)
const treeData = ref<DistributorTreeNode | null>(null)

const statsLoading = ref(false)
const stats = ref<DistributorStats | null>(null)

const childrenLoading = ref(false)
const children = ref<Distributor[]>([])

const editDialogVisible = ref(false)

// ── Fetch ─────────────────────────────────────────────────────────────────

async function loadDistributor() {
  loading.value = true
  try {
    distributor.value = await getDistributor(distributorId.value)
  } catch {
    ElMessage.error('加载经销商信息失败')
  } finally {
    loading.value = false
  }
}

async function loadTree() {
  treeLoading.value = true
  try {
    treeData.value = await getDistributorTree(distributorId.value)
  } catch {
    ElMessage.error('加载组织树失败')
  } finally {
    treeLoading.value = false
  }
}

async function loadStats() {
  statsLoading.value = true
  try {
    stats.value = await getDistributorStatistics(distributorId.value)
  } catch {
    ElMessage.error('加载统计数据失败')
  } finally {
    statsLoading.value = false
  }
}

async function loadChildren() {
  childrenLoading.value = true
  try {
    children.value = await getDirectChildren(distributorId.value)
  } catch {
    ElMessage.error('加载下级列表失败')
  } finally {
    childrenLoading.value = false
  }
}

// ── Handlers ─────────────────────────────────────────────────────────────

function handleTabChange(tab: string) {
  if (tab === 'tree' && !treeData.value) {
    loadTree()
  } else if (tab === 'stats' && !stats.value) {
    loadStats()
  } else if (tab === 'children' && children.value.length === 0) {
    loadChildren()
  }
}

function handleEdit() {
  editDialogVisible.value = true
}

function handleEditSuccess() {
  editDialogVisible.value = false
  loadDistributor()
}

function goBack() {
  router.push({ name: 'DistributorList' })
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

// ── Init ─────────────────────────────────────────────────────────────────

onMounted(async () => {
  await loadDistributor()
  if (activeTab.value === 'tree') loadTree()
  else if (activeTab.value === 'stats') loadStats()
  else if (activeTab.value === 'children') loadChildren()
})
</script>

<template>
  <div class="distributor-detail">
    <!-- Header -->
    <div class="detail-header">
      <el-button :icon="ArrowLeft" text @click="goBack">返回列表</el-button>
      <el-button :icon="Edit" type="primary" @click="handleEdit" v-if="distributor">编辑</el-button>
    </div>

    <!-- Basic info card -->
    <el-card v-if="distributor" shadow="never" class="info-card">
      <template #header>
        <div class="info-header">
          <span class="info-title">{{ distributor.name }}</span>
          <el-tag :type="getStatusType(distributor.status)" size="small">
            {{ getStatusLabel(distributor.status) }}
          </el-tag>
          <el-tag type="warning" effect="plain" size="small" style="margin-left: 8px">
            {{ getLevelLabel(distributor.level) }}
          </el-tag>
        </div>
      </template>
      <el-descriptions :column="3" border>
        <el-descriptions-item label="ID">{{ distributor.id }}</el-descriptions-item>
        <el-descriptions-item label="上级">
          {{ distributor.parent_name ?? '（顶级）' }}
        </el-descriptions-item>
        <el-descriptions-item label="下级数">{{ distributor.children_count }}</el-descriptions-item>
        <el-descriptions-item label="联系人">{{ distributor.contact_person || '-' }}</el-descriptions-item>
        <el-descriptions-item label="手机号">{{ distributor.contact_phone || '-' }}</el-descriptions-item>
        <el-descriptions-item label="邮箱">{{ distributor.contact_email || '-' }}</el-descriptions-item>
        <el-descriptions-item label="余额">{{ distributor.balance }}</el-descriptions-item>
        <el-descriptions-item label="信用额度">{{ distributor.credit_limit }}</el-descriptions-item>
        <el-descriptions-item label="创建时间">{{ distributor.created_at }}</el-descriptions-item>
        <el-descriptions-item label="备注" :span="3">{{ distributor.remark || '-' }}</el-descriptions-item>
      </el-descriptions>

      <!-- Aggregate stats -->
      <el-row :gutter="20" class="stats-row">
        <el-col :span="6">
          <el-card shadow="never" class="stat-card">
            <div class="stat-value">{{ distributor.order_count ?? 0 }}</div>
            <div class="stat-label">总订单数</div>
          </el-card>
        </el-col>
        <el-col :span="6">
          <el-card shadow="never" class="stat-card">
            <div class="stat-value">{{ distributor.total_revenue ?? '0.00' }}</div>
            <div class="stat-label">总收入</div>
          </el-card>
        </el-col>
        <el-col :span="6">
          <el-card shadow="never" class="stat-card">
            <div class="stat-value">{{ distributor.active_subscriptions ?? 0 }}</div>
            <div class="stat-label">活跃服务</div>
          </el-card>
        </el-col>
        <el-col :span="6">
          <el-card shadow="never" class="stat-card">
            <div class="stat-value" :class="{ 'text-danger': (distributor.overdue_invoices ?? 0) > 0 }">
              {{ distributor.overdue_invoices ?? 0 }}
            </div>
            <div class="stat-label">逾期账单</div>
          </el-card>
        </el-col>
      </el-row>
    </el-card>

    <!-- Tabs -->
    <el-card shadow="never" class="tab-card">
      <el-tabs v-model="activeTab" @tab-change="handleTabChange">
        <!-- Tree tab -->
        <el-tab-pane label="组织树" name="tree" lazy>
          <DistributorTree
            :data="treeData"
            :loading="treeLoading"
            @node-click="(node: DistributorTreeNode) => router.push({ name: 'DistributorDetail', params: { id: node.id } })"
          />
        </el-tab-pane>

        <!-- Statistics tab -->
        <el-tab-pane label="月度统计" name="stats" lazy>
          <el-skeleton :loading="statsLoading" :count="4" animated>
            <template #default>
              <template v-if="stats">
                <el-row :gutter="20">
                  <el-col :span="6">
                    <el-card shadow="never" class="stat-card">
                      <div class="stat-value">{{ stats.orders_this_month }}</div>
                      <div class="stat-label">本月订单</div>
                    </el-card>
                  </el-col>
                  <el-col :span="6">
                    <el-card shadow="never" class="stat-card">
                      <div class="stat-value">{{ stats.revenue_this_month }}</div>
                      <div class="stat-label">本月收入</div>
                    </el-card>
                  </el-col>
                  <el-col :span="6">
                    <el-card shadow="never" class="stat-card">
                      <div class="stat-value">{{ stats.subscription_count }}</div>
                      <div class="stat-label">订阅数</div>
                    </el-card>
                  </el-col>
                  <el-col :span="6">
                    <el-card shadow="never" class="stat-card">
                      <div class="stat-value" :class="{ 'text-danger': stats.overdue_invoice_count > 0 }">
                        {{ stats.overdue_invoice_count }}
                      </div>
                      <div class="stat-label">逾期账单</div>
                    </el-card>
                  </el-col>
                </el-row>
                <el-divider />
                <el-descriptions :column="2" border>
                  <el-descriptions-item label="累计订单">{{ stats.total_orders }}</el-descriptions-item>
                  <el-descriptions-item label="累计收入">{{ stats.total_revenue }}</el-descriptions-item>
                </el-descriptions>
              </template>
              <el-empty v-else description="暂无统计数据" />
            </template>
          </el-skeleton>
        </el-tab-pane>

        <!-- Direct children tab -->
        <el-tab-pane label="下级列表" name="children" lazy>
          <el-skeleton :loading="childrenLoading" :count="5" animated>
            <template #default>
              <el-table :data="children" stripe v-if="children.length > 0">
                <el-table-column prop="id" label="ID" width="70" />
                <el-table-column prop="name" label="名称" min-width="160">
                  <template #default="{ row }: { row: Distributor }">
                    <el-link type="primary" @click="router.push({ name: 'DistributorDetail', params: { id: row.id } })">
                      {{ row.name }}
                    </el-link>
                  </template>
                </el-table-column>
                <el-table-column prop="level" label="等级" width="100">
                  <template #default="{ row }: { row: Distributor }">
                    <el-tag size="small" type="warning" effect="plain">{{ getLevelLabel(row.level) }}</el-tag>
                  </template>
                </el-table-column>
                <el-table-column prop="contact_person" label="联系人" width="120" />
                <el-table-column prop="contact_phone" label="手机号" width="130" />
                <el-table-column prop="status" label="状态" width="80">
                  <template #default="{ row }: { row: Distributor }">
                    <el-tag :type="getStatusType(row.status)" size="small">{{ getStatusLabel(row.status) }}</el-tag>
                  </template>
                </el-table-column>
              </el-table>
              <el-empty v-else description="暂无下级经销商" />
            </template>
          </el-skeleton>
        </el-tab-pane>
      </el-tabs>
    </el-card>

    <!-- Edit dialog -->
    <DistributorForm
      v-if="editDialogVisible"
      :distributor-id="distributorId"
      :visible="editDialogVisible"
      @success="handleEditSuccess"
      @close="editDialogVisible = false"
    />
  </div>
</template>

<style scoped>
.detail-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.info-card {
  margin-bottom: 16px;
}

.info-header {
  display: flex;
  align-items: center;
}

.info-title {
  font-size: 18px;
  font-weight: 600;
  margin-right: 12px;
}

.stats-row {
  margin-top: 20px;
}

.stat-card {
  text-align: center;
  cursor: default;
}

.stat-value {
  font-size: 24px;
  font-weight: 700;
  color: var(--el-color-primary);
}

.stat-label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  margin-top: 4px;
}

.text-danger {
  color: var(--el-color-danger);
}

.tab-card {
  min-height: 300px;
}
</style>
