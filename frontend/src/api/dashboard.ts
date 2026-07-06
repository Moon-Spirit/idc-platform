import request from './request'

// ── Types: Dealer Dashboard ──────────────────────────────────────────────────

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

export async function getDashboardStats(): Promise<DashboardStats> {
  const res = await request.get<{ data: DashboardStats }>('/dashboard/stats')
  return res.data.data
}

// ── Types: Admin Dashboard ───────────────────────────────────────────────────

export interface RevenueTrendPoint {
  month: string
  amount: string
}

export interface BandwidthTop5Entry {
  distributor: string
  usage_mbps: number
}

export interface AdminDashboardStats {
  total_distributors: number
  new_orders_this_month: number
  revenue_this_month: string
  pending_orders: number
  overdue_invoices: number
  active_subscriptions: number
  bandwidth_top5: BandwidthTop5Entry[]
  revenue_trend: RevenueTrendPoint[]
}

export async function getAdminDashboardStats(): Promise<AdminDashboardStats> {
  const res = await request.get<{ data: AdminDashboardStats }>('/dashboard/stats')
  return res.data.data
}

// ── Types: Revenue Report ────────────────────────────────────────────────────

export interface RevenueReportItem {
  month: string
  amount: string
}

export interface RevenueByProductItem {
  product_type: string
  amount: string
  percentage: number
}

export interface RevenueReportData {
  by_month: RevenueReportItem[]
  by_product_type: RevenueByProductItem[]
}

export async function getRevenueReport(params: {
  start_date?: string
  end_date?: string
}): Promise<RevenueReportData> {
  const res = await request.get<{ data: RevenueReportData }>('/reports/revenue', {
    params: { ...params, group_by: 'month|product_type' },
  })
  return res.data.data
}

// ── Types: Distributor Sales Report ──────────────────────────────────────────

export interface DistributorSalesRow {
  order_no: string
  product_name: string
  quantity: number
  unit_price: string
  total_amount: string
  status: string
  created_at: string
  renewal_rate?: number
}

export interface DistributorSalesSummary {
  total_orders: number
  total_revenue: string
  renewal_rate: number
}

export interface DistributorSalesReport {
  items: DistributorSalesRow[]
  summary: DistributorSalesSummary
  total: number
}

export async function getDistributorSalesReport(params: {
  distributor_id: number
  start_date?: string
  end_date?: string
  page?: number
  per_page?: number
}): Promise<DistributorSalesReport> {
  const res = await request.get<{ data: DistributorSalesReport }>('/reports/distributor-sales', {
    params,
  })
  return res.data.data
}
