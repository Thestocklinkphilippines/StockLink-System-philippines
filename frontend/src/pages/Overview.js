import React, { useEffect, useState } from 'react'
import { Link } from 'react-router-dom'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import '../styles/overview.css'

export default function Overview(){
  const pollingIntervalMs = getPollingIntervalMs()
  const [cfg, setCfg] = useState(null)
  const [schedules, setSchedules] = useState([])
  const [deviceId, setDeviceId] = useState('esp32-001')
  const [lastRefreshAt, setLastRefreshAt] = useState('')

  useEffect(()=>{
    let isMounted = true

    const refresh = async () => {
      const active = await api.getActiveDeviceId()
      if (!isMounted) return
      setDeviceId(active)

      const configRes = await api.getJSON(`/api/device/${active}/config/`)
      if (isMounted && configRes.ok) {
        setCfg(configRes.body)
        setLastRefreshAt(new Date().toLocaleTimeString())
      }

      const schedulesRes = await api.getJSON(`/api/device/${active}/schedules/`)
      if (isMounted && schedulesRes.ok) setSchedules(schedulesRes.body || [])
    }

    refresh()
    const timer = setInterval(refresh, pollingIntervalMs)

    return () => {
      isMounted = false
      clearInterval(timer)
    }
  },[pollingIntervalMs])

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

  return (
    <section className="overview-page">
      <h3 className="overview-title">Overview</h3>
      <p className="overview-subtitle">Device: {deviceId}</p>
      <p className="overview-live-note">Auto-refresh: every {(pollingIntervalMs / 1000).toFixed(1)}s {lastRefreshAt ? `(last ${lastRefreshAt})` : ''}</p>
      <div className="overview-grid">
        <article className="overview-stat-card">
          <h4>Last updated</h4>
          <p>{cfg.last_updated}</p>
        </article>
        <article className="overview-stat-card">
          <h4>Updated by</h4>
          <p>{cfg.updated_by || 'System'}</p>
        </article>
        <article className="overview-stat-card">
          <h4>Schedules</h4>
          <p>{schedules.length}</p>
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
    </section>
  )
}
