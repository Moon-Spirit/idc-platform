<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { ElMessage } from 'element-plus'
import type { FormInstance } from 'element-plus'
import type { DistributorFormData } from '@/api/distributor'
import { createDistributor, updateDistributor, getDistributor, listDistributors } from '@/api/distributor'

const props = defineProps({
  distributorId: {
    type: Number,
    default: null,
  },
  visible: {
    type: Boolean,
    default: false,
  },
})

const emit = defineEmits<{
  (e: 'success'): void
  (e: 'close'): void
}>()

const formRef = ref<FormInstance>()
const loading = ref(false)
const parentOptions = ref<Array<{ id: number; name: string }>>([])
const isEdit = computed(() => props.distributorId !== null && props.distributorId > 0)

const form: DistributorFormData = reactive({
  name: '',
  level: 1,
  parent_id: null,
  price_template_id: null,
  contact_person: '',
  contact_phone: '',
  contact_email: '',
  credit_limit: '0.00',
  remark: '',
  status: 1,
})

const rules = {
  name: [{ required: true, message: '请输入经销商名称', trigger: 'blur' }],
  level: [{ required: true, message: '请选择等级', trigger: 'change' }],
}

// ── Methods ──────────────────────────────────────────────────────────────

async function loadParentOptions() {
  try {
    const res = await listDistributors({ page: 1, per_page: 200, status: 1 })
    parentOptions.value = res.items.map((d) => ({ id: d.id, name: d.name }))
  } catch {
    parentOptions.value = []
  }
}

async function loadDistributor() {
  if (!props.distributorId) return
  loading.value = true
  try {
    const data = await getDistributor(props.distributorId)
    form.name = data.name
    form.level = data.level
    form.parent_id = data.parent_id
    form.price_template_id = data.price_template_id
    form.contact_person = data.contact_person
    form.contact_phone = data.contact_phone
    form.contact_email = data.contact_email
    form.credit_limit = data.credit_limit
    form.remark = data.remark
    form.status = data.status
  } catch {
    ElMessage.error('加载经销商信息失败')
  } finally {
    loading.value = false
  }
}

async function handleSubmit() {
  const valid = await formRef.value?.validate().catch(() => false)
  if (!valid) return

  loading.value = true
  try {
    if (isEdit.value) {
      await updateDistributor(props.distributorId!, form)
      ElMessage.success('经销商信息已更新')
    } else {
      await createDistributor(form)
      ElMessage.success('经销商创建成功')
    }
    emit('success')
  } catch (e: unknown) {
    const msg = (e as { response?: { data?: { message?: string } } })?.response?.data?.message
    ElMessage.error(msg || '操作失败')
  } finally {
    loading.value = false
  }
}

function handleClose() {
  emit('close')
}

// ── Init ─────────────────────────────────────────────────────────────────

onMounted(() => {
  loadParentOptions()
  if (isEdit.value) {
    loadDistributor()
  }
})
</script>

<template>
  <el-dialog
    :model-value="visible"
    :title="isEdit ? '编辑经销商' : '新增经销商'"
    width="600px"
    :close-on-click-modal="false"
    @close="handleClose"
  >
    <el-form
      ref="formRef"
      :model="form"
      :rules="rules"
      label-width="110px"
      v-loading="loading"
    >
      <el-form-item label="名称" prop="name">
        <el-input v-model="form.name" placeholder="请输入经销商名称" />
      </el-form-item>

      <el-row :gutter="20">
        <el-col :span="12">
          <el-form-item label="等级" prop="level">
            <el-select v-model="form.level" style="width: 100%">
              <el-option :value="1" label="一级经销商" />
              <el-option :value="2" label="二级经销商" />
              <el-option :value="3" label="三级经销商" />
              <el-option :value="4" label="四级经销商" />
              <el-option :value="5" label="五级经销商" />
            </el-select>
          </el-form-item>
        </el-col>
        <el-col :span="12">
          <el-form-item label="上级经销商">
            <el-select
              v-model="form.parent_id"
              placeholder="无（顶级）"
              clearable
              filterable
              style="width: 100%"
            >
              <el-option
                v-for="p in parentOptions"
                :key="p.id"
                :value="p.id"
                :label="p.name"
              />
            </el-select>
          </el-form-item>
        </el-col>
      </el-row>

      <el-row :gutter="20">
        <el-col :span="12">
          <el-form-item label="联系人">
            <el-input v-model="form.contact_person" placeholder="联系人姓名" />
          </el-form-item>
        </el-col>
        <el-col :span="12">
          <el-form-item label="手机号">
            <el-input v-model="form.contact_phone" placeholder="手机号" />
          </el-form-item>
        </el-col>
      </el-row>

      <el-form-item label="邮箱">
        <el-input v-model="form.contact_email" placeholder="邮箱地址" />
      </el-form-item>

      <el-row :gutter="20">
        <el-col :span="12">
          <el-form-item label="信用额度">
            <el-input v-model="form.credit_limit" placeholder="0.00">
              <template #prefix>¥</template>
            </el-input>
          </el-form-item>
        </el-col>
        <el-col :span="12">
          <el-form-item label="状态">
            <el-select v-model="form.status" style="width: 100%">
              <el-option :value="1" label="启用" />
              <el-option :value="0" label="停用" />
            </el-select>
          </el-form-item>
        </el-col>
      </el-row>

      <el-form-item label="备注">
        <el-input
          v-model="form.remark"
          type="textarea"
          :rows="2"
          placeholder="备注信息（可选）"
        />
      </el-form-item>
    </el-form>

    <template #footer>
      <el-button @click="handleClose">取消</el-button>
      <el-button type="primary" :loading="loading" @click="handleSubmit">
        {{ isEdit ? '保存修改' : '确认创建' }}
      </el-button>
    </template>
  </el-dialog>
</template>
