import request from './request'

// ── Types ──────────────────────────────────────────────────────────────────

export interface CartItem {
  id: number
  product_id: number
  product_name: string
  product_type: string
  quantity: number
  period_months: number
  unit_price: string
  subtotal: string
  config: Record<string, unknown>
}

export interface CartData {
  items: CartItem[]
  total_amount: string
}

export interface AddToCartRequest {
  product_id: number
  quantity: number
  period_months: number
  config?: Record<string, unknown>
}

export interface UpdateCartItemRequest {
  quantity?: number
  period_months?: number
  config?: Record<string, unknown>
}

// ── API functions ─────────────────────────────────────────────────────────

export async function getCart(): Promise<CartData> {
  const res = await request.get<{ data: CartData }>('/cart')
  return res.data.data
}

export async function addToCart(data: AddToCartRequest): Promise<{ id: number }> {
  const res = await request.post<{ data: { id: number } }>('/cart/items', data)
  return res.data.data
}

export async function updateCartItem(
  id: number,
  data: UpdateCartItemRequest,
): Promise<void> {
  await request.put(`/cart/items/${id}`, data)
}

export async function removeCartItem(id: number): Promise<void> {
  await request.delete(`/cart/items/${id}`)
}

export async function submitOrder(data: {
  billing_cycle?: string
  remark?: string
}): Promise<{ id: number; order_no: string }> {
  const res = await request.post<{ data: { id: number; order_no: string } }>('/orders', data)
  return res.data.data
}
