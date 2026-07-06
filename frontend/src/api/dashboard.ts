import request from './request'

// ── Types ──────────────────────────────────────────────────────────────────

export interface DashboardStats {
  active_subscriptions: number
  pending_orders: number
  unpaid_invoices: number
  monthly_spending: string
  total_orders: number
  total_revenue: string
  balance: string
  distributor_name: string
}

// ── API functions ──────────────────────────────────────────────────────────

export async function getDashboardStats(): Promise<DashboardStats> {
  const res = await request.get<{ data: DashboardStats }>('/dashboard/stats')
  return res.data.data
}
