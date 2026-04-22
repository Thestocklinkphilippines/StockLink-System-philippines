import React, { useEffect, useState } from 'react'
import { Link } from 'react-router-dom'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import { formatDateTime } from '../utils/datetime'
import '../styles/overview.css'

export default function Overview(){
  const pollingIntervalMs = getPollingIntervalMs()
  const [cfg, setCfg] = useState(null)
  const [schedules, setSchedules] = useState([])
  const [alerts, setAlerts] = useState([])
  const [logs, setLogs] = useState([])
  const [deviceId, setDeviceId] = useState('esp32-001')
  const [lastRefreshAt, setLastRefreshAt] = useState('')

  useEffect(()=>{
    let isMounted = true

    const refresh = async () => {
      const active = await api.getActiveDeviceId()
      if (!isMounted) return
      setDeviceId(active)

      const [configRes, schedulesRes, alertsRes, logsRes] = await Promise.all([
        api.getJSON(`/api/device/${active}/config/`),
        api.getJSON(`/api/device/${active}/schedules/`),
        api.getJSON(`/api/device/${active}/alerts/`),
        api.getJSON(`/api/device/${active}/logs/`),
      ])

      if (!isMounted) return

      if (configRes.ok) setCfg(configRes.body)
      if (schedulesRes.ok) setSchedules(schedulesRes.body || [])
      if (alertsRes.ok) setAlerts(alertsRes.body || [])
      if (logsRes.ok) setLogs(logsRes.body || [])

      if (configRes.ok || schedulesRes.ok || alertsRes.ok || logsRes.ok) {
        setLastRefreshAt(new Date().toLocaleTimeString())
      }
    }

    refresh()
    const timer = setInterval(refresh, pollingIntervalMs)

    return () => {
      isMounted = false
      clearInterval(timer)
    }
  }, [pollingIntervalMs])

  if (!cfg) return <div className="overview-loading">Loading...</div>

  const sensorState = cfg.sensor_state || {}
  const config = cfg.config || {}
  const feederLevelPct = sensorState.feeder_level_pct ?? 100
  const waterLevelPct = sensorState.water_level_pct ?? 100
  const feederLowThresh = config.feeder_low_threshold_pct ?? 20
  const feederHighThresh = config.feeder_high_threshold_pct ?? 80
  const waterLowThresh = config.water_low_threshold_pct ?? 20
  const waterHighThresh = config.water_high_threshold_pct ?? 80

  const getStatusClass = (level, low, high) => {
    if (level <= low) return 'status-critical'
    if (level >= high) return 'status-good'
    return 'status-warning'
  }

  const feederStatus = getStatusClass(feederLevelPct, feederLowThresh, feederHighThresh)
  const waterStatus = getStatusClass(waterLevelPct, waterLowThresh, waterHighThresh)

  const totalDispensedKg = Number(config.total_feeds_today_kg ?? 0)
  const displayTotalDispensedKg = Number.isFinite(totalDispensedKg) ? totalDispensedKg : 0
  const pendingAlerts = alerts.filter(alertItem => !alertItem.resolved)
  const heartbeatLogs = logs.filter(logEntry => String(logEntry.log_type || '').toLowerCase() === 'heartbeat')
  const latestHeartbeat = heartbeatLogs[0]
  const heartbeatPulseKey = latestHeartbeat ? String(latestHeartbeat.timestamp || latestHeartbeat.last_updated || latestHeartbeat.id || '') : 'none'
  const latestAlert = alerts[0]

  return (
    <section className="overview-page">
      <h3 className="overview-title">Overview</h3>
      <p className="overview-subtitle">Device: {deviceId}</p>
      <p className="overview-live-note">Auto-refresh: every {(pollingIntervalMs / 1000).toFixed(1)}s {lastRefreshAt ? `(last ${lastRefreshAt})` : ''}</p>

      <div className="overview-layout-grid">
        <div>
          <div className="overview-grid overview-stats-grid">
            <article className="overview-stat-card">
              <h4>Last updated</h4>
              <p>{formatDateTime(cfg.last_updated)}</p>
            </article>
            <article className="overview-stat-card">
              <h4>Updated by</h4>
              <p>{cfg.updated_by || 'System'}</p>
            </article>
            <article className="overview-stat-card">
              <h4>Schedules</h4>
              <p>{schedules.length}</p>
            </article>
            <article className="overview-stat-card">
              <h4>Total Dispensed</h4>
              <p>{displayTotalDispensedKg.toFixed(3)}kg</p>
            </article>
          </div>

          <div className="overview-grid overview-sensor-grid">
            <article className={`overview-sensor-card ${feederStatus}`}>
              <h4>Feeder Tank</h4>
              <div className="sensor-level-display">
                <div className="sensor-level-bar">
                  <div className="sensor-level-fill" style={{width: `${feederLevelPct}%`}}></div>
                </div>
                <p className="sensor-level-pct">{feederLevelPct.toFixed(1)}%</p>
              </div>
              <div className="sensor-thresholds">
                <span className="sensor-threshold-low">Low: {feederLowThresh}%</span>
                <span className="sensor-threshold-high">High: {feederHighThresh}%</span>
              </div>
              {sensorState.last_reported_at ? (
                <p className="sensor-last-report">Last: {new Date(sensorState.last_reported_at).toLocaleString()}</p>
              ) : (
                <p className="sensor-last-report">No reports yet</p>
              )}
            </article>

            <article className={`overview-sensor-card ${waterStatus}`}>
              <h4>Water Tank</h4>
              <div className="sensor-level-display">
                <div className="sensor-level-bar">
                  <div className="sensor-level-fill" style={{width: `${waterLevelPct}%`}}></div>
                </div>
                <p className="sensor-level-pct">{waterLevelPct.toFixed(1)}%</p>
              </div>
              <div className="sensor-thresholds">
                <span className="sensor-threshold-low">Low: {waterLowThresh}% (refill)</span>
                <span className="sensor-threshold-high">High: {waterHighThresh}% (stop)</span>
              </div>
              {sensorState.last_reported_at ? (
                <p className="sensor-last-report">Last: {new Date(sensorState.last_reported_at).toLocaleString()}</p>
              ) : (
                <p className="sensor-last-report">No reports yet</p>
              )}
            </article>
          </div>

          <article className="overview-schedules-card">
            <div className="overview-schedules-head">
              <div>
                <h4>Scheduled Feedings</h4>
                <p>Read-only preview from Schedule Management</p>
              </div>
              <Link to="/dashboard/schedules" className="overview-manage-link">
                Open Schedule Management
              </Link>
            </div>

            {schedules.length === 0 ? (
              <div className="overview-schedules-empty">No schedules available.</div>
            ) : (
              <ul className="overview-schedules-list">
                {schedules.map((schedule) => (
                  <li key={schedule.id} className="overview-schedule-item">
                    <div>
                      <p className="overview-schedule-name">{schedule.schedule_name}</p>
                      <p className="overview-schedule-days">{(schedule.days && schedule.days.join(', ')) || 'Daily'}</p>
                    </div>
                    <div className="overview-schedule-meta">
                      <span>{schedule.time || '-'}</span>
                      <span>{schedule.feeding_amount_kg}kg</span>
                      <span className={`overview-schedule-status ${schedule.enabled ? 'overview-schedule-status-on' : 'overview-schedule-status-off'}`}>
                        {schedule.enabled ? 'Active' : 'Paused'}
                      </span>
                    </div>
                  </li>
                ))}
              </ul>
            )}
          </article>
        </div>

        <aside className="overview-right-panel">
          <article className="overview-side-card">
            <div className="overview-side-head">
              <h4>Alerts</h4>
              <Link to="/dashboard/alerts" className="overview-side-link">Open</Link>
            </div>
            <div className="overview-side-kpi-row">
              <div>
                <span className="overview-side-kpi-label">Pending</span>
                <strong className={`overview-side-kpi ${pendingAlerts.length > 0 ? 'overview-side-kpi-warn' : ''}`}>{pendingAlerts.length}</strong>
              </div>
              <div>
                <span className="overview-side-kpi-label">Total</span>
                <strong className="overview-side-kpi">{alerts.length}</strong>
              </div>
            </div>
            <p className="overview-side-note">
              {latestAlert ? `Latest: ${latestAlert.alert_type} at ${formatDateTime(latestAlert.timestamp, 'Unknown time')}` : 'No alerts recorded yet.'}
            </p>
          </article>

          <article className="overview-side-card">
            <div className="overview-side-head">
              <h4>Logs & Heartbeats</h4>
              <Link to="/dashboard/logs" className="overview-side-link">Open</Link>
            </div>
            <div className="overview-side-kpi-row">
              <div>
                <span className="overview-side-kpi-label">Logs</span>
                <strong className="overview-side-kpi">{logs.length}</strong>
              </div>
              <div>
                <span className="overview-side-kpi-label">Heartbeat</span>
                {latestHeartbeat ? (
                  <strong key={heartbeatPulseKey} className="overview-side-kpi overview-heartbeat-pulse">Beat</strong>
                ) : (
                  <strong className="overview-side-kpi overview-heartbeat-idle">No Signal</strong>
                )}
              </div>
            </div>
            <p className="overview-side-note">
              {latestHeartbeat ? `Latest heartbeat: ${formatDateTime(latestHeartbeat.timestamp, 'Unknown time')}` : 'No heartbeat logs yet.'}
            </p>
          </article>
        </aside>
      </div>
    </section>
  )
}
