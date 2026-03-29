import React, { useEffect, useState } from 'react'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import '../styles/settings.css'

export default function Settings(){
  const pollingIntervalMs = getPollingIntervalMs()
  const [loading, setLoading] = useState(true)
  const [saving, setSaving] = useState(false)
  const [savingCapacity, setSavingCapacity] = useState(false)
  const [savingThresholds, setSavingThresholds] = useState(false)
  const [timezone, setTimezone] = useState('')
  const [maxFeedsCapacityKg, setMaxFeedsCapacityKg] = useState('')
  const [maxFeedsCapacityUpdatedAt, setMaxFeedsCapacityUpdatedAt] = useState('')
  const [feederLowThresholdPct, setFeederLowThresholdPct] = useState('')
  const [feederHighThresholdPct, setFeederHighThresholdPct] = useState('')
  const [waterLowThresholdPct, setWaterLowThresholdPct] = useState('')
  const [waterHighThresholdPct, setWaterHighThresholdPct] = useState('')
  const [timezoneOptions, setTimezoneOptions] = useState([])
  const [serverTimeLocal, setServerTimeLocal] = useState('')
  const [serverTimeUtc, setServerTimeUtc] = useState('')
  const [message, setMessage] = useState('')
  const [error, setError] = useState('')
  const [hasLocalEdits, setHasLocalEdits] = useState(false)

  useEffect(() => {
    let isMounted = true

    const refresh = async () => {
      try {
        const res = await api.getJSON('/api/system/settings/')
        if (!isMounted) return

        if (res.ok) {
          const canHydrateEditableFields = !hasLocalEdits && !saving && !savingCapacity && !savingThresholds

          if (canHydrateEditableFields) {
            setTimezone(res.body.timezone || '')
            setMaxFeedsCapacityKg(String(res.body.max_feeds_capacity_kg || ''))
            setFeederLowThresholdPct(String(res.body.feeder_low_threshold_pct ?? ''))
            setFeederHighThresholdPct(String(res.body.feeder_high_threshold_pct ?? ''))
            setWaterLowThresholdPct(String(res.body.water_low_threshold_pct ?? ''))
            setWaterHighThresholdPct(String(res.body.water_high_threshold_pct ?? ''))
          }

          setMaxFeedsCapacityUpdatedAt(res.body.max_feeds_capacity_updated_at || '')
          setTimezoneOptions(Array.isArray(res.body.timezone_options) ? res.body.timezone_options : [])
          setServerTimeLocal(res.body.server_time_local || '')
          setServerTimeUtc(res.body.server_time_utc || '')
        } else {
          setError('Failed to load system settings.')
        }
      } catch (err) {
        if (isMounted) setError('Failed to load system settings.')
      } finally {
        if (isMounted) setLoading(false)
      }
    }

    refresh()
    const timer = setInterval(refresh, pollingIntervalMs)

    return () => {
      isMounted = false
      clearInterval(timer)
    }
  }, [hasLocalEdits, saving, savingCapacity, savingThresholds, pollingIntervalMs])

  async function saveTimezone() {
    if (!timezone) {
      setError('Please select a timezone.')
      return
    }

    setSaving(true)
    setMessage('')
    setError('')
    try {
      const res = await api.patchJSON('/api/system/settings/', { timezone })
      if (res.ok) {
        setTimezone(res.body.timezone || timezone)
        setTimezoneOptions(Array.isArray(res.body.timezone_options) ? res.body.timezone_options : timezoneOptions)
        setServerTimeLocal(res.body.server_time_local || '')
        setServerTimeUtc(res.body.server_time_utc || '')
        setHasLocalEdits(false)
        setMessage('Timezone updated for the system.')
      } else {
        const detail = res.body && (res.body.detail || res.body.timezone || JSON.stringify(res.body))
        setError(detail || 'Failed to update timezone.')
      }
    } catch (err) {
      setError('Failed to update timezone.')
    } finally {
      setSaving(false)
    }
  }

  async function saveMaxFeedsCapacity() {
    const parsed = Number(maxFeedsCapacityKg)
    if (!Number.isFinite(parsed) || parsed <= 0) {
      setError('Max feed capacity must be greater than 0.')
      return
    }

    setSavingCapacity(true)
    setMessage('')
    setError('')
    try {
      const res = await api.patchJSON('/api/system/settings/', { max_feeds_capacity_kg: parsed })
      if (res.ok) {
        setMaxFeedsCapacityKg(String(res.body.max_feeds_capacity_kg || parsed))
        setMaxFeedsCapacityUpdatedAt(res.body.max_feeds_capacity_updated_at || '')
        setServerTimeLocal(res.body.server_time_local || '')
        setServerTimeUtc(res.body.server_time_utc || '')
        setHasLocalEdits(false)
        setMessage('Max feed capacity updated for the system.')
      } else {
        const detail = res.body && (res.body.detail || res.body.max_feeds_capacity_kg || JSON.stringify(res.body))
        setError(detail || 'Failed to update max feed capacity.')
      }
    } catch (err) {
      setError('Failed to update max feed capacity.')
    } finally {
      setSavingCapacity(false)
    }
  }

  async function saveThresholds() {
    const feederLow = Number(feederLowThresholdPct)
    const feederHigh = Number(feederHighThresholdPct)
    const waterLow = Number(waterLowThresholdPct)
    const waterHigh = Number(waterHighThresholdPct)

    if (!Number.isFinite(feederLow) || feederLow < 0 || feederLow > 100) {
      setError('Feeder low threshold must be between 0 and 100%.')
      return
    }
    if (!Number.isFinite(feederHigh) || feederHigh < 0 || feederHigh > 100) {
      setError('Feeder high threshold must be between 0 and 100%.')
      return
    }
    if (!Number.isFinite(waterLow) || waterLow < 0 || waterLow > 100) {
      setError('Water low threshold must be between 0 and 100%.')
      return
    }
    if (!Number.isFinite(waterHigh) || waterHigh < 0 || waterHigh > 100) {
      setError('Water high threshold must be between 0 and 100%.')
      return
    }

    setSavingThresholds(true)
    setMessage('')
    setError('')
    try {
      const res = await api.patchJSON('/api/system/settings/', {
        feeder_low_threshold_pct: feederLow,
        feeder_high_threshold_pct: feederHigh,
        water_low_threshold_pct: waterLow,
        water_high_threshold_pct: waterHigh,
      })
      if (res.ok) {
        setFeederLowThresholdPct(String(res.body.feeder_low_threshold_pct ?? feederLow))
        setFeederHighThresholdPct(String(res.body.feeder_high_threshold_pct ?? feederHigh))
        setWaterLowThresholdPct(String(res.body.water_low_threshold_pct ?? waterLow))
        setWaterHighThresholdPct(String(res.body.water_high_threshold_pct ?? waterHigh))
        setServerTimeLocal(res.body.server_time_local || '')
        setServerTimeUtc(res.body.server_time_utc || '')
        setHasLocalEdits(false)
        setMessage('Alert thresholds updated.')
      } else {
        const detail = res.body && (res.body.detail || res.body.feeder_low_threshold_pct || res.body.feeder_high_threshold_pct || res.body.water_low_threshold_pct || res.body.water_high_threshold_pct || JSON.stringify(res.body))
        setError(detail || 'Failed to update thresholds.')
      }
    } catch (err) {
      setError('Failed to update thresholds.')
    } finally {
      setSavingThresholds(false)
    }
  }

  return (
    <section className="settings-page">
      <h3 className="settings-title">System Settings</h3>
      <p className="settings-subtitle">Configure tank sizes, timezone, sensor thresholds, and other system parameters.</p>

      <div className="settings-grid">
        <article className="settings-card">
          <h4>Device Profile</h4>
          <p>Set feeder capacity shared by dashboard, admin, and ESP32. Latest update becomes effective.</p>

          {loading ? <p className="settings-note">Loading feeder capacity...</p> : (
            <>
              <label className="settings-field" htmlFor="max-feeds-capacity">Max Feeds Capacity (kg)</label>
              <input
                id="max-feeds-capacity"
                className="settings-input"
                type="number"
                min="0.01"
                step="0.01"
                value={maxFeedsCapacityKg}
                onChange={e => {
                  setMaxFeedsCapacityKg(e.target.value)
                  setHasLocalEdits(true)
                }}
                disabled={savingCapacity}
              />

              <button className="settings-save-btn" onClick={saveMaxFeedsCapacity} disabled={savingCapacity || !maxFeedsCapacityKg}>
                {savingCapacity ? 'Saving...' : 'Save Capacity'}
              </button>

              <div className="settings-meta-row">
                <span>Last capacity update</span>
                <strong>{maxFeedsCapacityUpdatedAt || '-'}</strong>
              </div>
            </>
          )}
        </article>
        <article className="settings-card">
          <h4>Timezone</h4>
          <p>Keep schedule triggers aligned with your farm local time across server, dashboard, and ESP32.</p>

          {loading ? <p className="settings-note">Loading timezone settings...</p> : (
            <>
              <label className="settings-field" htmlFor="timezone-select">System Timezone</label>
              <select
                id="timezone-select"
                className="settings-select"
                value={timezone}
                onChange={e => {
                  setTimezone(e.target.value)
                  setHasLocalEdits(true)
                }}
                disabled={saving}
              >
                <option value="">Select timezone</option>
                {timezoneOptions.map(tz => (
                  <option key={tz} value={tz}>{tz}</option>
                ))}
              </select>

              <button className="settings-save-btn" onClick={saveTimezone} disabled={saving || !timezone}>
                {saving ? 'Saving...' : 'Save Timezone'}
              </button>

              <div className="settings-time-preview">
                <div>
                  <span>Server Local Time</span>
                  <strong>{serverTimeLocal || '-'}</strong>
                </div>
                <div>
                  <span>Server UTC Time</span>
                  <strong>{serverTimeUtc || '-'}</strong>
                </div>
              </div>
            </>
          )}

          {message ? <p className="settings-success">{message}</p> : null}
          {error ? <p className="settings-error">{error}</p> : null}
        </article>
        <article className="settings-card">
          <h4>Alert Thresholds</h4>
          <p>Configure low/high thresholds for feeder and water tanks. Low triggers alerts; high stops refill pump.</p>

          {loading ? <p className="settings-note">Loading threshold settings...</p> : (
            <>
              <label className="settings-field" htmlFor="feeder-low-threshold">Feeder Low Threshold (%)</label>
              <input
                id="feeder-low-threshold"
                className="settings-input"
                type="number"
                min="0"
                max="100"
                step="1"
                value={feederLowThresholdPct}
                onChange={e => {
                  setFeederLowThresholdPct(e.target.value)
                  setHasLocalEdits(true)
                }}
                disabled={savingThresholds}
              />

              <label className="settings-field" htmlFor="feeder-high-threshold">Feeder High Threshold (%)</label>
              <input
                id="feeder-high-threshold"
                className="settings-input"
                type="number"
                min="0"
                max="100"
                step="1"
                value={feederHighThresholdPct}
                onChange={e => {
                  setFeederHighThresholdPct(e.target.value)
                  setHasLocalEdits(true)
                }}
                disabled={savingThresholds}
              />

              <label className="settings-field" htmlFor="water-low-threshold">Water Low Threshold (%)</label>
              <input
                id="water-low-threshold"
                className="settings-input"
                type="number"
                min="0"
                max="100"
                step="1"
                value={waterLowThresholdPct}
                onChange={e => {
                  setWaterLowThresholdPct(e.target.value)
                  setHasLocalEdits(true)
                }}
                disabled={savingThresholds}
              />

              <label className="settings-field" htmlFor="water-high-threshold">Water High Threshold (%)</label>
              <input
                id="water-high-threshold"
                className="settings-input"
                type="number"
                min="0"
                max="100"
                step="1"
                value={waterHighThresholdPct}
                onChange={e => {
                  setWaterHighThresholdPct(e.target.value)
                  setHasLocalEdits(true)
                }}
                disabled={savingThresholds}
              />

              <button
                className="settings-save-btn"
                onClick={saveThresholds}
                disabled={savingThresholds || feederLowThresholdPct === '' || feederHighThresholdPct === '' || waterLowThresholdPct === '' || waterHighThresholdPct === ''}
              >
                {savingThresholds ? 'Saving...' : 'Save Thresholds'}
              </button>
            </>
          )}
        </article>
      </div>
    </section>
  )
}
