import request from './request'

// ── Types ──────────────────────────────────────────────────────────────────

export interface BalanceInfo {
  balance: string
  credit_limit: string
  available_credit: string
}

export interface TopUpResult {
  trade_no: string
  qr_code_url?: string
  amount: string
}

// ── API functions ─────────────────────────────────────────────────────────

export async function getBalance(): Promise<BalanceInfo> {
  const res = await request.get<{ data: BalanceInfo }>('/distributors/balance')
  return res.data.data
}

export async function topUp(data: {
  amount: string
  method: 'alipay' | 'wechat'
}): Promise<TopUpResult> {
  const res = await request.post<{ data: TopUpResult }>('/distributors/top-up', data)
  return res.data.data
}
