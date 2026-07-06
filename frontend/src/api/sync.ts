import request from './request'

// ── Types ──────────────────────────────────────────────────────────────────

export interface SyncConnection {
  id: number
  name: string
  type: 'v10' | 'finance'
  direction: 'upstream' | 'downstream'
  api_base_url: string
  api_key: string
  sync_interval: number
  status: number                    // 1=active, 0=disabled
  last_sync_at: string
  sync_status: 'idle' | 'syncing' | 'error'
  last_error: string
  pending: number
}

export interface SyncStatusResponse {
  connections: SyncConnection[]
  total_connections: number
  mock_mode: boolean
}

export interface SyncTriggerResult {
  connection_id?: number
  success: boolean
  items_synced?: number
  items_created?: number
  items_error?: number
  error?: string
}

export interface SyncLogItem {
  id: number
  connection_id: number
  connection_name: string
  direction: 'inbound' | 'outbound'
  entity_type: string
  action: string
  status: 'success' | 'failed' | 'pending'
  error_message: string
  retry_count: number
  created_at: string
  request_data: string
  response_data: string
}

export interface SyncLogsResponse {
  items: SyncLogItem[]
  total: number
  page: number
  per_page: number
}

export interface SyncLogsQuery {
  connection_id?: number
  entity_type?: string
  status?: string
  direction?: string
  date_from?: string
  date_to?: string
  page?: number
  per_page?: number
}

// ── API functions ──────────────────────────────────────────────────────────

/** GET /api/v1/sync/zjmf/status — connection status + sync stats */
export async function getSyncStatus(): Promise<SyncStatusResponse> {
  const res = await request.get<{ data: SyncStatusResponse }>('/sync/zjmf/status')
  return res.data.data
}

/** POST /api/v1/sync/zjmf/products — trigger product sync */
export async function triggerProductSync(
  connectionId?: number,
): Promise<SyncTriggerResult> {
  const body = connectionId ? { connection_id: connectionId } : {}
  const res = await request.post<{ data: SyncTriggerResult }>('/sync/zjmf/products', body)
  return res.data.data
}

/** POST /api/v1/sync/zjmf/inventory — trigger inventory sync */
export async function triggerInventorySync(
  connectionId?: number,
): Promise<SyncTriggerResult> {
  const body = connectionId ? { connection_id: connectionId } : {}
  const res = await request.post<{ data: SyncTriggerResult }>('/sync/zjmf/inventory', body)
  return res.data.data
}

/** GET /api/v1/sync/logs — sync logs with filters */
export async function getSyncLogs(
  query: SyncLogsQuery = {},
): Promise<SyncLogsResponse> {
  const params: Record<string, string | number> = {}
  if (query.connection_id) params.connection_id = query.connection_id
  if (query.entity_type) params.entity_type = query.entity_type
  if (query.status) params.status = query.status
  if (query.direction) params.direction = query.direction
  if (query.date_from) params.date_from = query.date_from
  if (query.date_to) params.date_to = query.date_to
  if (query.page) params.page = query.page
  if (query.per_page) params.per_page = query.per_page

  const res = await request.get<{ data: SyncLogsResponse }>('/sync/logs', { params })
  return res.data.data
}
