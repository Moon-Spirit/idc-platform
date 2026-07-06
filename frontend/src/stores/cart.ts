import { defineStore } from 'pinia'
import { ref } from 'vue'
import { getCart } from '@/api/cart'

export const useCartStore = defineStore('cart', () => {
  const itemCount = ref(0)
  const loading = ref(false)

  async function fetchCount(): Promise<void> {
    loading.value = true
    try {
      const data = await getCart()
      itemCount.value = data.items.length
    } catch {
      // handled by interceptor
    } finally {
      loading.value = false
    }
  }

  /**
   * Optimistically increment count (e.g. after quick-add).
   * Falls back to a full refresh.
   */
  async function incrementBy(n: number): Promise<void> {
    itemCount.value += n
    // Fire-and-forget full refresh to stay in sync
    try {
      const data = await getCart()
      itemCount.value = data.items.length
    } catch {
      // handled by interceptor
    }
  }

  return {
    itemCount,
    loading,
    fetchCount,
    incrementBy,
  }
})
