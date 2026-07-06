import { defineStore } from 'pinia'
import { ref, type Ref } from 'vue'

interface MenuRouteMeta {
  title: string
  icon?: string
}

interface MenuRoute {
  path: string
  name: string
  meta?: MenuRouteMeta
}

export const useAppStore = defineStore('app', () => {
  const sidebarCollapsed: Ref<boolean> = ref(false)
  const theme: Ref<string> = ref('default')

  const menuRoutes: Ref<MenuRoute[]> = ref([
    {
      path: '/dashboard',
      name: 'Dashboard',
      meta: { title: '仪表盘', icon: 'Odometer' },
    },
  ])

  function toggleSidebar(): void {
    sidebarCollapsed.value = !sidebarCollapsed.value
  }

  function setTheme(newTheme: string): void {
    theme.value = newTheme
  }

  function setMenuRoutes(routes: MenuRoute[]): void {
    menuRoutes.value = routes
  }

  return {
    sidebarCollapsed,
    theme,
    menuRoutes,
    toggleSidebar,
    setTheme,
    setMenuRoutes,
  }
})
