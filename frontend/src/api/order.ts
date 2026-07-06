import request from './request'

// ── Types ──────────────────────────────────────────────────────────────────

export type OrderStatus =
  | 'pending'
  | 'approved'
  | 'rejected'
  | 'active'
  | 'suspended'
  | 'terminated'

export interface OrderItem {
  product_name: string
  product_type?: string
  spec?: string
  quantity: number
  unit_price: string
  subtotal: string
}

export interface TimelineEntry {
  status: string
  time: string
  operator: string | null
}

export interface Order {
  id: number
  order_no: string
  distributor_id: number
  distributor_name: string
  status: OrderStatus
  total_amount: string
  final_amount: string
  billing_cycle: string
  item_count: number
  remark?: string
  created_at: string
  updated_at?: string
}

export interface OrderDetail extends Order {
  items: OrderItem[]
  timeline: TimelineEntry[]
}

export interface PaginatedOrders {
  items: Order[]
  total: number
  page: number
  per_page: number
}

// ── API functions ─────────────────────────────────────────────────────────

export async function listOrders(params: {
  page?: number
  per_page?: number
  status?: string
  distributor_id?: number
  keyword?: string
  start_date?: string
  end_date?: string
}): Promise<PaginatedOrders> {
  const res = await request.get<{ data: PaginatedOrders }>('/orders', { params })
  return res.data.data
}

export async function getOrder(id: number): Promise<OrderDetail> {
  const res = await request.get<{ data: OrderDetail }>(`/orders/${id}`)
  return res.data.data
}

export async function approveOrder(
  id: number,
  remark?: string,
): Promise<void> {
  await request.put(`/orders/${id}/approve`, { remark })
}

export async function rejectOrder(
  id: number,
  reason: string,
): Promise<void> {
  await request.put(`/orders/${id}/reject`, { reason })
}

export async function cancelOrder(id: number): Promise<void> {
  await request.put(`/orders/${id}/cancel`)
}
