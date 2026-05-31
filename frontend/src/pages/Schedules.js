import React, { useEffect, useState } from 'react'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import '../styles/schedules.css'
import AddScheduleModal from '../components/AddScheduleModal'

export default function Schedules({ currentUser = null }){
  const pollingIntervalMs = getPollingIntervalMs()
  const [schedules, setSchedules] = useState([])
  const [deviceConfig, setDeviceConfig] = useState(null)
  const [loading, setLoading] = useState(true)
  const [deviceId, setDeviceId] = useState('esp32-001')
  const [showAddModal, setShowAddModal] = useState(false)
  const [isSavingSchedule, setIsSavingSchedule] = useState(false)
  const [editingSchedule, setEditingSchedule] = useState(null)
  const [togglingMap, setTogglingMap] = useState({})
  const [scheduleFilter, setScheduleFilter] = useState('all')
  const [feedNowAmountKg, setFeedNowAmountKg] = useState('0.25')
  const [feedNowCommands, setFeedNowCommands] = useState([])
  const [feedNowLoading, setFeedNowLoading] = useState(false)
  const [feedNowMessage, setFeedNowMessage] = useState('')
  const [feedNowError, setFeedNowError] = useState('')

  useEffect(()=>{
    let isMounted = true

    const refresh = async () => {
      try{
        const active = await api.getActiveDeviceId()
        if (!isMounted) return
        setDeviceId(active)

        const [schedulesRes, logsRes, feedNowRes] = await Promise.all([
          api.getJSON(`/api/device/${active}/schedules/`),
          api.getJSON(`/api/device/${active}/config/`),
          api.getJSON(`/api/device/${active}/feed-now/`),
        ])

        if (isMounted && schedulesRes.ok){
          setSchedules(schedulesRes.body || [])
        }
        if (isMounted && logsRes.ok) {
          setDeviceConfig(logsRes.body || null)
        }
        if (isMounted && feedNowRes.ok) {
          setFeedNowCommands(feedNowRes.body || [])
        }
      }catch(err){
        if (isMounted) console.error('Failed to load schedules', err)
      } finally{
        if (isMounted) setLoading(false)
      }

    }

    refresh()
    const timer = setInterval(refresh, pollingIntervalMs)

    return () => {
      isMounted = false
      clearInterval(timer)
    }
  },[pollingIntervalMs])

  async function submitFeedNowCommand(){
    const amount = Number(feedNowAmountKg)
    if (!Number.isFinite(amount) || amount <= 0) {
      setFeedNowError('Feed amount must be greater than 0.')
      setFeedNowMessage('')
      return
    }

    setFeedNowLoading(true)
    setFeedNowMessage('')
    setFeedNowError('')
    try {
      const res = await api.postJSON(`/api/device/${deviceId}/feed-now/`, { amount_kg: amount })
      if (res.ok) {
        setFeedNowCommands(prev => [res.body, ...prev].slice(0, 20))
        setFeedNowMessage(`Feed-now command queued: #${res.body.id} ${amount.toFixed(3)} kg`)
      } else {
        const detail = res.body && (res.body.detail || res.body.amount_kg || JSON.stringify(res.body))
        setFeedNowError(detail || 'Failed to queue feed-now command.')
      }
    } catch (err) {
      setFeedNowError('Failed to queue feed-now command.')
    } finally {
      setFeedNowLoading(false)
    }
  }

  function formatFeedNowStatus(status){
    if (status === 'pending') return 'Pending'
    if (status === 'executed') return 'Executed'
    if (status === 'failed') return 'Failed'
    return status || 'Unknown'
  }

  async function createSchedule(payload){
    setIsSavingSchedule(true)
    try{
      const res = await api.postJSON(`/api/device/${deviceId}/schedules/`, payload)
      if (res.ok){
        setSchedules(prev => [...prev, res.body])
        setShowAddModal(false)
      } else {
        const detail = res.body && (res.body.detail || JSON.stringify(res.body))
        alert(detail || 'Failed to create schedule')
      }
    }catch(err){
      alert('Failed to create schedule')
    } finally{
      setIsSavingSchedule(false)
    }
  }

  async function updateSchedule(id, payload){
    setIsSavingSchedule(true)
    try{
      const res = await api.patchJSON(`/api/device/${deviceId}/schedules/${id}/`, payload)
      if (res.ok){
        setSchedules(prev => prev.map(s => (s.id === id ? res.body : s)))
        setShowAddModal(false)
        setEditingSchedule(null)
      } else {
        const detail = res.body && (res.body.detail || JSON.stringify(res.body))
        alert(detail || 'Failed to update schedule')
      }
    }catch(err){
      alert('Failed to update schedule')
    } finally{
      setIsSavingSchedule(false)
    }
  }

  async function saveSchedule(payload){
    if (editingSchedule) {
      await updateSchedule(editingSchedule.id, payload)
      return
    }
    await createSchedule(payload)
  }

  async function deleteSchedule(id){
    if (!confirm('Delete schedule?')) return
    const r = await fetch(`/api/device/${deviceId}/schedules/${id}/`, { method:'DELETE', credentials:'same-origin', headers:{ 'X-CSRFToken': document.cookie.match('(^|;)\\s*csrftoken\\s*=\\s*([^;]+)')?.pop() } })
    if (r.status===204) setSchedules(prev => prev.filter(s=>s.id!==id))
  }

  async function toggleSchedule(schedule){
    const nextEnabled = !schedule.enabled
    setTogglingMap(prev => ({ ...prev, [schedule.id]: true }))
    try{
      const res = await api.patchJSON(`/api/device/${deviceId}/schedules/${schedule.id}/`, { enabled: nextEnabled })
      if (res.ok){
        setSchedules(prev => prev.map(s => (s.id === schedule.id ? res.body : s)))
      } else {
        const detail = res.body && (res.body.detail || JSON.stringify(res.body))
        alert(detail || 'Failed to update schedule state')
      }
    }catch(err){
      alert('Failed to update schedule state')
    } finally{
      setTogglingMap(prev => ({ ...prev, [schedule.id]: false }))
    }
  }

  function openAddModal(){
    setEditingSchedule(null)
    setShowAddModal(true)
  }

  function openEditModal(schedule){
    setEditingSchedule(schedule)
    setShowAddModal(true)
  }

  function closeScheduleModal(){
    setShowAddModal(false)
    setEditingSchedule(null)
  }

  function getScheduleTimeIcon(timeValue){
    const hourStr = String(timeValue || '').split(':')[0]
    const hour = Number(hourStr)

    if (!Number.isFinite(hour)) {
      return { icon: '🕒', label: 'scheduled' }
    }

    if (hour >= 3 && hour < 6) {
      return { icon: '🌅', label: 'dawn' }
    }
    if (hour >= 6 && hour < 12) {
      return { icon: '☀️', label: 'morning' }
    }
    if (hour >= 12 && hour < 17) {
      return { icon: '🌤️', label: 'afternoon' }
    }
    if (hour >= 17 && hour < 20) {
      return { icon: '🌇', label: 'evening' }
    }
    return { icon: '🌙', label: 'night' }
  }

  const visibleSchedules = schedules.filter(s => {
    if (scheduleFilter === 'active') return Boolean(s.enabled)
    if (scheduleFilter === 'paused') return !s.enabled
    return true
  })

  const todaysTotalKg = Number(deviceConfig?.config?.total_feeds_today_kg ?? deviceConfig?.total_feeds_today_kg ?? 0)
  const displayTotalKg = Number.isFinite(todaysTotalKg) ? todaysTotalKg : 0

  return (
    <div className="schedules-page">
      <div className="schedules-stats">
        <div className="stat-card">
          <p className="stat-label">Today's Total</p>
          <p className="stat-value">{displayTotalKg.toFixed(3)}kg</p>
        </div>
        <div className="stat-card">
          <p className="stat-label">Active Schedules</p>
          <p className="stat-value">{schedules.filter(s=>s.enabled).length}</p>
        </div>
      </div>

      <div className="schedules-tabs-wrap">
        <div className="schedules-filter-row">
          <span className="schedules-filter-label">Filter</span>
          <div className="schedules-filter-pills" role="tablist" aria-label="Schedule filter">
            <button
              type="button"
              role="tab"
              aria-selected={scheduleFilter === 'all'}
              className={`schedules-filter-pill ${scheduleFilter === 'all' ? 'schedules-filter-pill-active' : ''}`}
              onClick={() => setScheduleFilter('all')}
            >
              All
            </button>
            <button
              type="button"
              role="tab"
              aria-selected={scheduleFilter === 'active'}
              className={`schedules-filter-pill ${scheduleFilter === 'active' ? 'schedules-filter-pill-active' : ''}`}
              onClick={() => setScheduleFilter('active')}
            >
              Active
            </button>
            <button
              type="button"
              role="tab"
              aria-selected={scheduleFilter === 'paused'}
              className={`schedules-filter-pill ${scheduleFilter === 'paused' ? 'schedules-filter-pill-active' : ''}`}
              onClick={() => setScheduleFilter('paused')}
            >
              Paused
            </button>
          </div>
        </div>
      </div>

      <main className="schedules-list">
        {loading ? <div className="schedules-loading">Loading...</div> : (
          visibleSchedules.length === 0 ? (
            <div className="schedules-empty">No schedules match this filter.</div>
          ) : (
          visibleSchedules.map(s => {
            const timeIcon = getScheduleTimeIcon(s.time)
            return (
            <article key={s.id} className="schedule-item">
              <div className="schedule-row">
                <div>
                  <div className="schedule-title-row">
                    <span className="schedule-icon" aria-label={`${timeIcon.label} schedule`} title={`${timeIcon.label} schedule`}>
                      {timeIcon.icon}
                    </span>
                    <h3 className="schedule-title">{s.schedule_name}</h3>
                  </div>
                  <p className="schedule-days">{(s.days && s.days.join(', ')) || 'Daily'}</p>
                </div>
                <div>
                  <button
                    type="button"
                    className={`schedule-toggle ${s.enabled ? 'schedule-toggle-on' : ''}`}
                    onClick={()=>toggleSchedule(s)}
                    disabled={Boolean(togglingMap[s.id])}
                    aria-pressed={s.enabled}
                    aria-label={`${s.enabled ? 'Disable' : 'Enable'} schedule ${s.schedule_name}`}
                  >
                    <span className={`schedule-toggle-knob ${s.enabled ? 'schedule-toggle-knob-on' : ''}`}></span>
                  </button>
                </div>
              </div>

              <div className="schedule-row schedule-row-bottom">
                <div className="schedule-meta">
                  <div>
                    <div className="schedule-meta-label">Time</div>
                    <div className="schedule-meta-value">{s.time}</div>
                  </div>
                  <div>
                    <div className="schedule-meta-label">Amount</div>
                    <div className="schedule-meta-value">{Number(s.feeding_amount_kg).toFixed(3)}kg</div>
                  </div>
                </div>
                <div className="schedule-actions">
                  <button onClick={()=>openEditModal(s)} className="schedule-action schedule-action-edit">✎</button>
                  <button onClick={()=>deleteSchedule(s.id)} className="schedule-action schedule-action-delete">🗑</button>
                </div>
              </div>
            </article>
          )}))
        )}

        <div className="schedules-feed-now">
          <div className="schedules-feed-now-copy">
            <h4>Need to feed now?</h4>
                <p>Queue an immediate feed-now command for this device.</p>
            <div className="feed-now-controls">
              <label htmlFor="feed-now-amount" className="feed-now-label">Amount (kg)</label>
              <input
                id="feed-now-amount"
                className="feed-now-input"
                type="number"
                min="0.001"
                step="0.001"
                value={feedNowAmountKg}
                onChange={e => setFeedNowAmountKg(e.target.value)}
                    disabled={feedNowLoading}
              />
                  <button className="feed-now-btn" onClick={submitFeedNowCommand} disabled={feedNowLoading || !feedNowAmountKg}>
                {feedNowLoading ? 'Queueing...' : 'Feed Now'}
              </button>
            </div>
            {feedNowMessage ? <p className="feed-now-message">{feedNowMessage}</p> : null}
            {feedNowError ? <p className="feed-now-error">{feedNowError}</p> : null}
          </div>
          <div className="feed-now-history">
            <h5>Recent Feed Commands</h5>
            {feedNowCommands.length === 0 ? (
              <p className="feed-now-empty">No feed-now commands yet.</p>
            ) : (
              <ul>
                {feedNowCommands.slice(0, 5).map(cmd => (
                  <li key={cmd.id}>
                    <span>#{cmd.id} · {Number(cmd.amount_kg).toFixed(3)}kg</span>
                    <span className={`feed-now-status feed-now-status-${cmd.status}`}>{formatFeedNowStatus(cmd.status)}</span>
                  </li>
                ))}
              </ul>
            )}
          </div>
        </div>
      </main>

      <button className="fab-add" onClick={openAddModal} aria-label="Add schedule">+</button>

      <AddScheduleModal
        open={showAddModal}
        onClose={closeScheduleModal}
        onSubmit={saveSchedule}
        isSubmitting={isSavingSchedule}
        initialSchedule={editingSchedule}
      />
    </div>
  )
}
