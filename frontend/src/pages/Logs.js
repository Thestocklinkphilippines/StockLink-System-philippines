import React, { useEffect, useState } from 'react'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import '../styles/logs.css'

function formatTimestamp(ts) {
  if (!ts) return 'Unknown time'
  const d = new Date(ts)
  if (Number.isNaN(d.getTime())) return ts
  return d.toLocaleString()
}

function formatMessage(log) {
  const p = log?.payload || {}
  const type = log?.log_type || 'generic'

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
  if (p.simulated != null) bits.push(p.simulated ? 'Simulated' : 'Live')
  if (p.wifi_connected != null) bits.push(p.wifi_connected ? 'Wi-Fi online' : 'Wi-Fi offline')
  return bits.join(' | ')
}

export default function Logs(){
  const pollingIntervalMs = getPollingIntervalMs()
  const [logs, setLogs] = useState([])
  const [deviceId, setDeviceId] = useState('esp32-001')

  useEffect(()=>{
    let isMounted = true

    const refresh = async () => {
      const active = await api.getActiveDeviceId()
      if (!isMounted) return
      setDeviceId(active)

      const res = await api.getJSON(`/api/device/${active}/logs/`)
      if (isMounted && res.ok) setLogs(res.body)
    }

    refresh()
    const timer = setInterval(refresh, pollingIntervalMs)

    return () => {
      isMounted = false
      clearInterval(timer)
    }
  },[pollingIntervalMs])

  const heartbeatLog = logs.find(l => l.log_type === 'heartbeat')
  const displayLogs = logs.filter(l => l.log_type !== 'heartbeat')

  return (
    <section className="logs-page">
      <div className="logs-title-row">
        <h3 className="logs-title">Feeding Logs ({deviceId})</h3>
        <div className="logs-heartbeat">
          Heartbeat: {heartbeatLog ? formatTimestamp(heartbeatLog.timestamp) : 'No heartbeat yet'}
        </div>
      </div>
      <div className="logs-card">
        {displayLogs.length===0 ? <div className="logs-empty">No logs yet.</div> : (
          <ul className="logs-list">
            {displayLogs.map(l=> (
              <li key={l.id} className="logs-item">
                <div className="logs-item-head">
                  <span className="logs-item-type">{formatMessage(l)}</span>
                  <span className="logs-item-time">{formatTimestamp(l.timestamp)}</span>
                </div>
                <div className="logs-item-details">{formatDetails(l)}</div>
              </li>
            ))}
          </ul>
        )}
      </div>
    </section>
  )
}
