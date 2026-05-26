import React from 'react'
import { Link } from 'react-router-dom'
import '../styles/main.css'
import '../styles/auth-common.css'
import '../styles/legal.css'

export default function PrivacyPolicy() {
  return (
    <main className="auth-page auth-page-legal">
      <div className="main-content">
        <section className="register-panel legal-panel">
          <p className="legal-kicker">Legal Information</p>
          <h1 className="legal-title">Privacy Policy</h1>
          <p className="logintitlemessage">This policy describes how StockLink IoT collects, uses, and protects account and device information.</p>

          <div className="legal-body">
            <section className="legal-section">
              <h3>1. Information we collect</h3>
              <p>We may collect account details, login activity, device identifiers, configuration settings, and operational records needed to run the platform.</p>
            </section>

            <section className="legal-section">
              <h3>2. How we use information</h3>
              <p>We use this information to authenticate users, display dashboards, synchronize device status, send alerts, and support platform maintenance.</p>
            </section>

            <section className="legal-section">
              <h3>3. Data protection</h3>
              <p>We apply access controls and operational safeguards intended to reduce unauthorized access, disclosure, or loss of system records.</p>
            </section>

            <div className="legal-actions">
              <Link to="/register" className="login-button legal-link">Back to registration</Link>
              <Link to="/terms-of-service" className="createacc-link">View Terms of Service</Link>
            </div>
          </div>
        </section>
      </div>
    </main>
  )
}