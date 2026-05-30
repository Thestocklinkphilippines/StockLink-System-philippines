import React, { useEffect, useState } from 'react'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import '../styles/logs.css'

const FEED_PAGE_SIZE = 12

function formatTimestamp(ts) {
  if (!ts) return 'Unknown time'
  const d = new Date(ts)
  if (Number.isNaN(d.getTime())) return ts
  return d.toLocaleString()
}

function formatMessage(log) {
  const p = log?.payload || {}
  const type = log?.log_type || 'generic'

  if (type === 'feed_now' || p.event === 'feed_now') {
    return p.status === 'executed' ? 'Manual feed executed' : `Manual feed ${p.status || 'event'}`
  }

  if (type === 'feeding') {
    const amount = p.amount_kg != null ? Number(p.amount_kg).toFixed(3) : '0.000'
    const remaining = p.remaining_kg != null ? Number(p.remaining_kg).toFixed(3) : '0.000'
    return `Dispensed ${amount} kg, remaining ${remaining} kg`
  }

  if (type === 'watering') {
    if (p.event === 'refill_complete') return `Refill complete (${Number(p.water_level_pct || 0).toFixed(1)}%)`
    if (p.event === 'refill') return `Refill attempted (${Number(p.water_level_pct || 0).toFixed(1)}%)`
    return 'Watering event recorded'
  }

  if (type === 'power') {
    if (p.event === 'mains_restored') return 'Mains power restored'
    return 'Power event recorded'
  }

  if (type === 'device_connection_loss' || p.event === 'connection_lost') {
    return 'Device connection lost'
  }

  if (type === 'device_connection_restored' || p.event === 'connection_restored') {
    return 'Device connection restored'
  }

  if (type === 'ui') {
    return `Keypad input: ${p.key || '?'} from ${p.source || 'device'}`
  }

  if (type === 'heartbeat') {
    const up = p.uptime_ms != null ? `${Math.floor(Number(p.uptime_ms) / 1000)}s uptime` : 'alive'
    return `Heartbeat (${up})`
  }

  if (p.event) return `${type}: ${p.event}`
  return type
}

function formatDetails(log) {
  const p = log?.payload || {}
  const bits = []
  if (p.feeder_level_pct != null) bits.push(`Feeder ${Number(p.feeder_level_pct).toFixed(1)}%`)
  if (p.water_level_pct != null) bits.push(`Water ${Number(p.water_level_pct).toFixed(1)}%`)
  if (p.battery_voltage_v != null) bits.push(`Battery ${Number(p.battery_voltage_v).toFixed(2)}V`)
  if (p.simulated != null) bits.push(p.simulated ? 'Simulated' : 'Live')
  if (p.wifi_connected != null) bits.push(p.wifi_connected ? 'Wi-Fi online' : 'Wi-Fi offline')
  if (p.offline_seconds != null) bits.push(`Offline ${Number(p.offline_seconds).toFixed(0)}s`)
  if (p.trigger) bits.push(`Trigger ${p.trigger}`)
  return bits.join(' | ')
}

function parseDateMs(value) {
  const date = value ? new Date(value) : null
  const time = date && !Number.isNaN(date.getTime()) ? date.getTime() : null
  return time
}

function isManualFeedCommand(command) {
  return Boolean(command && (command.status === 'executed' || command.status === 'failed'))
}

function matchesManualFeedCommand(log, feedNowCommands = []) {
  const p = log?.payload || {}
  const amountKg = p.amount_kg != null ? Number(p.amount_kg) : null
  const logTime = parseDateMs(log?.timestamp)
  if (!Number.isFinite(amountKg) || logTime == null) return false

  const matches = feedNowCommands.filter((command) => {
    if (!isManualFeedCommand(command)) return false
    const commandAmount = Number(command?.amount_kg)
    if (!Number.isFinite(commandAmount) || Math.abs(commandAmount - amountKg) >= 0.001) return false

    const commandTime = parseDateMs(command?.executed_at || command?.updated_at || command?.created_at)
    if (commandTime == null) return false

    return Math.abs(commandTime - logTime) <= 15 * 60 * 1000
  })

  if (matches.length === 0) return false
  if (p.command_id != null) return matches.some((command) => String(command.id) === String(p.command_id))
  return true
}

function isManualTrigger(log) {
  const p = log?.payload || {}
  return String(p.trigger || '').trim().toLowerCase() === 'manual'
}

function isManualFeedLog(log, feedNowCommands = []) {
  const type = String(log?.log_type || '').toLowerCase()
  const p = log?.payload || {}
  return type === 'feed_now' || p.event === 'feed_now' || p.command_id != null || p.status === 'executed' || p.status === 'failed' || isManualTrigger(log) || matchesManualFeedCommand(log, feedNowCommands)
}

function formatFeedHeadline(log, feedNowCommands = []) {
  const type = String(log?.log_type || '').toLowerCase()
  const p = log?.payload || {}

  if (isManualFeedLog(log, feedNowCommands)) {
    if (isManualTrigger(log)) {
      return 'Manual feed event'
    }

    if (p.status) {
      return p.status === 'executed' ? 'Manual feed executed' : `Manual feed ${p.status}`
    }
    return 'Manual feed event'
  }

  if (type === 'feeding') {
    return 'Scheduled feeding'
  }

  if (p.event) {
    return `Feeding event: ${p.event}`
  }

  return 'Feed dispensed'
}

function formatFeedSummary(log) {
  const p = log?.payload || {}
  const summaryBits = []

  if (p.amount_kg != null) summaryBits.push(`Dispensed ${Number(p.amount_kg).toFixed(3)} kg`)
  if (p.remaining_kg != null) summaryBits.push(`Remaining ${Number(p.remaining_kg).toFixed(3)} kg`)
  if (p.status) summaryBits.push(`Status ${p.status}`)
  if (p.command_id != null) summaryBits.push(`Command #${p.command_id}`)
  if (p.simulated != null) summaryBits.push(p.simulated ? 'Simulated' : 'Live')

  return summaryBits.length > 0 ? summaryBits.join(' · ') : formatDetails(log) || 'Feed event recorded'
}

export default function Logs() {
  const pollingIntervalMs = getPollingIntervalMs()
  const [logs, setLogs] = useState([])
  const [schedules, setSchedules] = useState([])
  const [feedNowCommands, setFeedNowCommands] = useState([])
  const [deviceId, setDeviceId] = useState('esp32-001')
  const [feedPage, setFeedPage] = useState(1)

  useEffect(() => {
    let isMounted = true

    const refresh = async () => {
      const active = await api.getActiveDeviceId()
      if (!isMounted) return
      setDeviceId(active)

      const [logsRes, schedulesRes, feedNowRes] = await Promise.all([
        api.getJSON(`/api/device/${active}/logs/`),
        api.getJSON(`/api/device/${active}/schedules/`),
        api.getJSON(`/api/device/${active}/feed-now/`),
      ])

      if (isMounted && logsRes.ok) setLogs(logsRes.body)
      if (isMounted && schedulesRes.ok) setSchedules(schedulesRes.body || [])
      if (isMounted && feedNowRes.ok) setFeedNowCommands(feedNowRes.body || [])
    }

    refresh()
    const timer = setInterval(refresh, pollingIntervalMs)

    return () => {
      isMounted = false
      clearInterval(timer)
    }
  }, [pollingIntervalMs])

  const heartbeatLog = logs.find(l => String(l.log_type || '').toLowerCase() === 'heartbeat')
  const feedLogs = logs.filter(l => {
    const type = String(l.log_type || '').toLowerCase()
    return type === 'feeding' || type === 'feed_now'
  })
  const feedPageCount = Math.max(1, Math.ceil(feedLogs.length / FEED_PAGE_SIZE))
  const safeFeedPage = Math.min(feedPage, feedPageCount)
  const feedPageStart = (safeFeedPage - 1) * FEED_PAGE_SIZE
  const pagedFeedLogs = feedLogs.slice(feedPageStart, feedPageStart + FEED_PAGE_SIZE)

  useEffect(() => {
    if (feedPage > feedPageCount) {
      setFeedPage(feedPageCount)
    }
  }, [feedPage, feedPageCount])

  const nonStackingTypes = ['watering', 'power', 'power_outage', 'power_restored']
  const uniqueNonStackingLogs = []
  const seenTypes = new Set()
  for (const l of logs) {
    const t = String(l.log_type || '').toLowerCase()
    if (nonStackingTypes.includes(t) && !seenTypes.has(t)) {
      uniqueNonStackingLogs.push(l)
      seenTypes.add(t)
    }
  }

  const currentLogs = logs.filter(l => !nonStackingTypes.includes(String(l.log_type || '').toLowerCase()) && String(l.log_type || '').toLowerCase() !== 'feeding' && String(l.log_type || '').toLowerCase() !== 'feed_now' && String(l.log_type || '').toLowerCase() !== 'heartbeat')

  const normalizeTimeParts = (value) => {
    if (!value) return []
    const date = value instanceof Date ? value : new Date(value)
    if (Number.isNaN(date.getTime())) return []

    const parts = []
    parts.push(date.toISOString().slice(11, 16))
    parts.push(date.toLocaleTimeString('en-GB', { hour12: false, hour: '2-digit', minute: '2-digit' }))

    return Array.from(new Set(parts.filter(Boolean)))
  }

  const resolveScheduledFeedName = (log) => {
    const p = log?.payload || {}
    if (isManualFeedLog(log, feedNowCommands)) return 'Manual feed'
    if (p.schedule_name) return String(p.schedule_name)

    if (p.schedule_id != null) {
      const byId = schedules.find((schedule) => String(schedule?.id) === String(p.schedule_id))
      if (byId?.schedule_name) return String(byId.schedule_name)
    }

    const amountKg = p.amount_kg != null ? Number(p.amount_kg) : null
    if (!Number.isFinite(amountKg)) return ''

    const timestamp = new Date(log?.timestamp)
    const logTimeParts = normalizeTimeParts(timestamp)

    const candidates = schedules.filter((schedule) => {
      if (!schedule || !schedule.time) return false
      const scheduleName = schedule.schedule_name ? String(schedule.schedule_name) : ''
      if (!scheduleName) return false
      const scheduleAmount = Number(schedule.feeding_amount_kg)
      if (!Number.isFinite(scheduleAmount)) return false
      const amountMatches = Math.abs(scheduleAmount - amountKg) < 0.001
      const scheduleTime = String(schedule.time).slice(0, 5)
      const timeMatches = logTimeParts.includes(scheduleTime)
      return amountMatches && timeMatches
    })

    if (candidates.length === 1) return candidates[0].schedule_name || ''

    const amountOnlyMatches = schedules.filter((schedule) => {
      const scheduleAmount = Number(schedule?.feeding_amount_kg)
      return Number.isFinite(scheduleAmount) && Math.abs(scheduleAmount - amountKg) < 0.001 && schedule?.schedule_name
    })

    if (amountOnlyMatches.length === 1) return String(amountOnlyMatches[0].schedule_name)

    if (candidates.length > 1) return String(candidates[0].schedule_name || 'Scheduled feed')

    if (amountOnlyMatches.length > 1) return String(amountOnlyMatches[0].schedule_name || 'Scheduled feed')

    if (schedules.length === 1 && schedules[0]?.schedule_name) return String(schedules[0].schedule_name)

    return 'Scheduled feed'
  }

  const resolveExportRowName = (log) => {
    if (isManualFeedLog(log, feedNowCommands)) return 'Manual feed'
    return resolveScheduledFeedName(log)
  }

  const exportFeedLogsCSV = (feedLogsToExport) => {
    if (!Array.isArray(feedLogsToExport) || feedLogsToExport.length === 0) return

    const csvEscape = (v) => {
      if (v == null) return '""'
      const s = String(v)
      return '"' + s.replace(/"/g, '""') + '"'
    }

    const rows = []
    rows.push(['Time', 'Schedule', 'Amount_kg', 'Type'])

    for (const log of feedLogsToExport) {
      const p = log?.payload || {}
      const ts = log?.timestamp
      const timeVal = (() => {
        try {
          const d = new Date(ts)
          return Number.isNaN(d.getTime()) ? ts : d.toISOString()
        } catch (e) {
          return ts
        }
      })()

      const name = resolveExportRowName(log)

      let amount = ''
      if (p.amount_kg != null) amount = Number(p.amount_kg).toFixed(3)
      else if (p.amount_g != null) amount = (Number(p.amount_g) / 1000).toFixed(3)

      const t = String(log?.log_type || '').toLowerCase()
      const typeVal = (t === 'feed_now' || p.event === 'feed_now') ? 'manual' : 'schedule'

      rows.push([timeVal, name, amount, typeVal])
    }

    const csv = rows.map(r => r.map(csvEscape).join(',')).join('\r\n')
    const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `${deviceId || 'device'}-feed-logs.csv`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
  }

  const feedPageItems = (() => {
    if (feedPageCount <= 7) {
      return Array.from({ length: feedPageCount }, (_, index) => index + 1)
    }

    if (safeFeedPage <= 4) {
      return [1, 2, 3, 4, 5, 'ellipsis-end', feedPageCount]
    }

    if (safeFeedPage >= feedPageCount - 3) {
      return [1, 'ellipsis-start', feedPageCount - 4, feedPageCount - 3, feedPageCount - 2, feedPageCount - 1, feedPageCount]
    }

    return [1, 'ellipsis-start', safeFeedPage - 1, safeFeedPage, safeFeedPage + 1, 'ellipsis-end', feedPageCount]
  })()

  return (
    <section className="logs-page">
      <div className="logs-hero">
        <div>
          <p className="logs-eyebrow">Append-only feed history</p>
          <h3 className="logs-title">Feeding Logs</h3>
          <p className="logs-subtitle">Feeding events now stack again so every dispense remains visible. Current live logs stay in the sidebar.</p>
        </div>
        <div className="logs-hero-stat">
          <span>Feed events</span>
          <strong>{feedLogs.length}</strong>
        </div>
        <div className="logs-hero-actions">
          <button className="logs-export-btn" onClick={() => exportFeedLogsCSV(feedLogs)}>Export CSV</button>
        </div>
      </div>

      <div className="logs-layout">
        <article className="logs-main-card">
          <div className="logs-section-head">
            <div>
              <h4>Feed Event History</h4>
              <p>Each feeding action is appended here in time order.</p>
            </div>
            <div className="logs-feed-page-meta">
              <span>{feedLogs.length} total</span>
              <strong>
                {feedLogs.length === 0 ? 'Page 0 of 0' : `Page ${safeFeedPage} of ${feedPageCount}`}
              </strong>
            </div>
          </div>

          {feedLogs.length === 0 ? (
            <div className="logs-empty logs-empty-main">No feeding events yet. They will accumulate here once the device starts dispensing feed.</div>
          ) : (
            <>
              <div className="logs-feed-pagination" aria-label="Feed history pagination">
                <button
                  type="button"
                  className="logs-feed-page-turner"
                  onClick={() => setFeedPage(1)}
                  disabled={safeFeedPage === 1}
                >
                  First
                </button>
                <button
                  type="button"
                  className="logs-feed-page-turner"
                  onClick={() => setFeedPage((current) => Math.max(1, current - 1))}
                  disabled={safeFeedPage === 1}
                >
                  Prev
                </button>

                <div className="logs-feed-page-buttons">
                  {feedPageItems.map((item) => (
                    item === 'ellipsis-start' || item === 'ellipsis-end' ? (
                      <span key={item === 'ellipsis-start' ? 'feed-ellipsis-start' : 'feed-ellipsis-end'} className="logs-feed-page-ellipsis">…</span>
                    ) : (
                      <button
                        key={item}
                        type="button"
                        className={`logs-feed-page-turner logs-feed-page-number${item === safeFeedPage ? ' is-active' : ''}`}
                        onClick={() => setFeedPage(item)}
                        aria-current={item === safeFeedPage ? 'page' : undefined}
                      >
                        {item}
                      </button>
                    )
                  ))}
                </div>

                <button
                  type="button"
                  className="logs-feed-page-turner"
                  onClick={() => setFeedPage((current) => Math.min(feedPageCount, current + 1))}
                  disabled={safeFeedPage === feedPageCount}
                >
                  Next
                </button>
                <button
                  type="button"
                  className="logs-feed-page-turner"
                  onClick={() => setFeedPage(feedPageCount)}
                  disabled={safeFeedPage === feedPageCount}
                >
                  Last
                </button>
              </div>

              <ul className="logs-feed-list">
                {pagedFeedLogs.map((log) => (
                  <li key={log.id} className="logs-feed-item">
                    <div className="logs-feed-item-head">
                      <div>
                        <span className="logs-feed-item-type">{formatFeedHeadline(log, feedNowCommands)}</span>
                        <span className="logs-feed-item-summary">{formatFeedSummary(log)}</span>
                      </div>
                      <span className="logs-feed-item-time">{formatTimestamp(log.timestamp)}</span>
                    </div>
                    {formatDetails(log) ? <div className="logs-feed-item-details">{formatDetails(log)}</div> : null}
                  </li>
                ))}
              </ul>
            </>
          )}
        </article>

        <article className="logs-main-card logs-current-logs-card">
          <div className="logs-section-head">
            <div>
              <h4>Current Logs</h4>
              <p>Non-feeding live updates.</p>
            </div>
            <strong className="logs-sidebar-count">{currentLogs.length}</strong>
          </div>

          {currentLogs.length === 0 ? (
            <div className="logs-empty logs-empty-main">No current logs.</div>
          ) : (
            <ul className="logs-current-list">
              {currentLogs.slice(0, 10).map((log) => (
                <li key={log.id} className="logs-current-item">
                  <div className="logs-current-item-head">
                    <span className="logs-current-item-type">{formatMessage(log)}</span>
                    <span className="logs-current-item-time">{formatTimestamp(log.timestamp)}</span>
                  </div>
                  <div className="logs-current-item-details">{formatDetails(log) || log.log_type}</div>
                </li>
              ))}
            </ul>
          )}
        </article>

        <aside className="logs-sidebar logs-sidebar-compact">
          <article className="logs-sidebar-card logs-heartbeat-card logs-featured-card">
            <div className="logs-section-head logs-section-head-compact">
              <div>
                <h4>Current Signal</h4>
                <p>Latest heartbeat from the device.</p>
              </div>
            </div>
            <div className="logs-heartbeat-value">
              {heartbeatLog ? formatTimestamp(heartbeatLog.timestamp) : 'No heartbeat yet'}
            </div>
          </article>

          <article className="logs-sidebar-card">
            <div className="logs-section-head logs-section-head-compact">
              <div>
                <h4>Status Events</h4>
                <p>Non-stacking events (latest per type).</p>
              </div>
              <strong className="logs-sidebar-count">{uniqueNonStackingLogs.length}</strong>
            </div>

            {uniqueNonStackingLogs.length === 0 ? (
              <div className="logs-empty logs-empty-sidebar">No status events.</div>
            ) : (
              <ul className="logs-current-list">
                {uniqueNonStackingLogs.map((log) => (
                  <li key={log.id} className="logs-current-item">
                    <div className="logs-current-item-head">
                      <span className="logs-current-item-type">{formatMessage(log)}</span>
                      <span className="logs-current-item-time">{formatTimestamp(log.timestamp)}</span>
                    </div>
                    <div className="logs-current-item-details">{formatDetails(log) || log.log_type}</div>
                  </li>
                ))}
              </ul>
            )}
          </article>
        </aside>
      </div>
    </section>
  )
}
