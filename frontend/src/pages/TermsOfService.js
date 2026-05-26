import React from 'react'
import { Link } from 'react-router-dom'
import '../styles/main.css'
import '../styles/auth-common.css'
import '../styles/legal.css'

export default function TermsOfService() {
  return (
    <main className="auth-page auth-page-legal">
      <div className="main-content">
        <section className="register-panel legal-panel">
          <p className="legal-kicker">Legal Information</p>
          <h1 className="legal-title">Terms of Service</h1>
          <p className="logintitlemessage">These terms explain how StockLink IoT may be used when creating and managing an account.</p>

          <div className="legal-body">
            <section className="legal-section">
              <h3>1. Account responsibility</h3>
              <p>You are responsible for providing accurate information, protecting your credentials, and keeping your account activity compliant with applicable farm and data management rules.</p>
            </section>

            <section className="legal-section">
              <h3>2. Acceptable use</h3>
              <p>Do not use the platform to interfere with devices, bypass access controls, or submit false records. The service is intended for legitimate poultry and livestock management operations.</p>
            </section>

            <section className="legal-section">
              <h3>3. Service changes</h3>
              <p>We may update features, availability, or these terms to reflect system improvements, safety changes, or legal requirements.</p>
            </section>

            <div className="legal-actions">
              <Link to="/register" className="login-button legal-link">Back to registration</Link>
              <Link to="/privacy-policy" className="createacc-link">View Privacy Policy</Link>
            </div>
          </div>
        </section>
      </div>
    </main>
  )
}