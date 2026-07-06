import request from './request'

// ── Types ──────────────────────────────────────────────────────────────────

export interface Distributor {
  id: number
  name: string
  level: number
  parent_id: number | null
  parent_name: string | null
  price_template_id: number | null
  contact_person: string
  contact_phone: string
  contact_email: string
  balance: string
  credit_limit: string
  status: number
  remark: string
  children_count: number
  order_count: number
  total_revenue: string
  created_at: string
  updated_at: string
}

export interface DistributorDetail extends Distributor {
  active_subscriptions: number
  overdue_invoices: number
}

export interface DistributorTreeNode {
  id: number
  name: string
  level: number
  status: number
  children: DistributorTreeNode[]
}

export interface DistributorStats {
  orders_this_month: number
  revenue_this_month: string
  subscription_count: number
  overdue_invoice_count: number
  total_orders: number
  total_revenue: string
}

export interface PaginatedResponse<T> {
  items: T[]
  total: number
  page: number
  per_page: number
}

export interface DistributorFormData {
  name: string
  level: number
  parent_id: number | null
  price_template_id: number | null
  contact_person: string
  contact_phone: string
  contact_email: string
  credit_limit: string
  remark: string
  status: number
}

// ── API functions ─────────────────────────────────────────────────────────

export async function listDistributors(params: {
  page?: number
  per_page?: number
  status?: number
  level?: number
  keyword?: string
  parent_id?: number
}): Promise<PaginatedResponse<Distributor>> {
  const res = await request.get<{ data: PaginatedResponse<Distributor> }>('/distributors', { params })
  return res.data.data
}

export async function getDistributor(id: number): Promise<DistributorDetail> {
  const res = await request.get<{ data: DistributorDetail }>(`/distributors/${id}`)
  return res.data.data
}

export async function createDistributor(data: DistributorFormData): Promise<{ id: number }> {
  const res = await request.post<{ data: { id: number } }>('/distributors', data)
  return res.data.data
}

export async function updateDistributor(id: number, data: Partial<DistributorFormData>): Promise<void> {
  await request.put(`/distributors/${id}`, data)
}

export async function getDistributorTree(id: number): Promise<DistributorTreeNode> {
  const res = await request.get<{ data: DistributorTreeNode }>(`/distributors/${id}/tree`)
  return res.data.data
}

export async function getDirectChildren(id: number): Promise<Distributor[]> {
  const res = await request.get<{ data: Distributor[] }>(`/distributors/${id}/children`)
  return res.data.data
}

export async function getDistributorStatistics(id: number): Promise<DistributorStats> {
  const res = await request.get<{ data: DistributorStats }>(`/distributors/${id}/statistics`)
  return res.data.data
}
