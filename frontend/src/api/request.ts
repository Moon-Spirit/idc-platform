import axios, { type AxiosInstance, type InternalAxiosRequestConfig } from 'axios'
import { ElMessage } from 'element-plus'

const BASE_URL = import.meta.env.VITE_API_BASE_URL ?? '/api/v1'

const request: AxiosInstance = axios.create({
  baseURL: BASE_URL,
  timeout: 15000,
  headers: {
    'Content-Type': 'application/json',
  },
})

// Request interceptor: attach Authorization header
request.interceptors.request.use(
  (config: InternalAxiosRequestConfig) => {
    const token = localStorage.getItem('token')
    if (token && config.headers) {
      config.headers.Authorization = `Bearer ${token}`
    }
    return config
  },
  (error) => {
    return Promise.reject(error)
  },
)

/**
 * Backend API response format: { code: 0, message: "success", data: ... }
 * This interceptor unwraps `data` on success (code === 0) so callers
 * receive the payload directly. Non-zero codes are rejected as errors.
 */
function unwrapResponseBody(body: unknown): unknown {
  if (body !== null && typeof body === 'object' && 'code' in (body as Record<string, unknown>)) {
    const obj = body as { code: number; message?: string; data?: unknown }
    if (obj.code === 0) {
      return obj.data
    }
    throw new Error(obj.message ?? 'Request failed')
  }
  return body
}

request.interceptors.response.use(
  (response) => {
    try {
      response.data = unwrapResponseBody(response.data)
    } catch (e) {
      ElMessage.error((e as Error).message)
      return Promise.reject(e)
    }
    return response
  },
  (error) => {
    if (error.response) {
      const status = error.response.status
      const body = error.response.data
      const msg =
        (body && typeof body === 'object' && 'message' in body
          ? (body as { message?: string }).message
          : undefined) ?? error.message

      if (status === 401) {
        localStorage.removeItem('token')
        if (!window.location.pathname.includes('/login')) {
          ElMessage.error(msg)
          window.location.href = '/login'
        }
      }

      return Promise.reject(new Error(msg))
    }
    return Promise.reject(error)
  },
)

export default request
