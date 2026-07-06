import request from './request'

export interface LoginRequest {
  username: string
  password: string
}

export interface LoginResponse {
  token: string
}

export async function login(data: LoginRequest): Promise<LoginResponse> {
  const res = await request.post<LoginResponse>('/auth/login', data)
  return res.data
}
