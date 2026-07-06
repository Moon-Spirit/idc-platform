import { defineStore } from 'pinia'
import { ref, type Ref } from 'vue'
import { login as apiLogin, type LoginRequest, type LoginResponse } from '@/api/auth'
import request from '@/api/request'

interface UserInfo {
  id: number
  username: string
  role: string
}

export const useAuthStore = defineStore('auth', () => {
  const token: Ref<string | null> = ref(localStorage.getItem('token'))
  const user: Ref<UserInfo | null> = ref(null)
  const permissions: Ref<string[]> = ref([])

  async function login(credentials: LoginRequest): Promise<void> {
    const res: LoginResponse = await apiLogin(credentials)
    token.value = res.token
    localStorage.setItem('token', res.token)
    // fetch user info after login
    await fetchUserInfo()
  }

  async function fetchUserInfo(): Promise<void> {
    const res = await request.get<{ data: UserInfo }>('/user/info')
    user.value = res.data.data
  }

  function logout(): void {
    token.value = null
    user.value = null
    permissions.value = []
    localStorage.removeItem('token')
  }

  return {
    token,
    user,
    permissions,
    login,
    logout,
    fetchUserInfo,
  }
})
