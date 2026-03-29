import React, { useEffect, useState } from 'react'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import '../styles/alerts.css'

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
      <h3 className="alerts-title">Notifications & Alerts ({deviceId})</h3>
      <div className="alerts-card">
        {alerts.length===0 ? <div className="alerts-empty">No alerts.</div> : (
          <ul className="alerts-list">
            {alerts.map(a=> (
              <li key={a.id} className="alerts-item">
                <span className="alerts-item-type">{a.alert_type}</span>
                <span className="alerts-item-time">{a.timestamp}</span>
                <span className={`alerts-item-status ${a.resolved ? 'ok' : 'warn'}`}>{a.resolved ? 'Resolved' : 'Pending'}</span>
              </li>
            ))}
          </ul>
        )}
      </div>
    </section>
  )
}
