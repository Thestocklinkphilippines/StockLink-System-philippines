import React, { useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import api from '../api'
import '../styles/main.css'
import '../styles/auth-common.css'
import '../styles/login.css'

export default function Login(){
  const [identity, setIdentity] = useState('')
  const [password, setPassword] = useState('')
  const [show, setShow] = useState(false)
  const [remember, setRemember] = useState(false)
  const [error, setError] = useState('')
  const [isSubmitting, setIsSubmitting] = useState(false)
  const navigate = useNavigate()

  function EyeOffIcon() {
    return (
      <svg
        className="icon"
        fill="none"
        stroke="currentColor"
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeWidth="3"
        viewBox="0 0 24 24"
      >
        <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z" />
        <circle cx="12" cy="12" r="3" />
      </svg>
    )
  }

  function EyeIcon() {
    return (
      <svg
        width="21px"
        height="21px"
        viewBox="0 0 24 24"
        xmlns="http://www.w3.org/2000/svg"
      >
        <g
          fill="none"
          fillRule="evenodd"
          stroke="currentColor"
          strokeLinecap="round"
          strokeLinejoin="round"
          transform="translate(2 10)"
        >
          <path d="m0 .5c2.53705308 3.66666667 5.37038642 5.5 8.5 5.5 3.1296136 0 5.9629469-1.83333333 8.5-5.5"/>
          <path d="m2.5 3.423-2 2.077"/>
          <path d="m14.5 3.423 2 2.077"/>
          <path d="m10.5 6 1 2.5"/>
          <path d="m6.5 6-1 2.5"/>
        </g>
      </svg>
    )
  }

  async function submit(e){
    e.preventDefault()
    setError('')
    setIsSubmitting(true)
    try {
      const res = await api.postJSON('/api/auth/login/', { identity, password })
      if (res.ok) {
        navigate('/dashboard/overview')
      } else {
        setError(res.body && res.body.detail ? res.body.detail : 'Login failed')
      }
    } finally {
      setIsSubmitting(false)
    }
  }

  return (
    <main className="auth-page auth-page-login">
      <div className="main-content">
        <div className="sidepanel">
          <div className="circle1"></div>
          <div className="circle2"></div>
          <div className="logo-container">
            <svg
              className="logo-icon"
              fill="none"
              stroke="currentColor"
              strokeLinecap="round"
              strokeLinejoin="round"
              strokeWidth="2"
              viewBox="0 0 24 24"
              xmlns="http://www.w3.org/2000/svg"
            >
              <path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5" />
            </svg>
          </div>

          <h1 className="branding-title">StockLink IoT</h1>
          <p className="branding-text">Smart Feeding &amp; Drinker Systems for Modern Poultry Farms. Monitor, Manage, and Automate with Reliability.</p>

          <div className="features">
            <div className="feature-item">
              <div className="feature-icon">
                <svg className="feature-svg" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="3" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><polyline points="20 6 9 17 4 12"></polyline></svg>
              </div>
              <div className="feature-text">
                <span>Real-time Resource Monitoring</span>
              </div>
            </div>

            <div className="feature-item">
              <div className="feature-icon">
                <svg className="feature-svg" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="3" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><polyline points="20 6 9 17 4 12"></polyline></svg>
              </div>
              <div className="feature-text">
                <span>Automated Farm Operations</span>
              </div>
            </div>

            <div className="feature-item">
              <div className="feature-icon">
                <svg className="feature-svg" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="3" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><polyline points="20 6 9 17 4 12"></polyline></svg>
              </div>
              <div className="feature-text">
                <span>Instant Smart Alert System</span>
              </div>
            </div>
          </div>
        </div>

        <section className="login-panel">
          <h2 className="logintitle">Website Login</h2>
          <p className="logintitlemessage">Welcome back! Access your poultry management dashboard.</p>
          <form onSubmit={submit}>
            <div className="email-field">
              <p>Email or Username</p>
              <input value={identity} onChange={e=>setIdentity(e.target.value)} required className="auth-input" id="identity" name="identity"/>
            </div>

            <div className="password-field">
              <div className="password-label-container">
                <label htmlFor="password">Password</label>
                <Link className="forgot-link" to="/forgot-password">Forgot Password?</Link>
              </div>
              <input id="password" type={show ? 'text' : 'password'} value={password} onChange={e=>setPassword(e.target.value)} required className="auth-input"/>
              <button type="button" className={`password-toggle ${show ? 'active' : ''}`} onClick={() => setShow(!show)}>
                {show ? <EyeOffIcon /> : <EyeIcon />}
              </button>
            </div>

            <div className="remember">
              <input checked={remember} onChange={e=>setRemember(e.target.checked)} id="remember-me" name="remember-me" type="checkbox" className="auth-checkbox"/>
              <label htmlFor="remember-me">Remember this device For 30 days</label>
            </div>

            {error ? <p className="auth-error">{error}</p> : null}

            <button className="login-button" type="submit" disabled={isSubmitting} aria-busy={isSubmitting}>
              <span className="login-button-text">{isSubmitting ? 'Logging in...' : 'Login to Dashboard'}</span>
              {isSubmitting ? <span className="auth-button-spinner" aria-hidden="true"></span> : <svg className="login-button-arrow" xmlns="http://www.w3.org/2000/svg" width="3em" height="3em" viewBox="0 0 24 24" fill="none">
                <path d="M6 12H18M18 12L13 7M18 12L13 17" stroke="#000000" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/>
              </svg>}
            </button>
          </form>

          <footer className="footer-links">
            <div className="createacc">
              <p>
                Don't have an account yet?
                <a className="createacc-link" href="/register">
                  Create an account
                </a>
              </p>
            </div>

            <div className="copyright">
              <p>© 2026 StockLink IoT Systems. All rights reserved. <br/>Secure poultry management platform.</p>
            </div>
          </footer>
        </section>
      </div>
    </main>
  )
}
