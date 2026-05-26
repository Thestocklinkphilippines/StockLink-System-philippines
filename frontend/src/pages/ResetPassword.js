import React, { useEffect, useState } from 'react'
import { Link, useSearchParams } from 'react-router-dom'
import api from '../api'
import '../styles/main.css'
import '../styles/auth-common.css'
import '../styles/verify-email.css'
import '../styles/reset-password.css'

export default function ResetPassword() {
  const [searchParams] = useSearchParams()
  const uid = searchParams.get('uid')
  const token = searchParams.get('token')
  const hasResetLink = Boolean(uid && token)

  const [email, setEmail] = useState('')
  const [newPassword, setNewPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [status, setStatus] = useState(hasResetLink ? 'ready' : 'idle')
  const [requestState, setRequestState] = useState('idle')
  const [requestText, setRequestText] = useState('')
  const [message, setMessage] = useState(
    hasResetLink
      ? 'Enter a new password to recover your account.'
      : 'Enter the email address on your account and we will send a password reset link.'
  )
  const [isSubmitting, setIsSubmitting] = useState(false)

  useEffect(() => {
    if (hasResetLink) {
      setStatus('ready')
      setMessage('Enter a new password to recover your account.')
      setRequestState('idle')
      setRequestText('')
      return
    }

    setStatus('idle')
    setMessage('Enter the email address on your account and we will send a password reset link.')
    setRequestState('idle')
    setRequestText('')
  }, [hasResetLink, uid, token])

  async function submitResetRequest() {
    setIsSubmitting(true)
    setMessage('')

    try {
      const res = await api.postJSON('/api/auth/password-reset/request/', { email })
      if (res.ok) {
        const found = Boolean(res.body && res.body.account_found)
        const sent = Boolean(res.body && res.body.email_sent)
        setStatus(sent ? 'sent' : 'warning')
        setRequestState(found ? 'sent' : 'missing')
        setRequestText(found ? 'link sent' : 'email not found')
        setMessage(res.body && res.body.detail ? res.body.detail : found ? 'Password reset email sent.' : 'No account found for that email.')
      } else {
        setStatus('error')
        setRequestState('error')
        setRequestText(res.body && res.body.detail ? res.body.detail : 'Unable to request a reset email right now.')
        setMessage(res.body && res.body.detail ? res.body.detail : 'Unable to request a reset email right now.')
      }
    } finally {
      setIsSubmitting(false)
    }
  }

  async function requestReset(event) {
    event.preventDefault()
    submitResetRequest()
  }

  async function submitNewPassword(event) {
    event.preventDefault()
    setIsSubmitting(true)
    setMessage('')

    try {
      const res = await api.postJSON('/api/auth/password-reset/confirm/', {
        uid,
        token,
        new_password: newPassword,
        confirm_password: confirmPassword,
      })

      if (res.ok) {
        setStatus('success')
        setMessage(res.body && res.body.detail ? res.body.detail : 'Password reset successfully.')
        setNewPassword('')
        setConfirmPassword('')
      } else {
        setStatus('error')
        setMessage(res.body && res.body.detail ? res.body.detail : 'The reset link may be invalid or expired.')
      }
    } finally {
      setIsSubmitting(false)
    }
  }

  return (
    <main className="auth-page auth-page-reset-password">
      <div className="main-content">
        <section className="register-panel verify-email-panel reset-password-panel">
          <p className="legal-kicker">Account Security</p>
          <h1 className="legal-title">Password Recovery</h1>
          <p className="verify-status-text">{message}</p>

          {!hasResetLink ? (
            <form onSubmit={requestReset} className="reset-password-form">
              <label className="reset-password-field" htmlFor="reset-email">
                <span>Email Address</span>
                <input
                  id="reset-email"
                  type="email"
                  className="auth-input"
                  value={email}
                  onChange={e => setEmail(e.target.value)}
                  required
                  autoComplete="email"
                  placeholder="name@example.com"
                />
              </label>

              {requestState !== 'idle' ? (
                <p className={`reset-inline-status ${requestState === 'sent' ? 'is-success' : requestState === 'missing' ? 'is-error' : ''}`} aria-live="polite">
                  {requestText}
                </p>
              ) : null}

              <button className="login-button reset-submit-button" type="submit" disabled={isSubmitting}>
                <span className="login-button-text">{isSubmitting ? 'Sending...' : 'Send Reset Link'}</span>
                {isSubmitting ? <span className="auth-button-spinner" aria-hidden="true"></span> : null}
              </button>

              {requestState === 'sent' ? (
                <button
                  type="button"
                  className="reset-resend-button"
                  onClick={submitResetRequest}
                  disabled={isSubmitting || !email}
                >
                  Resend link
                </button>
              ) : null}
            </form>
          ) : (
            <form onSubmit={submitNewPassword} className="reset-password-form">
              <label className="reset-password-field" htmlFor="new-password">
                <span>New Password</span>
                <input
                  id="new-password"
                  type="password"
                  className="auth-input"
                  value={newPassword}
                  onChange={e => setNewPassword(e.target.value)}
                  required
                  autoComplete="new-password"
                  minLength={8}
                  placeholder="Create a new password"
                />
              </label>

              <label className="reset-password-field" htmlFor="confirm-password">
                <span>Confirm Password</span>
                <input
                  id="confirm-password"
                  type="password"
                  className="auth-input"
                  value={confirmPassword}
                  onChange={e => setConfirmPassword(e.target.value)}
                  required
                  autoComplete="new-password"
                  minLength={8}
                  placeholder="Repeat the new password"
                />
              </label>

              <button className="login-button reset-submit-button" type="submit" disabled={isSubmitting}>
                <span className="login-button-text">{isSubmitting ? 'Updating...' : 'Reset Password'}</span>
                {isSubmitting ? <span className="auth-button-spinner" aria-hidden="true"></span> : null}
              </button>
            </form>
          )}

          {status === 'success' ? (
            <div className="verify-actions">
              <Link className="login-button legal-link" to="/login">Go to Login</Link>
            </div>
          ) : null}

          {status === 'error' ? (
            <div className="verify-actions">
              <Link className="login-button legal-link" to="/forgot-password">Try Again</Link>
              <Link className="createacc-link" to="/login">Back to Login</Link>
            </div>
          ) : null}

          {!hasResetLink && status !== 'success' ? (
            <div className="verify-actions">
              <Link className="createacc-link" to="/login">Back to Login</Link>
            </div>
          ) : null}
        </section>
      </div>
    </main>
  )
}