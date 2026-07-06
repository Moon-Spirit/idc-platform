<script setup lang="ts">
import { ref, watch, type PropType } from 'vue'
import type { DistributorTreeNode } from '@/api/distributor'
import { FolderOpened, Document } from '@element-plus/icons-vue'

const props = defineProps({
  data: {
    type: Object as PropType<DistributorTreeNode | null>,
    default: null,
  },
  loading: {
    type: Boolean,
    default: false,
  },
})

const emit = defineEmits<{
  (e: 'node-click', node: DistributorTreeNode): void
}>()

const treeRef = ref()

// Map server tree to el-tree format (already compatible with {id, label, children})
const treeData = ref<DistributorTreeNode[]>([])

watch(
  () => props.data,
  (val) => {
    treeData.value = val ? [val] : []
  },
  { immediate: true },
)

function handleNodeClick(node: DistributorTreeNode) {
  emit('node-click', node)
}

function getStatusTag(status: number): string {
  if (status === 1) return 'success'
  if (status === 0) return 'info'
  return 'danger'
}

function getStatusLabel(status: number): string {
  if (status === 1) return '启用'
  if (status === 0) return '停用'
  return '删除'
}

function getLevelLabel(level: number): string {
  const labels: Record<number, string> = {
    1: '一级',
    2: '二级',
    3: '三级',
    4: '四级',
    5: '五级',
  }
  return labels[level] ?? `${level}级`
}
</script>

<template>
  <div class="distributor-tree">
    <el-skeleton :loading="loading" :count="5" animated>
      <template #default>
        <el-tree
          ref="treeRef"
          :data="treeData"
          :props="{ children: 'children', label: 'name' }"
          node-key="id"
          default-expand-all
          highlight-current
          @node-click="handleNodeClick"
        >
          <template #default="{ data }">
            <span class="custom-tree-node">
              <el-icon v-if="data.children && data.children.length > 0" class="tree-icon">
                <FolderOpened />
              </el-icon>
              <el-icon v-else class="tree-icon">
                <Document />
              </el-icon>
              <span class="node-name">{{ data.name }}</span>
              <el-tag :type="getStatusTag(data.status)" size="small" class="node-tag">
                {{ getStatusLabel(data.status) }}
              </el-tag>
              <el-tag type="warning" size="small" effect="plain" class="node-tag">
                {{ getLevelLabel(data.level) }}
              </el-tag>
            </span>
          </template>
        </el-tree>
      </template>
    </el-skeleton>
  </div>
</template>

<style scoped>
.custom-tree-node {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 14px;
  padding: 2px 0;
}

.tree-icon {
  color: var(--el-color-warning);
  flex-shrink: 0;
}

.node-name {
  font-weight: 500;
  margin-right: 4px;
}

.node-tag {
  margin-left: 4px;
}
</style>
