import React, { useState } from 'react'
import { Link } from 'react-router-dom'
import api from '../api'
import '../styles/main.css'
import '../styles/auth-common.css'
import '../styles/register.css'
import '../styles/verify-email.css'

export default function Register(){
  const [username, setUsername] = useState('')
  const [firstName, setFirstName] = useState('')
  const [lastName, setLastName] = useState('')
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [consent, setConsent] = useState(false)
  const [show, setShow] = useState(false)
  const [error, setError] = useState('')
  const [success, setSuccess] = useState('')
  const [isRegistered, setIsRegistered] = useState(false)
  const [resendBusy, setResendBusy] = useState(false)
  const [isSubmitting, setIsSubmitting] = useState(false)

  function getPasswordIssue(value) {
    if (value.length < 8) return 'Password must be at least 8 characters long.'
    if (!/[A-Z]/.test(value)) return 'Password must include at least one uppercase letter.'
    if (!/[a-z]/.test(value)) return 'Password must include at least one lowercase letter.'
    if (!/[0-9]/.test(value)) return 'Password must include at least one number.'
    if (!/[^A-Za-z0-9]/.test(value)) return 'Password must include at least one special character.'
    return ''
  }

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
    setSuccess('')
    setIsSubmitting(true)
    if (!consent) {
      setError('You must agree to the Terms of Service and Privacy Policy before creating an account.')
      setIsSubmitting(false)
      return
    }
    const passwordIssue = getPasswordIssue(password)
    if (passwordIssue) {
      setError(passwordIssue)
      setIsSubmitting(false)
      return
    }
    if (password !== confirmPassword) {
      setError('Passwords do not match.')
      setIsSubmitting(false)
      return
    }
    try {
      const res = await api.postJSON('/api/auth/register/', { username, first_name: firstName, last_name: lastName, email, password })
      if (res.ok) {
        setIsRegistered(true)
        setSuccess(res.body && res.body.detail ? res.body.detail : 'Verification email sent. Please check your inbox.')
      } else {
        setError(res.body && res.body.detail ? res.body.detail : 'Register failed')
      }
    } finally {
      setIsSubmitting(false)
    }
  }

  async function resendVerification() {
    setError('')
    setSuccess('')
    setResendBusy(true)
    const res = await api.postJSON('/api/auth/resend-verification/', { email })
    setResendBusy(false)
    if (res.ok) {
      setSuccess(res.body && res.body.detail ? res.body.detail : 'Verification email sent.')
      return
    }
    setError(res.body && res.body.detail ? res.body.detail : 'Failed to resend verification email.')
  }

  return (
    <main className="auth-page auth-page-register">
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
          <p className="branding-text">Join the network of smart livestock management. Monitor, manage, and automate your farm with precision.</p>

          <div className="features">
            <div className="feature-item">
              <div className="feature-icon">
                <svg className="feature-svg" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth={3} viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><polyline points="20 6 9 17 4 12"></polyline></svg>
              </div>
              <div className="feature-text">
                <span>Real-time Resource Monitoring</span>
              </div>
            </div>

            <div className="feature-item">
              <div className="feature-icon">
                <svg className="feature-svg" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth={3} viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><polyline points="20 6 9 17 4 12"></polyline></svg>
              </div>
              <div className="feature-text">
                <span>Automated Farm Operations</span>
              </div>
            </div>

            <div className="feature-item">
              <div className="feature-icon">
                <svg className="feature-svg" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth={3} viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><polyline points="20 6 9 17 4 12"></polyline></svg>
              </div>
              <div className="feature-text">
                <span>Instant Smart Alert System</span>
              </div>
            </div>
          </div>
        </div>

        <section className="register-panel">
          <h2 className="logintitle">Create Account</h2>
          <p className="logintitlemessage">Welcome! Please fill in the details below to register your farm management account.</p>

          {!isRegistered ? (
          <form onSubmit={submit}>
            <div className="username-field">
              <p>Username</p>
              <input type="text" id="username" name="username" required value={username} onChange={e=>setUsername(e.target.value)} />
            </div>

            <div className="name-grid">
              <div className="username-field">
                <p>First Name</p>
                <input type="text" id="first-name" name="first_name" value={firstName} onChange={e => setFirstName(e.target.value)} />
              </div>

              <div className="username-field">
                <p>Last Name</p>
                <input type="text" id="last-name" name="last_name" value={lastName} onChange={e => setLastName(e.target.value)} />
              </div>
            </div>

            <div className="email-field">
              <p>Email Address</p>
              <input
                type="email"
                id="email"
                name="email"
                required
                value={email}
                onChange={e => setEmail(e.target.value)}
              />
            </div>

            <div className="password-field">
              <div className="password-label-container">
                <label htmlFor="password">Password</label>
                <a className="forgot-link" href="#">Forgot Password?</a>
              </div>
              <input
                id="password"
                name="password"
                placeholder="••••••••"
                required
                type={show ? 'text' : 'password'}
                value={password}
                onChange={e => setPassword(e.target.value)}
                aria-describedby="password-requirements"
                title="At least 8 characters with one uppercase letter, one lowercase letter, one number, and one special character"
              />
              <button type="button" className={`password-toggle ${show ? 'active' : ''}`} onClick={() => setShow(!show)}>
                {show ? <EyeOffIcon /> : <EyeIcon />}
              </button>
            </div>

            <div className="password-field">
              <div className="password-label-container">
                <label htmlFor="confirm-password">Confirm Password</label>
              </div>
              <input
                id="confirm-password"
                name="confirm-password"
                placeholder="••••••••"
                required
                type={show ? 'text' : 'password'}
                value={confirmPassword}
                onChange={e => setConfirmPassword(e.target.value)}
                title="Re-enter the same password"
              />
            </div>

            <p id="password-requirements" className="password-requirements">
              Password must be at least 8 characters and include an uppercase letter, a lowercase letter, a number, and a special character.
            </p>

            <div className="terms">
              <input id="terms-agreed" name="terms-agreed" type="checkbox" checked={consent} onChange={e => setConsent(e.target.checked)} required />
              <label htmlFor="terms-agreed">
                I Agree to the <Link to="/terms-of-service" className="terms-link">Terms of Service</Link> and <Link to="/privacy-policy" className="privacy-link">Privacy Policy</Link>
              </label>
            </div>

            {error ? <p className="auth-error">{error}</p> : null}

            <button className="login-button" type="submit" disabled={isSubmitting} aria-busy={isSubmitting}>
              <span className="login-button-text">{isSubmitting ? 'Creating Account...' : 'Create Account'}</span>
              {isSubmitting ? <span className="auth-button-spinner" aria-hidden="true"></span> : <svg className="login-button-arrow" xmlns="http://www.w3.org/2000/svg" width="3em" height="3em" viewBox="0 0 24 24" fill="none">
                <path d="M6 12H18M18 12L13 7M18 12L13 17" stroke="#000000" strokeWidth={2} strokeLinecap="round" strokeLinejoin="round"/>
              </svg>}
            </button>
          </form>
          ) : (
            <div className="verification-panel">
              <p className="verification-title">Check your email</p>
              <p className="verification-message">
                We sent a verification link to <strong>{email}</strong>. Click the link to activate your account before logging in.
              </p>

              {success ? <p className="auth-success">{success}</p> : null}
              {error ? <p className="auth-error">{error}</p> : null}

              <button className="login-button" type="button" onClick={resendVerification} disabled={resendBusy}>
                <span className="login-button-text">{resendBusy ? 'Sending...' : 'Resend Verification Email'}</span>
              </button>

              <p className="verification-alt-link">
                Ready to continue? <a className="createacc-link" href="/login">Go to login</a>
              </p>
            </div>
          )}

          <footer className="footer-links">
            <div className="createacc">
              <p>
                Already have an account?
                <a className="createacc-link" href="/login">
                  Login here
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
