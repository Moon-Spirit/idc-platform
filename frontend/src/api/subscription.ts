import request from './request'

// ── Types ──────────────────────────────────────────────────────────────────

export interface BandwidthDataPoint {
  time: string
  bytes_in: number
  bytes_out: number
  in_rate_mbps: number
  out_rate_mbps: number
}

export interface BandwidthData {
  port_id: string
  data_points: BandwidthDataPoint[]
  monthly_95th: number
  bandwidth_limit: number
}

export interface Subscription {
  id: number
  sub_no: string
  product_name: string
  product_type?: string
  status: string
}

export interface PaginatedSubscriptions {
  items: Subscription[]
  total: number
  page: number
  per_page: number
}

// ── API functions ─────────────────────────────────────────────────────────

export async function listSubscriptions(params: {
  page?: number
  per_page?: number
  status?: string
}): Promise<PaginatedSubscriptions> {
  const res = await request.get<{ data: PaginatedSubscriptions }>('/subscriptions', { params })
  return res.data.data
}

export async function getBandwidthData(
  subId: number,
  params: {
    start_date?: string
    end_date?: string
    granularity?: string
  },
): Promise<BandwidthData> {
  const res = await request.get<{ data: BandwidthData }>(`/subscriptions/${subId}/bandwidth`, { params })
  return res.data.data
}
