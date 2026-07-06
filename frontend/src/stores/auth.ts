import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import {
  login as apiLogin,
  fetchUser as apiFetchUser,
  logout as apiLogout,
  type LoginRequest,
  type UserInfo,
} from '@/api/auth'
import router from '@/router'

export const useAuthStore = defineStore('auth', () => {
  const token = ref<string | null>(localStorage.getItem('token'))
  const user = ref<UserInfo | null>(null)
  const permissions = ref<string[]>([])

  const isLoggedIn = computed(() => !!token.value)

  async function login(credentials: LoginRequest): Promise<void> {
    const res = await apiLogin(credentials)
    token.value = res.token
    user.value = res.user
    permissions.value = res.user.permissions ?? []
    localStorage.setItem('token', res.token)
  }

  async function fetchUserInfo(): Promise<void> {
    if (!token.value) return
    const userInfo = await apiFetchUser()
    user.value = userInfo
    permissions.value = userInfo.permissions ?? []
  }

  async function logout(): Promise<void> {
    try {
      await apiLogout()
    } catch {
      // Ignore server-side logout errors; clear local state regardless
    }
    token.value = null
    user.value = null
    permissions.value = []
    localStorage.removeItem('token')
    router.push({ name: 'Login' })
  }

  async function init(): Promise<void> {
    if (!token.value) return
    try {
      await fetchUserInfo()
    } catch {
      token.value = null
      user.value = null
      permissions.value = []
      localStorage.removeItem('token')
    }
  }

  return {
    token,
    user,
    permissions,
    isLoggedIn,
    login,
    logout,
    fetchUserInfo,
    init,
  }
})
