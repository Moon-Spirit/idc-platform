import request from './request'

export interface LoginRequest {
  username: string
  password: string
}

export interface UserInfo {
  id: number
  username: string
  role: string
  real_name: string
  email: string
  phone: string
  distributor_id: number | null
  permissions: string[]
}

export interface LoginResult {
  token: string
  expires_in: number
  user: UserInfo
}

export async function login(data: LoginRequest): Promise<LoginResult> {
  const res = await request.post<LoginResult>('/auth/login', data)
  return res.data
}

export async function fetchUser(): Promise<UserInfo> {
  const res = await request.get<UserInfo>('/auth/me')
  return res.data
}

export async function logout(): Promise<void> {
  await request.post('/auth/logout')
}

export interface RegisterRequest {
  username: string
  password: string
  email: string
  phone: string
  company_name: string
}

export async function register(data: RegisterRequest): Promise<void> {
  await request.post('/auth/register', data)
}
