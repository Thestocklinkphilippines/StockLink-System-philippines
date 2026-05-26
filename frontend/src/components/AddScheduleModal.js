import React, { useEffect, useState } from 'react'
import './AddScheduleModal.css'

const DAY_LABELS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']

export default function AddScheduleModal({ open, onClose, onSubmit, isSubmitting, initialSchedule }) {
  const [scheduleName, setScheduleName] = useState('')
  const [selectedDays, setSelectedDays] = useState([])
  const [scheduleTime, setScheduleTime] = useState('07:00')
  const [amountKg, setAmountKg] = useState('1')
  const [enabled, setEnabled] = useState(true)
  const [error, setError] = useState('')

  const isEditMode = Boolean(initialSchedule)

  useEffect(() => {
    if (!open) return
    setError('')

    if (initialSchedule) {
      setScheduleName(initialSchedule.schedule_name || '')
      setSelectedDays(Array.isArray(initialSchedule.days) ? initialSchedule.days : [])
      setScheduleTime(initialSchedule.time || '07:00')
      setAmountKg(String(initialSchedule.feeding_amount_kg ?? '1'))
      setEnabled(Boolean(initialSchedule.enabled))
    } else {
      setScheduleName('')
      setSelectedDays([])
      setScheduleTime('07:00')
      setAmountKg('1')
      setEnabled(true)
    }
  }, [open, initialSchedule])

  function toggleDay(day) {
    setSelectedDays(prev =>
      prev.includes(day)
        ? prev.filter(d => d !== day)
        : [...prev, day]
    )
  }

  async function submit(e) {
    e.preventDefault()
    setError('')

    const amount = Number(amountKg)
    if (!scheduleName.trim()) {
      setError('Schedule name is required.')
      return
    }
    if (selectedDays.length === 0) {
      setError('Please select at least one day.')
      return
    }
    if (!scheduleTime) {
      setError('Please choose a schedule time.')
      return
    }
    if (!Number.isFinite(amount) || amount <= 0) {
      setError('Feed amount must be greater than 0.')
      return
    }

    await onSubmit({
      schedule_name: scheduleName.trim(),
      enabled,
      days: selectedDays,
      time: scheduleTime,
      feeding_amount_kg: amount,
    })
  }

  if (!open) return null

  return (
    <div className="schedule-modal-overlay" onClick={onClose}>
      <section className="schedule-modal" role="dialog" aria-modal="true" aria-label={isEditMode ? 'Edit schedule' : 'Add schedule'} onClick={e => e.stopPropagation()}>
        <div className="schedule-modal-head">
          <h3>{isEditMode ? 'Edit Feeding Schedule' : 'Add Feeding Schedule'}</h3>
          <button className="schedule-modal-close" type="button" onClick={onClose} aria-label="Close schedule popup">x</button>
        </div>

        <p className="schedule-modal-subtitle">
          Set up a recurring weekly feeding schedule. Pick days and times, and we'll handle the rest.
        </p>

        <form className="schedule-modal-form" onSubmit={submit}>
          <label className="schedule-field">
            <span>Schedule Name</span>
            <input
              type="text"
              value={scheduleName}
              onChange={e => setScheduleName(e.target.value)}
              placeholder="Morning Feed"
              required
            />
          </label>

          <div className="schedule-days-selector">
            <span className="schedule-days-label">Repeat On</span>
            <div className="schedule-days-grid">
              {DAY_LABELS.map(day => (
                <button
                  key={day}
                  type="button"
                  className={`schedule-day-btn ${selectedDays.includes(day) ? 'selected' : ''}`}
                  onClick={() => toggleDay(day)}
                >
                  {day}
                </button>
              ))}
            </div>
          </div>

          <label className="schedule-field">
            <span>Time</span>
            <input
              type="time"
              value={scheduleTime}
              onChange={e => setScheduleTime(e.target.value)}
              required
            />
          </label>

          <label className="schedule-field">
            <span>Feed Amount (kg)</span>
            <input
              type="number"
              min="0.001"
              step="0.001"
              value={amountKg}
              onChange={e => setAmountKg(e.target.value)}
              required
            />
          </label>

          <label className="schedule-enabled-row">
            <input type="checkbox" checked={enabled} onChange={e => setEnabled(e.target.checked)} />
            <span>Enabled</span>
          </label>

          {error ? <p className="schedule-modal-error">{error}</p> : null}

          <div className="schedule-modal-actions">
            <button className="schedule-btn schedule-btn-ghost" type="button" onClick={onClose} disabled={isSubmitting}>Cancel</button>
            <button className="schedule-btn schedule-btn-primary" type="submit" disabled={isSubmitting}>
              {isSubmitting ? 'Saving...' : isEditMode ? 'Save Changes' : 'Save Schedule'}
            </button>
          </div>
        </form>
      </section>
    </div>
  )
}
