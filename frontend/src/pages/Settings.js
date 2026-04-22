import React, { useEffect, useState } from 'react'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import { formatDateTime } from '../utils/datetime'
import '../styles/settings.css'

export default function Settings(){
  const pollingIntervalMs = getPollingIntervalMs()
  const [loading, setLoading] = useState(true)
  const [saving, setSaving] = useState(false)
  const [savingCapacity, setSavingCapacity] = useState(false)
  const [savingThresholds, setSavingThresholds] = useState(false)
  const [savingAlertThresholds, setSavingAlertThresholds] = useState(false)
  const [savingKeywords, setSavingKeywords] = useState(false)
  const [savingRecipients, setSavingRecipients] = useState(false)
  const [savingEmail, setSavingEmail] = useState(false)
  const [sendingTestEmail, setSendingTestEmail] = useState(false)
  const [timezone, setTimezone] = useState('')
  const [maxFeedsCapacityKg, setMaxFeedsCapacityKg] = useState('')
  const [maxFeedsCapacityUpdatedAt, setMaxFeedsCapacityUpdatedAt] = useState('')
  const [feederLowThresholdPct, setFeederLowThresholdPct] = useState('')
  const [feederHighThresholdPct, setFeederHighThresholdPct] = useState('')
  const [waterLowThresholdPct, setWaterLowThresholdPct] = useState('')
  const [waterHighThresholdPct, setWaterHighThresholdPct] = useState('')
  const [alertFeederLowThresholdPct, setAlertFeederLowThresholdPct] = useState('')
  const [alertFeederHighThresholdPct, setAlertFeederHighThresholdPct] = useState('')
  const [alertWaterLowThresholdPct, setAlertWaterLowThresholdPct] = useState('')
  const [alertWaterHighThresholdPct, setAlertWaterHighThresholdPct] = useState('')
  const [importantLogKeywords, setImportantLogKeywords] = useState('')
  const [alertRecipients, setAlertRecipients] = useState([])
  const [newRecipientEmail, setNewRecipientEmail] = useState('')
  const [accountEmail, setAccountEmail] = useState('')
  const [timezoneOptions, setTimezoneOptions] = useState([])
  const [serverTimeLocal, setServerTimeLocal] = useState('')
  const [serverTimeUtc, setServerTimeUtc] = useState('')
  const [message, setMessage] = useState('')
  const [error, setError] = useState('')
  const [settingsDirty, setSettingsDirty] = useState(false)
  const [emailDirty, setEmailDirty] = useState(false)

  useEffect(() => {
    let isMounted = true

    const refresh = async () => {
      try {
        const [settingsRes, userRes] = await Promise.all([
          api.getJSON('/api/system/settings/'),
          api.getJSON('/api/auth/user/'),
        ])
        if (!isMounted) return

        if (settingsRes.ok) {
          const canHydrateEditableFields = !settingsDirty && !saving && !savingCapacity && !savingThresholds && !savingAlertThresholds && !savingKeywords && !savingRecipients

          if (canHydrateEditableFields) {
            setTimezone(settingsRes.body.timezone || '')
            setMaxFeedsCapacityKg(String(settingsRes.body.max_feeds_capacity_kg || ''))
            setFeederLowThresholdPct(String(settingsRes.body.feeder_low_threshold_pct ?? ''))
            setFeederHighThresholdPct(String(settingsRes.body.feeder_high_threshold_pct ?? ''))
            setWaterLowThresholdPct(String(settingsRes.body.water_low_threshold_pct ?? ''))
            setWaterHighThresholdPct(String(settingsRes.body.water_high_threshold_pct ?? ''))
            setAlertFeederLowThresholdPct(String(settingsRes.body.alert_feeder_low_threshold_pct ?? ''))
            setAlertFeederHighThresholdPct(String(settingsRes.body.alert_feeder_high_threshold_pct ?? ''))
            setAlertWaterLowThresholdPct(String(settingsRes.body.alert_water_low_threshold_pct ?? ''))
            setAlertWaterHighThresholdPct(String(settingsRes.body.alert_water_high_threshold_pct ?? ''))
            const keywordList = Array.isArray(settingsRes.body.important_log_keywords) ? settingsRes.body.important_log_keywords : []
            setImportantLogKeywords(keywordList.join(', '))
            const recipientList = Array.isArray(settingsRes.body.alert_recipients) ? settingsRes.body.alert_recipients : []
            setAlertRecipients(recipientList)
          }

          setMaxFeedsCapacityUpdatedAt(settingsRes.body.max_feeds_capacity_updated_at || '')
          setTimezoneOptions(Array.isArray(settingsRes.body.timezone_options) ? settingsRes.body.timezone_options : [])
          setServerTimeLocal(settingsRes.body.server_time_local || '')
          setServerTimeUtc(settingsRes.body.server_time_utc || '')
        } else {
          setError('Failed to load system settings.')
        }

        if (userRes.ok && userRes.body && userRes.body.is_authenticated) {
          if (!emailDirty && !savingEmail && !sendingTestEmail) {
            setAccountEmail(userRes.body.email || '')
          }
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
  }, [settingsDirty, emailDirty, saving, savingCapacity, savingThresholds, savingAlertThresholds, savingKeywords, savingRecipients, savingEmail, sendingTestEmail, pollingIntervalMs])

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
        setSettingsDirty(false)
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
        setSettingsDirty(false)
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
        setSettingsDirty(false)
        setMessage('Refill control thresholds updated.')
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

  async function saveAlertThresholds() {
    const feederLow = Number(alertFeederLowThresholdPct)
    const feederHigh = Number(alertFeederHighThresholdPct)
    const waterLow = Number(alertWaterLowThresholdPct)
    const waterHigh = Number(alertWaterHighThresholdPct)

    if (!Number.isFinite(feederLow) || feederLow < 0 || feederLow > 100) {
      setError('Alert feeder low threshold must be between 0 and 100%.')
      return
    }
    if (!Number.isFinite(feederHigh) || feederHigh < 0 || feederHigh > 100) {
      setError('Alert feeder high threshold must be between 0 and 100%.')
      return
    }
    if (!Number.isFinite(waterLow) || waterLow < 0 || waterLow > 100) {
      setError('Alert water low threshold must be between 0 and 100%.')
      return
    }
    if (!Number.isFinite(waterHigh) || waterHigh < 0 || waterHigh > 100) {
      setError('Alert water high threshold must be between 0 and 100%.')
      return
    }

    setSavingAlertThresholds(true)
    setMessage('')
    setError('')
    try {
      const res = await api.patchJSON('/api/system/settings/', {
        alert_feeder_low_threshold_pct: feederLow,
        alert_feeder_high_threshold_pct: feederHigh,
        alert_water_low_threshold_pct: waterLow,
        alert_water_high_threshold_pct: waterHigh,
      })
      if (res.ok) {
        setAlertFeederLowThresholdPct(String(res.body.alert_feeder_low_threshold_pct ?? feederLow))
        setAlertFeederHighThresholdPct(String(res.body.alert_feeder_high_threshold_pct ?? feederHigh))
        setAlertWaterLowThresholdPct(String(res.body.alert_water_low_threshold_pct ?? waterLow))
        setAlertWaterHighThresholdPct(String(res.body.alert_water_high_threshold_pct ?? waterHigh))
        setServerTimeLocal(res.body.server_time_local || '')
        setServerTimeUtc(res.body.server_time_utc || '')
        setSettingsDirty(false)
        setMessage('Alert trigger thresholds updated.')
      } else {
        const detail = res.body && (
          res.body.detail ||
          res.body.alert_feeder_low_threshold_pct ||
          res.body.alert_feeder_high_threshold_pct ||
          res.body.alert_water_low_threshold_pct ||
          res.body.alert_water_high_threshold_pct ||
          JSON.stringify(res.body)
        )
        setError(detail || 'Failed to update alert thresholds.')
      }
    } catch (err) {
      setError('Failed to update alert thresholds.')
    } finally {
      setSavingAlertThresholds(false)
    }
  }

  async function saveImportantKeywords() {
    const keywords = importantLogKeywords
      .split(',')
      .map(item => item.trim().toLowerCase())
      .filter(Boolean)

    setSavingKeywords(true)
    setMessage('')
    setError('')
    try {
      const res = await api.patchJSON('/api/system/settings/', {
        important_log_keywords: keywords,
      })
      if (res.ok) {
        const keywordList = Array.isArray(res.body.important_log_keywords) ? res.body.important_log_keywords : keywords
        setImportantLogKeywords(keywordList.join(', '))
        setSettingsDirty(false)
        setMessage('Important notification keywords updated.')
      } else {
        const detail = res.body && (res.body.detail || res.body.important_log_keywords || JSON.stringify(res.body))
        setError(detail || 'Failed to update important keywords.')
      }
    } catch (err) {
      setError('Failed to update important keywords.')
    } finally {
      setSavingKeywords(false)
    }
  }

  function addRecipientEmail() {
    const candidate = newRecipientEmail.trim().toLowerCase()
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/

    if (!candidate) {
      setError('Please enter an email before adding.')
      return
    }
    if (!emailRegex.test(candidate)) {
      setError('Please enter a valid email address.')
      return
    }
    if (alertRecipients.includes(candidate)) {
      setError('That email is already in the alert list.')
      return
    }

    setError('')
    setMessage('')
    setAlertRecipients(prev => [...prev, candidate])
    setNewRecipientEmail('')
    setSettingsDirty(true)
  }

  function removeRecipientEmail(emailToRemove) {
    setAlertRecipients(prev => prev.filter(item => item !== emailToRemove))
    setSettingsDirty(true)
    setMessage('')
    setError('')
  }

  async function saveAlertRecipients() {
    setSavingRecipients(true)
    setMessage('')
    setError('')
    try {
      const res = await api.patchJSON('/api/system/settings/', {
        alert_recipients: alertRecipients,
      })
      if (res.ok) {
        const recipientList = Array.isArray(res.body.alert_recipients) ? res.body.alert_recipients : alertRecipients
        setAlertRecipients(recipientList)
        setSettingsDirty(false)
        setMessage('Alert recipient list updated.')
      } else {
        const detail = res.body && (res.body.detail || res.body.alert_recipients || JSON.stringify(res.body))
        setError(detail || 'Failed to update alert recipient list.')
      }
    } catch (err) {
      setError('Failed to update alert recipient list.')
    } finally {
      setSavingRecipients(false)
    }
  }

  async function saveAccountEmail() {
    if (!accountEmail) {
      setError('Email is required.')
      return
    }

    setSavingEmail(true)
    setMessage('')
    setError('')
    try {
      const res = await api.patchJSON('/api/auth/user/', { email: accountEmail })
      if (res.ok) {
        setAccountEmail(res.body.email || accountEmail)
        setEmailDirty(false)
        setMessage('Account email updated.')
      } else {
        const detail = res.body && (res.body.detail || res.body.email || JSON.stringify(res.body))
        setError(detail || 'Failed to update account email.')
      }
    } catch (err) {
      setError('Failed to update account email.')
    } finally {
      setSavingEmail(false)
    }
  }

  async function sendTestEmail() {
    if (!accountEmail) {
      setError('Email is required before sending a test message.')
      return
    }

    setSendingTestEmail(true)
    setMessage('')
    setError('')
    try {
      const res = await api.postJSON('/api/notifications/test-email/', { email: accountEmail })
      if (res.ok) {
        setMessage(`Test email sent to ${accountEmail}.`)
      } else {
        const detail = res.body && (res.body.detail || JSON.stringify(res.body))
        setError(detail || 'Failed to send test email.')
      }
    } catch (err) {
      setError('Failed to send test email.')
    } finally {
      setSendingTestEmail(false)
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
                  setSettingsDirty(true)
                }}
                disabled={savingCapacity}
              />

              <button className="settings-save-btn" onClick={saveMaxFeedsCapacity} disabled={savingCapacity || !maxFeedsCapacityKg}>
                {savingCapacity ? 'Saving...' : 'Save Capacity'}
              </button>

              <div className="settings-meta-row">
                <span>Last capacity update</span>
                <strong>{formatDateTime(maxFeedsCapacityUpdatedAt)}</strong>
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
                  setSettingsDirty(true)
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
                  <strong>{formatDateTime(serverTimeLocal)}</strong>
                </div>
                <div>
                  <span>Server UTC Time</span>
                  <strong>{formatDateTime(serverTimeUtc)}</strong>
                </div>
              </div>
            </>
          )}
        </article>
        <article className="settings-card">
          <h4>Refill Control Thresholds</h4>
          <p>Configure low/high thresholds used by the device refill logic. This section does not control alert trigger points.</p>

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
                  setSettingsDirty(true)
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
                  setSettingsDirty(true)
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
                  setSettingsDirty(true)
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
                  setSettingsDirty(true)
                }}
                disabled={savingThresholds}
              />

              <button
                className="settings-save-btn"
                onClick={saveThresholds}
                disabled={savingThresholds || feederLowThresholdPct === '' || feederHighThresholdPct === '' || waterLowThresholdPct === '' || waterHighThresholdPct === ''}
              >
                {savingThresholds ? 'Saving...' : 'Save Refill Thresholds'}
              </button>
            </>
          )}
        </article>
        <article className="settings-card">
          <h4>Alert Trigger Thresholds</h4>
          <p>Configure low/high thresholds that trigger and resolve alerts. These are independent from refill control thresholds.</p>

          {loading ? <p className="settings-note">Loading alert thresholds...</p> : (
            <>
              <label className="settings-field" htmlFor="alert-feeder-low-threshold">Alert Feeder Low Threshold (%)</label>
              <input
                id="alert-feeder-low-threshold"
                className="settings-input"
                type="number"
                min="0"
                max="100"
                step="1"
                value={alertFeederLowThresholdPct}
                onChange={e => {
                  setAlertFeederLowThresholdPct(e.target.value)
                  setSettingsDirty(true)
                }}
                disabled={savingAlertThresholds}
              />

              <label className="settings-field" htmlFor="alert-feeder-high-threshold">Alert Feeder High Threshold (%)</label>
              <input
                id="alert-feeder-high-threshold"
                className="settings-input"
                type="number"
                min="0"
                max="100"
                step="1"
                value={alertFeederHighThresholdPct}
                onChange={e => {
                  setAlertFeederHighThresholdPct(e.target.value)
                  setSettingsDirty(true)
                }}
                disabled={savingAlertThresholds}
              />

              <label className="settings-field" htmlFor="alert-water-low-threshold">Alert Water Low Threshold (%)</label>
              <input
                id="alert-water-low-threshold"
                className="settings-input"
                type="number"
                min="0"
                max="100"
                step="1"
                value={alertWaterLowThresholdPct}
                onChange={e => {
                  setAlertWaterLowThresholdPct(e.target.value)
                  setSettingsDirty(true)
                }}
                disabled={savingAlertThresholds}
              />

              <label className="settings-field" htmlFor="alert-water-high-threshold">Alert Water High Threshold (%)</label>
              <input
                id="alert-water-high-threshold"
                className="settings-input"
                type="number"
                min="0"
                max="100"
                step="1"
                value={alertWaterHighThresholdPct}
                onChange={e => {
                  setAlertWaterHighThresholdPct(e.target.value)
                  setSettingsDirty(true)
                }}
                disabled={savingAlertThresholds}
              />

              <button
                className="settings-save-btn"
                onClick={saveAlertThresholds}
                disabled={savingAlertThresholds || alertFeederLowThresholdPct === '' || alertFeederHighThresholdPct === '' || alertWaterLowThresholdPct === '' || alertWaterHighThresholdPct === ''}
              >
                {savingAlertThresholds ? 'Saving...' : 'Save Alert Thresholds'}
              </button>
            </>
          )}
        </article>
        <article className="settings-card">
          <h4>Notification Filters</h4>
          <p>Control which incoming log payloads count as important notifications.</p>

          {loading ? <p className="settings-note">Loading notification keywords...</p> : (
            <>
              <label className="settings-field" htmlFor="important-log-keywords">Important Log Keywords (comma-separated)</label>
              <textarea
                id="important-log-keywords"
                className="settings-textarea"
                rows={4}
                value={importantLogKeywords}
                onChange={e => {
                  setImportantLogKeywords(e.target.value)
                  setSettingsDirty(true)
                }}
                disabled={savingKeywords}
                placeholder="critical, error, fault"
              />

              <button className="settings-save-btn" onClick={saveImportantKeywords} disabled={savingKeywords}>
                {savingKeywords ? 'Saving...' : 'Save Keywords'}
              </button>
            </>
          )}
        </article>
        <article className="settings-card">
          <h4>Email Notifications</h4>
          <p>Use your account email for alerts and test SMTP delivery from this panel.</p>

          {loading ? <p className="settings-note">Loading alert recipients...</p> : (
            <>
              <label className="settings-field" htmlFor="alert-recipient-input">Alert Recipient Emails</label>
              <div className="settings-recipient-add-row">
                <input
                  id="alert-recipient-input"
                  className="settings-input"
                  type="email"
                  value={newRecipientEmail}
                  onChange={e => setNewRecipientEmail(e.target.value)}
                  disabled={savingRecipients}
                  placeholder="alerts@example.com"
                />
                <button className="settings-save-btn settings-outline-btn" onClick={addRecipientEmail} disabled={savingRecipients || !newRecipientEmail.trim()}>
                  Add
                </button>
              </div>

              {alertRecipients.length === 0 ? (
                <p className="settings-note">No alert recipients configured. Alerts will fall back to active user emails.</p>
              ) : (
                <ul className="settings-recipient-list">
                  {alertRecipients.map(email => (
                    <li key={email} className="settings-recipient-item">
                      <span>{email}</span>
                      <button
                        className="settings-recipient-remove"
                        onClick={() => removeRecipientEmail(email)}
                        disabled={savingRecipients}
                        type="button"
                      >
                        Remove
                      </button>
                    </li>
                  ))}
                </ul>
              )}

              <button className="settings-save-btn" onClick={saveAlertRecipients} disabled={savingRecipients}>
                {savingRecipients ? 'Saving...' : 'Save Alert List'}
              </button>
            </>
          )}

          {loading ? <p className="settings-note">Loading account email...</p> : (
            <>
              <label className="settings-field" htmlFor="account-email">Account Email</label>
              <input
                id="account-email"
                className="settings-input"
                type="email"
                value={accountEmail}
                onChange={e => {
                  setAccountEmail(e.target.value)
                  setEmailDirty(true)
                }}
                disabled={savingEmail || sendingTestEmail}
                placeholder="name@gmail.com"
              />

              <div className="settings-button-row">
                <button className="settings-save-btn" onClick={saveAccountEmail} disabled={savingEmail || sendingTestEmail || !accountEmail}>
                  {savingEmail ? 'Saving...' : 'Save Email'}
                </button>
                <button className="settings-save-btn settings-outline-btn" onClick={sendTestEmail} disabled={sendingTestEmail || savingEmail || !accountEmail}>
                  {sendingTestEmail ? 'Sending...' : 'Send Test Email'}
                </button>
              </div>
            </>
          )}
        </article>
      </div>

      {message ? <p className="settings-success">{message}</p> : null}
      {error ? <p className="settings-error">{error}</p> : null}
    </section>
  )
}
