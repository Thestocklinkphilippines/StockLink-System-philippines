import React, { useEffect, useState } from 'react'
import { Link, useSearchParams } from 'react-router-dom'
import api from '../api'
import '../styles/main.css'
import '../styles/auth-common.css'
import '../styles/verify-email.css'

export default function VerifyEmail() {
  const [searchParams] = useSearchParams()
  const [status, setStatus] = useState('loading')
  const [message, setMessage] = useState('Verifying your email...')

  useEffect(() => {
    async function verify() {
      const uid = searchParams.get('uid')
      const token = searchParams.get('token')

      if (!uid || !token) {
        setStatus('error')
        setMessage('Missing verification parameters. Please use the link from your email.')
        return
      }

      const res = await api.postJSON('/api/auth/verify-email/', { uid, token })
      if (res.ok) {
        setStatus('success')
        setMessage(res.body && res.body.detail ? res.body.detail : 'Email verified successfully.')
        return
      }

      setStatus('error')
      setMessage(res.body && res.body.detail ? res.body.detail : 'Verification failed. The link may be invalid or expired.')
    }

    verify()
  }, [searchParams])

  return (
    <main className="auth-page auth-page-verify-email">
      <div className="main-content">
        <section className="register-panel verify-email-panel">
          <p className="legal-kicker">Account Security</p>
          <h1 className="legal-title">Email Verification</h1>
          <p className="verify-status-text">{message}</p>

          {status === 'loading' ? <p className="verify-muted">Please wait a moment...</p> : null}
          {status === 'success' ? (
            <div className="verify-actions">
              <Link className="login-button legal-link" to="/login">Proceed to Login</Link>
              <Link className="createacc-link" to="/register">Create Another Account</Link>
            </div>
          ) : null}
          {status === 'error' ? (
            <div className="verify-actions">
              <Link className="login-button legal-link" to="/register">Back to Registration</Link>
              <Link className="createacc-link" to="/login">Go to Login</Link>
            </div>
          ) : null}
        </section>
      </div>
    </main>
  )
}
