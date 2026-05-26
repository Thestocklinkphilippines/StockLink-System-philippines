import React, { useEffect, useState } from 'react'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import { formatDateTime } from '../utils/datetime'
import '../styles/alerts.css'

function humanizeAlertType(value) {
  const raw = String(value || '').trim()
  if (!raw) return 'Unknown Alert'

  return raw
    .replace(/[_-]+/g, ' ')
    .replace(/\s+/g, ' ')
    .split(' ')
    .map(token => token.charAt(0).toUpperCase() + token.slice(1).toLowerCase())
    .join(' ')
}

export default function Alerts(){
  const pollingIntervalMs = getPollingIntervalMs()
  const [alerts, setAlerts] = useState([])
  const [deviceId, setDeviceId] = useState('esp32-001')

  useEffect(()=>{
    let isMounted = true

    const refresh = async () => {
      const active = await api.getActiveDeviceId()
      if (!isMounted) return
      setDeviceId(active)

      const res = await api.getJSON(`/api/device/${active}/alerts/`)
      if (isMounted && res.ok) setAlerts(res.body)
    }

    refresh()
    const timer = setInterval(refresh, pollingIntervalMs)

    return () => {
      isMounted = false
      clearInterval(timer)
    }
  },[pollingIntervalMs])

  return (
    <section className="alerts-page">
      <h3 className="alerts-title">Notifications & Alerts</h3>
      <div className="alerts-card">
        {alerts.length===0 ? <div className="alerts-empty">No alerts.</div> : (
          <ul className="alerts-list">
            {alerts.map(a=> (
              <li key={a.id} className={`alerts-item ${a.resolved ? 'alerts-item-resolved' : 'alerts-item-active'}`}>
                <span className="alerts-item-type">{humanizeAlertType(a.alert_type)}</span>
                <span className="alerts-item-time">{formatDateTime(a.timestamp, 'Unknown time')}</span>
                <span className={`alerts-item-status ${a.resolved ? 'ok' : 'warn'}`}>{a.resolved ? 'Resolved' : 'Active'}</span>
              </li>
            ))}
          </ul>
        )}
      </div>
    </section>
  )
}
