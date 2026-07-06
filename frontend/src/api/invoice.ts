import request from './request'

// ── Types ──────────────────────────────────────────────────────────────────

export type InvoiceStatus =
  | 'unpaid'
  | 'paid'
  | 'overdue'
  | 'void'

export interface Invoice {
  id: number
  invoice_no: string
  distributor_id?: number
  distributor_name?: string
  period: string
  total_amount: string
  paid_amount: string
  status: InvoiceStatus
  due_date: string
  item_count: number
  created_at?: string
  updated_at?: string
}

export interface InvoiceItem {
  type: string
  description: string
  amount: string
}

export interface InvoiceDetail extends Invoice {
  items: InvoiceItem[]
  balance_before?: string
  balance_after?: string
}

export interface PaginatedInvoices {
  items: Invoice[]
  total: number
  page: number
  per_page: number
}

export interface PayBalanceResult {
  balance_before: string
  balance_after: string
  paid_amount: string
}

export interface PayOnlineResult {
  qr_code_url: string
  trade_no: string
}

// ── API functions ─────────────────────────────────────────────────────────

export async function listInvoices(params: {
  page?: number
  per_page?: number
  status?: string
  distributor_id?: number
  year?: number
  month?: number
}): Promise<PaginatedInvoices> {
  const res = await request.get<{ data: PaginatedInvoices }>('/invoices', { params })
  return res.data.data
}

export async function getInvoice(id: number): Promise<InvoiceDetail> {
  const res = await request.get<{ data: InvoiceDetail }>(`/invoices/${id}`)
  return res.data.data
}

export function getInvoicePdfUrl(id: number): string {
  const baseUrl = import.meta.env.VITE_API_BASE_URL ?? '/api/v1'
  const token = localStorage.getItem('token')
  return `${baseUrl}/invoices/${id}/pdf?token=${token}`
}

export async function payByBalance(id: number): Promise<PayBalanceResult> {
  const res = await request.put<{ data: PayBalanceResult }>(`/invoices/${id}/pay`)
  return res.data.data
}

export async function payOnline(
  id: number,
  method: 'alipay' | 'wechat',
): Promise<PayOnlineResult> {
  const res = await request.post<{ data: PayOnlineResult }>(`/invoices/${id}/pay-online`, { method })
  return res.data.data
}
