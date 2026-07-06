import request from './request'

/* ──────────────────────────────────────────────────────────────────────────
 * Types
 * ──────────────────────────────────────────────────────────────────────── */

export interface DiskInfo {
  total_gb: number
  free_gb: number
  enough: boolean
}

export interface MemoryInfo {
  total_gb: number
  free_gb: number
  enough: boolean
}

export interface EnvInfo {
  os: string
  os_version: string
  postgresql: { reachable: boolean; version?: string }
  redis: { reachable: boolean; version?: string }
  disk_space: DiskInfo
  memory: MemoryInfo
}

export interface DbConfig {
  host: string
  port: number
  database: string
  username: string
  password: string
}

export interface RedisConfig {
  host: string
  port: number
  password: string
}

export interface AdminConfig {
  username: string
  password: string
  email: string
  jwt_secret: string
}

export interface ConnectionTestResult {
  success: boolean
  message: string
}

export interface InstallStep {
  name: string
  status: 'pending' | 'running' | 'done' | 'failed'
  message?: string
}

export interface InstallConfig {
  db: DbConfig
  redis: RedisConfig
  admin: AdminConfig
}

export interface InstallResult {
  success: boolean
  steps: InstallStep[]
  admin_username: string
  message: string
}

export interface SetupStatus {
  installed: boolean
}

/* ──────────────────────────────────────────────────────────────────────────
 * API functions
 * ──────────────────────────────────────────────────────────────────────── */

/** POST /api/v1/setup/check-env — 检测系统环境 */
export async function checkEnv(): Promise<EnvInfo> {
  const res = await request.post<EnvInfo>('/setup/check-env')
  return res.data
}

/** POST /api/v1/setup/test-db — 测试数据库连接 */
export async function testDbConnection(config: DbConfig): Promise<ConnectionTestResult> {
  const res = await request.post<ConnectionTestResult>('/setup/test-db', config)
  return res.data
}

/** POST /api/v1/setup/test-redis — 测试 Redis 连接 */
export async function testRedisConnection(config: RedisConfig): Promise<ConnectionTestResult> {
  const res = await request.post<ConnectionTestResult>('/setup/test-redis', config)
  return res.data
}

/** POST /api/v1/setup/run — 执行安装 */
export async function runInstall(config: InstallConfig): Promise<InstallResult> {
  const res = await request.post<InstallResult>('/setup/run', config)
  return res.data
}

/** GET /api/v1/setup/status — 检查是否已安装 */
export async function getSetupStatus(): Promise<SetupStatus> {
  const res = await request.get<SetupStatus>('/setup/status')
  return res.data
}

/** GET /api/v1/setup/status — 检查安装状态（别名，语义更明确） */
export async function checkSetupStatus(): Promise<SetupStatus> {
  return getSetupStatus()
}
