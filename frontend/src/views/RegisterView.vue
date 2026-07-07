<script setup lang="ts">
import { reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import { register } from '@/api/auth'
import type { FormInstance, FormRules } from 'element-plus'

const router = useRouter()

const formRef = ref<FormInstance>()
const registerError = ref('')
const registerSuccess = ref(false)

const form = reactive({
  username: '',
  password: '',
  confirmPassword: '',
  email: '',
  phone: '',
  company_name: '',
})

const rules: FormRules = {
  username: [
    { required: true, message: '请输入用户名', trigger: 'blur' },
    { min: 2, message: '用户名至少2个字符', trigger: 'blur' },
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 6, message: '密码至少6个字符', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认密码', trigger: 'blur' },
    {
      validator: (_rule, value, callback) => {
        if (value !== form.password) {
          callback(new Error('两次输入的密码不一致'))
        } else {
          callback()
        }
      },
      trigger: 'blur',
    },
  ],
  email: [
    { required: true, message: '请输入邮箱', trigger: 'blur' },
    { type: 'email', message: '请输入正确的邮箱地址', trigger: 'blur' },
  ],
  phone: [
    { required: true, message: '请输入手机号', trigger: 'blur' },
  ],
  company_name: [
    { required: true, message: '请输入公司名称', trigger: 'blur' },
  ],
}

const loading = ref(false)

async function handleRegister() {
  const valid = await formRef.value?.validate().catch(() => false)
  if (!valid) return

  loading.value = true
  registerError.value = ''
  try {
    await register({
      username: form.username,
      password: form.password,
      email: form.email,
      phone: form.phone,
      company_name: form.company_name,
    })
    registerSuccess.value = true
  } catch (e) {
    registerError.value = (e as Error).message || '注册失败，请重试'
  } finally {
    loading.value = false
  }
}

function goToLogin() {
  router.push('/login')
}
</script>

<template>
  <div class="auth-container">
    <div class="auth-card auth-card--wide">
      <div class="auth-logo">
        <h1>经销商注册</h1>
        <p class="auth-subtitle">创建您的经销商账户</p>
      </div>

      <el-alert
        v-if="registerError"
        :title="registerError"
        type="error"
        show-icon
        closable
        class="auth-alert"
        @close="registerError = ''"
      />

      <el-alert
        v-if="registerSuccess"
        title="注册申请已提交"
        type="success"
        show-icon
        :closable="false"
        class="auth-alert"
      >
        <template #default>
          <p>您的账户创建申请已提交，审核通过后您将收到通知。</p>
          <el-button type="primary" @click="goToLogin" class="mt-8">返回登录</el-button>
        </template>
      </el-alert>

      <el-form
        v-if="!registerSuccess"
        ref="formRef"
        :model="form"
        :rules="rules"
        label-position="top"
        @keyup.enter="handleRegister"
      >
        <el-form-item label="用户名" prop="username">
          <el-input v-model="form.username" placeholder="请输入用户名" />
        </el-form-item>
        <el-form-item label="密码" prop="password">
          <el-input v-model="form.password" type="password" placeholder="请输入密码" show-password />
        </el-form-item>
        <el-form-item label="确认密码" prop="confirmPassword">
          <el-input v-model="form.confirmPassword" type="password" placeholder="请再次输入密码" show-password />
        </el-form-item>
        <el-form-item label="邮箱" prop="email">
          <el-input v-model="form.email" placeholder="请输入邮箱" />
        </el-form-item>
        <el-form-item label="手机号" prop="phone">
          <el-input v-model="form.phone" placeholder="请输入手机号" />
        </el-form-item>
        <el-form-item label="公司名称" prop="company_name">
          <el-input v-model="form.company_name" placeholder="请输入公司名称" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="loading" style="width: 100%" size="large" @click="handleRegister">
            {{ loading ? '提交中...' : '提交注册' }}
          </el-button>
        </el-form-item>
      </el-form>

      <div v-if="!registerSuccess" class="auth-footer">
        已有账号？<router-link to="/login">立即登录</router-link>
      </div>
    </div>
  </div>
</template>
