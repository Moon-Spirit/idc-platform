<script setup lang="ts">
import { ref, reactive, computed, watch, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import {
  Check,
  Close,
  CopyDocument,
  Loading,
  WarningFilled,
  SuccessFilled,
} from '@element-plus/icons-vue'
import {
  checkEnv,
  testDbConnection,
  testRedisConnection,
  runInstall,
  getSetupStatus,
} from '@/api/setup'
import type {
  EnvInfo,
  DbConfig,
  RedisConfig,
  ConnectionTestResult,
  InstallResult,
} from '@/api/setup'

/* ═══════════════════════════════════════════════════════════════════════════
 * State
 * ═════════════════════════════════════════════════════════════════════════ */

const router = useRouter()

const stepLabels = [
  '环境检测',
  '数据库配置',
  'Redis配置',
  '管理员账号',
  '安装执行',
  '安装完成',
] as const

const activeStep = ref(0)

// ── Step 0: Environment check ────────────────────────────────────────────
const envInfo = ref<EnvInfo | null>(null)
const envCheckLoading = ref(false)
const envHasIssues = ref(false)

// ── Step 1: Database config ──────────────────────────────────────────────
const dbFormRef = ref<FormInstance>()
const dbForm = reactive<DbConfig>({
  host: 'localhost',
  port: 5432,
  database: 'idc_platform',
  username: 'idcapp',
  password: '',
})
const dbTesting = ref(false)
const dbTestResult = ref<ConnectionTestResult | null>(null)

// ── Step 2: Redis config ─────────────────────────────────────────────────
const redisFormRef = ref<FormInstance>()
const redisForm = reactive<RedisConfig>({
  host: 'localhost',
  port: 6379,
  password: '',
})
const redisTesting = ref(false)
const redisTestResult = ref<ConnectionTestResult | null>(null)

// ── Step 3: Admin account ────────────────────────────────────────────────
const adminFormRef = ref<FormInstance>()
const adminForm = reactive({
  username: 'admin',
  password: '',
  confirmPassword: '',
  email: '',
  jwtSecret: '',
})

// ── Step 4: Install execution ────────────────────────────────────────────
const installRunning = ref(false)
const installProgress = ref(0)
interface InstallStepState {
  label: string
  status: 'pending' | 'running' | 'done' | 'failed'
}
const installSteps = reactive<InstallStepState[]>([
  { label: '创建数据库', status: 'pending' },
  { label: '执行迁移',   status: 'pending' },
  { label: '写入配置',   status: 'pending' },
  { label: '完成',       status: 'pending' },
])
const installResult = ref<InstallResult | null>(null)
const installError = ref('')

/* ═══════════════════════════════════════════════════════════════════════════
 * Computed
 * ═════════════════════════════════════════════════════════════════════════ */

interface StrengthInfo {
  level: number
  label: string
  color: string
  percent: number
}

const passwordStrength = computed<StrengthInfo>(() => {
  const pwd = adminForm.password
  if (!pwd) {
    return { level: 0, label: '', color: '', percent: 0 }
  }

  let score = 0
  if (pwd.length >= 8)   score++
  if (pwd.length >= 12)  score++
  if (/[a-z]/.test(pwd)) score++
  if (/[A-Z]/.test(pwd)) score++
  if (/[0-9]/.test(pwd)) score++
  if (/[^a-zA-Z0-9]/.test(pwd)) score++

  if (score <= 2) return { level: 1, label: '弱', color: '#f56c6c', percent: 33 }
  if (score <= 4) return { level: 2, label: '中', color: '#e6a23c', percent: 66 }
  return { level: 3, label: '强', color: '#67c23a', percent: 100 }
})

/* ═══════════════════════════════════════════════════════════════════════════
 * Validation rules
 * ═════════════════════════════════════════════════════════════════════════ */

const dbRules: FormRules = {
  host:     [{ required: true, message: '请输入主机地址', trigger: 'blur' }],
  port:     [{ required: true, message: '请输入端口号',   trigger: 'blur' }],
  database: [{ required: true, message: '请输入数据库名', trigger: 'blur' }],
  username: [{ required: true, message: '请输入用户名',   trigger: 'blur' }],
  password: [{ required: true, message: '请输入密码',     trigger: 'blur' }],
}

const redisRules: FormRules = {
  host: [{ required: true, message: '请输入主机地址', trigger: 'blur' }],
  port: [{ required: true, message: '请输入端口号',   trigger: 'blur' }],
}

const validateConfirmPassword = (
  _rule: unknown,
  value: string,
  callback: (error?: Error) => void,
): void => {
  if (value !== adminForm.password) {
    callback(new Error('两次输入的密码不一致'))
  } else {
    callback()
  }
}

const adminRules: FormRules = {
  username: [
    { required: true, message: '请输入管理员用户名', trigger: 'blur' },
    { min: 2, message: '用户名至少 2 个字符', trigger: 'blur' },
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 8, message: '密码至少 8 个字符', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认密码', trigger: 'blur' },
    { validator: validateConfirmPassword as unknown, trigger: 'blur' },
  ],
  email: [
    { required: true, message: '请输入邮箱地址', trigger: 'blur' },
    { type: 'email', message: '请输入有效的邮箱地址', trigger: 'blur' },
  ],
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Helpers
 * ═════════════════════════════════════════════════════════════════════════ */

function delay(ms: number): Promise<void> {
  return new Promise(resolve => { setTimeout(resolve, ms) })
}

function generateJwtSecret(): void {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-='
  const array = new Uint8Array(64)
  crypto.getRandomValues(array)
  let result = ''
  for (let i = 0; i < 64; i++) {
    result += chars[array[i] % chars.length]
  }
  adminForm.jwtSecret = result
}

function copyJwtSecret(): void {
  navigator.clipboard.writeText(adminForm.jwtSecret).then(() => {
    ElMessage.success('已复制到剪贴板')
  }).catch(() => {
    ElMessage.error('复制失败，请手动复制')
  })
}

function copyInstallSecret(): void {
  if (installResult.value?.install_secret) {
    navigator.clipboard.writeText(installResult.value.install_secret).then(() => {
      ElMessage.success('已复制到剪贴板')
    }).catch(() => {
      ElMessage.error('复制失败，请手动复制')
    })
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Step 0: Environment check
 * ═════════════════════════════════════════════════════════════════════════ */

async function checkEnvironment(): Promise<void> {
  envCheckLoading.value = true
  envHasIssues.value = false
  try {
    envInfo.value = await checkEnv()
    if (!envInfo.value.disk_space.enough || !envInfo.value.memory.enough) {
      envHasIssues.value = true
    }
    ElMessage.success('环境检测完成')
  } catch (e) {
    ElMessage.error('环境检测失败：' + ((e as Error).message ?? '未知错误'))
  } finally {
    envCheckLoading.value = false
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Step 1: Test DB connection
 * ═════════════════════════════════════════════════════════════════════════ */

async function handleTestDb(): Promise<void> {
  const valid = await dbFormRef.value?.validate().catch(() => false) ?? false
  if (!valid) return

  dbTesting.value = true
  dbTestResult.value = null
  try {
    dbTestResult.value = await testDbConnection(dbForm)
    if (dbTestResult.value.success) {
      ElMessage.success('数据库连接成功')
    } else {
      ElMessage.error(dbTestResult.value.message)
    }
  } catch (e) {
    dbTestResult.value = {
      success: false,
      message: (e as Error).message ?? '连接测试失败',
    }
    ElMessage.error(dbTestResult.value.message)
  } finally {
    dbTesting.value = false
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Step 2: Test Redis connection
 * ═════════════════════════════════════════════════════════════════════════ */

async function handleTestRedis(): Promise<void> {
  const valid = await redisFormRef.value?.validate().catch(() => false) ?? false
  if (!valid) return

  redisTesting.value = true
  redisTestResult.value = null
  try {
    redisTestResult.value = await testRedisConnection(redisForm)
    if (redisTestResult.value.success) {
      ElMessage.success('Redis 连接成功')
    } else {
      ElMessage.error(redisTestResult.value.message)
    }
  } catch (e) {
    redisTestResult.value = {
      success: false,
      message: (e as Error).message ?? '连接测试失败',
    }
    ElMessage.error(redisTestResult.value.message)
  } finally {
    redisTesting.value = false
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Step 4: Execute install
 * ═════════════════════════════════════════════════════════════════════════ */

async function executeInstall(): Promise<void> {
  installRunning.value = true
  installProgress.value = 0
  installError.value = ''
  const stepItems = installSteps as InstallStepState[]
  for (const s of stepItems) {
    s.status = 'pending'
  }

  try {
    // Call backend — stub returns immediately, then we animate steps
    const result = await runInstall({
      db: dbForm,
      redis: redisForm,
      admin: {
        username: adminForm.username,
        password: adminForm.password,
        email: adminForm.email,
        jwt_secret: adminForm.jwtSecret,
      },
    })

    // Animate through steps for visual feedback
    for (let i = 0; i < stepItems.length; i++) {
      stepItems[i].status = 'running'
      await delay(700)
      stepItems[i].status = 'done'
      installProgress.value = Math.round(((i + 1) / stepItems.length) * 100)
    }

    installResult.value = result
    // Store install_secret for subsequent API calls
    if (result.install_secret) {
      localStorage.setItem('install_secret', result.install_secret)
    }
    await delay(500)
    activeStep.value = 5
  } catch (e) {
    // Mark the failed step
    const failedIndex = installSteps.findIndex(s => s.status === 'running')
    if (failedIndex >= 0) {
      installSteps[failedIndex].status = 'failed'
    } else {
      // All were pending — mark first as failed
      installSteps[0].status = 'failed'
    }
    installError.value = (e as Error).message ?? '安装过程中发生错误'
    installResult.value = null
    ElMessage.error(installError.value)
  } finally {
    installRunning.value = false
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Navigation
 * ═════════════════════════════════════════════════════════════════════════ */

async function nextStep(): Promise<void> {
  let valid = true

  if (activeStep.value === 1) {
    valid = await dbFormRef.value?.validate().catch(() => false) ?? false
  } else if (activeStep.value === 2) {
    valid = await redisFormRef.value?.validate().catch(() => false) ?? false
  } else if (activeStep.value === 3) {
    valid = await adminFormRef.value?.validate().catch(() => false) ?? false
  }

  if (!valid) return

  if (activeStep.value === 4) {
    await executeInstall()
  } else {
    activeStep.value++
  }
}

function prevStep(): void {
  if (activeStep.value > 0) {
    activeStep.value--
  }
}

function goToLogin(): void {
  router.push('/login')
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ═════════════════════════════════════════════════════════════════════════ */

onMounted(async () => {
  // If already installed, redirect to login
  try {
    const status = await getSetupStatus()
    if (status.installed) {
      router.replace('/login')
      return
    }
  } catch {
    // Status check failed — proceed with setup
  }

  // Generate a default JWT secret
  generateJwtSecret()
})

// Watch for step 4 transition to auto-start installation
watch(activeStep, (newVal) => {
  if (newVal === 4) {
    executeInstall()
  }
})
</script>

<template>
  <div class="setup-container">
    <div class="setup-card">
      <h1 class="setup-title">IDC 平台安装向导</h1>

      <!-- ═════════════════════════════════════════════════════════════════
           Step indicator
           ═════════════════════════════════════════════════════════════════ -->
      <el-steps
        :active="activeStep"
        align-center
        finish-status="success"
        class="setup-steps"
      >
        <el-step
          v-for="(label, idx) in stepLabels"
          :key="idx"
          :title="label"
        />
      </el-steps>

      <!-- ═════════════════════════════════════════════════════════════════
           Step 0: 环境检测
           ═════════════════════════════════════════════════════════════════ -->
      <div v-if="activeStep === 0" class="step-panel">
        <h3 class="step-heading">系统环境检测</h3>
        <p class="step-desc">检测服务器环境是否满足系统运行要求</p>

        <div v-loading="envCheckLoading" class="env-list">
          <div v-if="envInfo" class="env-list-inner">
            <!-- OS -->
            <div class="env-item">
              <span class="env-label">操作系统</span>
              <span class="env-value">
                <el-tag size="small" type="success">
                  {{ envInfo.os }} {{ envInfo.os_version }}
                </el-tag>
              </span>
            </div>

            <!-- PostgreSQL -->
            <div class="env-item">
              <span class="env-label">PostgreSQL</span>
              <span class="env-value">
                <el-tag size="small" type="warning">
                  <el-icon style="margin-right: 4px"><WarningFilled /></el-icon>
                  未测试（将在下一步配置）
                </el-tag>
              </span>
            </div>

            <!-- Redis -->
            <div class="env-item">
              <span class="env-label">Redis</span>
              <span class="env-value">
                <el-tag size="small" type="warning">
                  <el-icon style="margin-right: 4px"><WarningFilled /></el-icon>
                  未测试（将在后续步骤配置）
                </el-tag>
              </span>
            </div>

            <!-- Disk -->
            <div class="env-item">
              <span class="env-label">磁盘空间</span>
              <span class="env-value">
                <el-tag
                  size="small"
                  :type="envInfo.disk_space.enough ? 'success' : 'danger'"
                >
                  <el-icon style="margin-right: 4px">
                    <SuccessFilled v-if="envInfo.disk_space.enough" />
                    <Close v-else />
                  </el-icon>
                  总计 {{ envInfo.disk_space.total_gb }} GB
                  · 剩余 {{ envInfo.disk_space.free_gb }} GB
                </el-tag>
              </span>
            </div>

            <!-- Memory -->
            <div class="env-item">
              <span class="env-label">内存</span>
              <span class="env-value">
                <el-tag
                  size="small"
                  :type="envInfo.memory.enough ? 'success' : 'danger'"
                >
                  <el-icon style="margin-right: 4px">
                    <SuccessFilled v-if="envInfo.memory.enough" />
                    <Close v-else />
                  </el-icon>
                  总计 {{ envInfo.memory.total_gb }} GB
                  · 可用 {{ envInfo.memory.free_gb }} GB
                </el-tag>
              </span>
            </div>

            <el-alert
              v-if="envHasIssues"
              title="部分环境检测未通过，系统可能无法正常运行"
              type="warning"
              show-icon
              :closable="false"
              style="margin-top: 16px"
            />
          </div>

          <el-empty v-else-if="!envCheckLoading" description="点击下方按钮开始检测" />
        </div>

        <div class="step-actions">
          <el-button
            @click="checkEnvironment"
            :loading="envCheckLoading"
            plain
          >
            重新检测
          </el-button>
          <el-button
            type="primary"
            :disabled="!envInfo"
            @click="nextStep"
          >
            继续
          </el-button>
        </div>
      </div>

      <!-- ═════════════════════════════════════════════════════════════════
           Step 1: 数据库配置
           ═════════════════════════════════════════════════════════════════ -->
      <div v-if="activeStep === 1" class="step-panel">
        <h3 class="step-heading">数据库配置</h3>
        <p class="step-desc">配置 PostgreSQL 数据库连接信息</p>

        <el-form
          ref="dbFormRef"
          :model="dbForm"
          :rules="dbRules"
          label-width="100px"
          label-position="left"
          class="step-form"
          @keyup.enter="handleTestDb"
        >
          <el-form-item label="主机地址" prop="host">
            <el-input v-model="dbForm.host" placeholder="localhost" />
          </el-form-item>
          <el-form-item label="端口" prop="port">
            <el-input
              v-model.number="dbForm.port"
              placeholder="5432"
              type="number"
            />
          </el-form-item>
          <el-form-item label="数据库名" prop="database">
            <el-input v-model="dbForm.database" placeholder="idc_platform" />
          </el-form-item>
          <el-form-item label="用户名" prop="username">
            <el-input v-model="dbForm.username" placeholder="idcapp" />
          </el-form-item>
          <el-form-item label="密码" prop="password">
            <el-input
              v-model="dbForm.password"
              type="password"
              placeholder="请输入数据库密码"
              show-password
            />
          </el-form-item>
        </el-form>

        <el-alert
          v-if="dbTestResult"
          :title="dbTestResult.message"
          :type="dbTestResult.success ? 'success' : 'error'"
          show-icon
          :closable="false"
          class="test-result-alert"
        />

        <div class="step-actions">
          <el-button @click="prevStep">上一步</el-button>
          <el-button
            @click="handleTestDb"
            :loading="dbTesting"
            plain
          >
            测试连接
          </el-button>
          <el-button type="primary" @click="nextStep">
            继续
          </el-button>
        </div>
      </div>

      <!-- ═════════════════════════════════════════════════════════════════
           Step 2: Redis配置
           ═════════════════════════════════════════════════════════════════ -->
      <div v-if="activeStep === 2" class="step-panel">
        <h3 class="step-heading">Redis 配置</h3>
        <p class="step-desc">配置 Redis 缓存服务连接信息</p>

        <el-form
          ref="redisFormRef"
          :model="redisForm"
          :rules="redisRules"
          label-width="100px"
          label-position="left"
          class="step-form"
          @keyup.enter="handleTestRedis"
        >
          <el-form-item label="主机地址" prop="host">
            <el-input v-model="redisForm.host" placeholder="localhost" />
          </el-form-item>
          <el-form-item label="端口" prop="port">
            <el-input
              v-model.number="redisForm.port"
              placeholder="6379"
              type="number"
            />
          </el-form-item>
          <el-form-item label="密码" prop="password">
            <el-input
              v-model="redisForm.password"
              type="password"
              placeholder="无需密码则留空"
              show-password
            />
          </el-form-item>
        </el-form>

        <el-alert
          v-if="redisTestResult"
          :title="redisTestResult.message"
          :type="redisTestResult.success ? 'success' : 'error'"
          show-icon
          :closable="false"
          class="test-result-alert"
        />

        <div class="step-actions">
          <el-button @click="prevStep">上一步</el-button>
          <el-button
            @click="handleTestRedis"
            :loading="redisTesting"
            plain
          >
            测试连接
          </el-button>
          <el-button type="primary" @click="nextStep">
            继续
          </el-button>
        </div>
      </div>

      <!-- ═════════════════════════════════════════════════════════════════
           Step 3: 管理员账号
           ═════════════════════════════════════════════════════════════════ -->
      <div v-if="activeStep === 3" class="step-panel">
        <h3 class="step-heading">管理员账号</h3>
        <p class="step-desc">创建系统管理员账号与安全凭证</p>

        <el-form
          ref="adminFormRef"
          :model="adminForm"
          :rules="adminRules"
          label-width="120px"
          label-position="left"
          class="step-form"
        >
          <el-form-item label="用户名" prop="username">
            <el-input v-model="adminForm.username" placeholder="admin" />
          </el-form-item>

          <el-form-item label="密码" prop="password">
            <el-input
              v-model="adminForm.password"
              type="password"
              placeholder="至少 8 个字符"
              show-password
            />
            <!-- Password strength bar -->
            <div v-if="adminForm.password" class="pwd-strength">
              <el-progress
                :percentage="passwordStrength.percent"
                :color="passwordStrength.color"
                :stroke-width="4"
                :show-text="false"
                class="pwd-strength-bar"
              />
              <span
                class="pwd-strength-label"
                :style="{ color: passwordStrength.color }"
              >
                密码强度：{{ passwordStrength.label }}
              </span>
            </div>
          </el-form-item>

          <el-form-item label="确认密码" prop="confirmPassword">
            <el-input
              v-model="adminForm.confirmPassword"
              type="password"
              placeholder="再次输入密码"
              show-password
            />
          </el-form-item>

          <el-form-item label="邮箱" prop="email">
            <el-input v-model="adminForm.email" placeholder="admin@example.com" />
          </el-form-item>

          <el-form-item label="JWT Secret" prop="jwtSecret">
            <el-input
              v-model="adminForm.jwtSecret"
              placeholder="点击下方按钮自动生成"
              type="textarea"
              :rows="2"
            />
            <div class="jwt-actions">
              <el-button size="small" plain @click="generateJwtSecret">
                自动生成
              </el-button>
              <el-button
                size="small"
                plain
                :disabled="!adminForm.jwtSecret"
                @click="copyJwtSecret"
              >
                <el-icon style="margin-right: 4px"><CopyDocument /></el-icon>
                复制
              </el-button>
            </div>
          </el-form-item>
        </el-form>

        <div class="step-actions">
          <el-button @click="prevStep">上一步</el-button>
          <el-button type="primary" @click="nextStep">
            下一步
          </el-button>
        </div>
      </div>

      <!-- ═════════════════════════════════════════════════════════════════
           Step 4: 安装执行
           ═════════════════════════════════════════════════════════════════ -->
      <div v-if="activeStep === 4" class="step-panel">
        <h3 class="step-heading">安装执行</h3>
        <p class="step-desc">系统正在执行安装操作，请稍候...</p>

        <el-progress
          :percentage="installProgress"
          :stroke-width="12"
          :color="installProgress === 100 ? '#67c23a' : '#409eff'"
          striped
          striped-flow
          class="overall-progress"
        />

        <div class="install-steps">
          <div
            v-for="(step, idx) in installSteps"
            :key="idx"
            class="install-step-item"
          >
            <!-- Pending -->
            <el-icon v-if="step.status === 'pending'" class="step-icon pending">
              <span class="step-number">{{ idx + 1 }}</span>
            </el-icon>

            <!-- Running -->
            <el-icon
              v-else-if="step.status === 'running'"
              class="step-icon running"
              :size="22"
            >
              <Loading />
            </el-icon>

            <!-- Done -->
            <el-icon
              v-else-if="step.status === 'done'"
              class="step-icon done"
              :size="22"
            >
              <Check />
            </el-icon>

            <!-- Failed -->
            <el-icon
              v-else
              class="step-icon failed"
              :size="22"
            >
              <Close />
            </el-icon>

            <span
              class="step-label"
              :class="{
                'text-pending': step.status === 'pending',
                'text-running': step.status === 'running',
                'text-done':    step.status === 'done',
                'text-failed':  step.status === 'failed',
              }"
            >
              {{ step.label }}
            </span>
          </div>
        </div>

        <el-alert
          v-if="installError"
          :title="installError"
          type="error"
          show-icon
          :closable="false"
          style="margin-top: 16px"
        />
      </div>

      <!-- ═════════════════════════════════════════════════════════════════
           Step 5: 安装完成
           ═════════════════════════════════════════════════════════════════ -->
      <div v-if="activeStep === 5" class="step-panel">
        <el-result
          v-if="installResult?.success"
          icon="success"
          title="安装完成"
          sub-title="系统已成功安装并准备就绪"
        >
          <template #extra>
            <div class="result-detail">
              <h4>安装摘要</h4>
              <div
                v-for="(step, idx) in installResult.steps"
                :key="idx"
                class="result-step"
              >
                <el-icon
                  :size="18"
                  :color="step.status === 'done' ? '#67c23a' : '#f56c6c'"
                >
                  <Check v-if="step.status === 'done'" />
                  <Close v-else />
                </el-icon>
                <span>{{ step.name }}</span>
              </div>

              <el-divider />

              <div class="result-login-info">
                <h4>管理员登录信息</h4>
                <p><strong>用户名：</strong>{{ installResult.admin_username }}</p>
                <p><strong>密码：</strong><em>（您设置的密码）</em></p>
              </div>

              <el-divider v-if="installResult.install_secret" />

              <div v-if="installResult.install_secret" class="result-secret">
                <h4>安装密钥（请妥善保管）</h4>
                <p class="secret-desc">
                  该密钥会自动附加到所有 API 请求路径中，防止未授权访问。
                  如果丢失，请联系系统管理员。
                </p>
                <el-input
                  :model-value="installResult.install_secret"
                  readonly
                  class="secret-input"
                >
                  <template #append>
                    <el-button
                      @click="copyInstallSecret"
                      :icon="CopyDocument"
                    >
                      复制
                    </el-button>
                  </template>
                </el-input>
              </div>
            </div>

            <el-button type="primary" size="large" @click="goToLogin">
              前往登录
            </el-button>
          </template>
        </el-result>

        <el-result
          v-else
          icon="error"
          title="安装失败"
          sub-title="安装过程中遇到问题，请检查后重试"
        >
          <template #extra>
            <p class="error-message">{{ installError || '未知错误' }}</p>
            <el-button type="primary" @click="activeStep = 4">
              重试安装
            </el-button>
          </template>
        </el-result>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* ── Layout ─────────────────────────────────────────────────────────────── */
.setup-container {
  min-height: 100vh;
  display: flex;
  align-items: flex-start;
  justify-content: center;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  padding: 40px 16px;
  box-sizing: border-box;
}

.setup-card {
  width: 100%;
  max-width: 720px;
  padding: 40px 48px;
  background: #fff;
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.18);
}

.setup-title {
  text-align: center;
  margin: 0 0 32px;
  color: var(--el-text-color-primary);
  font-size: 24px;
  font-weight: 600;
}

.setup-steps {
  margin-bottom: 32px;
}

/* ── Step panel ──────────────────────────────────────────────────────────── */
.step-panel {
  min-height: 320px;
}

.step-heading {
  margin: 0 0 6px;
  font-size: 18px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.step-desc {
  margin: 0 0 24px;
  font-size: 14px;
  color: var(--el-text-color-secondary);
}

.step-form {
  max-width: 480px;
}

.step-actions {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  margin-top: 32px;
  padding-top: 20px;
  border-top: 1px solid var(--el-border-color-light);
}

/* ── Environment list ────────────────────────────────────────────────────── */
.env-list {
  min-height: 160px;
}

.env-list-inner {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.env-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 16px;
  background: var(--el-fill-color-lighter);
  border-radius: 8px;
}

.env-label {
  font-size: 14px;
  font-weight: 500;
  color: var(--el-text-color-primary);
}

.env-value {
  display: flex;
  align-items: center;
}

/* ── Test result ─────────────────────────────────────────────────────────── */
.test-result-alert {
  margin-top: 16px;
}

/* ── Password strength ───────────────────────────────────────────────────── */
.pwd-strength {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 6px;
}

.pwd-strength-bar {
  flex: 1;
  max-width: 120px;
}

.pwd-strength-label {
  font-size: 12px;
  white-space: nowrap;
}

/* ── JWT actions ─────────────────────────────────────────────────────────── */
.jwt-actions {
  display: flex;
  gap: 8px;
  margin-top: 6px;
}

/* ── Install progress ────────────────────────────────────────────────────── */
.overall-progress {
  margin-bottom: 28px;
}

.install-steps {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.install-step-item {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 12px 16px;
  background: var(--el-fill-color-lighter);
  border-radius: 8px;
  transition: background 0.3s;
}

.step-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border-radius: 50%;
  flex-shrink: 0;
}

.step-number {
  font-size: 13px;
  font-weight: 600;
  font-style: normal;
}

.step-icon.pending {
  background: var(--el-fill-color);
  color: var(--el-text-color-secondary);
}

.step-icon.running {
  background: #ecf5ff;
  color: #409eff;
  animation: spin 1s linear infinite;
}

.step-icon.done {
  background: #f0f9eb;
  color: #67c23a;
}

.step-icon.failed {
  background: #fef0f0;
  color: #f56c6c;
}

.step-label {
  font-size: 14px;
  font-weight: 500;
}

.text-pending { color: var(--el-text-color-secondary); }
.text-running { color: #409eff; }
.text-done    { color: #67c23a; }
.text-failed  { color: #f56c6c; }

@keyframes spin {
  from { transform: rotate(0deg); }
  to   { transform: rotate(360deg); }
}

/* ── Result ──────────────────────────────────────────────────────────────── */
.result-detail {
  text-align: left;
  max-width: 400px;
  margin: 0 auto 24px;
}

.result-detail h4 {
  margin: 0 0 12px;
  font-size: 15px;
  color: var(--el-text-color-primary);
}

.result-step {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 0;
  font-size: 14px;
  color: var(--el-text-color-regular);
}

.result-login-info {
  margin-top: 8px;
}

.result-login-info p {
  margin: 6px 0;
  font-size: 14px;
  color: var(--el-text-color-regular);
}

.result-secret {
  margin-top: 8px;
}

.result-secret h4 {
  margin: 0 0 6px;
  font-size: 15px;
  color: var(--el-text-color-primary);
}

.secret-desc {
  margin: 0 0 12px;
  font-size: 13px;
  color: var(--el-text-color-secondary);
  line-height: 1.5;
}

.secret-input {
  max-width: 100%;
}

.error-message {
  color: #f56c6c;
  font-size: 14px;
  margin-bottom: 16px;
}

/* ── Responsive ──────────────────────────────────────────────────────────── */
@media (max-width: 640px) {
  .setup-card {
    padding: 24px 20px;
  }

  .setup-title {
    font-size: 20px;
  }

  .step-form {
    max-width: 100%;
  }

  .step-actions {
    flex-wrap: wrap;
  }

  .step-actions .el-button {
    flex: 1;
    min-width: 0;
  }

  .env-item {
    flex-direction: column;
    align-items: flex-start;
    gap: 6px;
  }
}
</style>
