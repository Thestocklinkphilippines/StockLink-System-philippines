import React, { useEffect, useRef, useState } from 'react'
import { NavLink, useNavigate } from 'react-router-dom'
import api from '../api'
import './Sidebar.css'

export default function Sidebar({ currentUser = null }){
  const [user, setUser] = useState(null)
  const [role, setRole] = useState('USER')
  const [isStaff, setIsStaff] = useState(false)
  const [menuOpen, setMenuOpen] = useState(false)
  const [passwordModalOpen, setPasswordModalOpen] = useState(false)
  const [currentPassword, setCurrentPassword] = useState('')
  const [newPassword, setNewPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [passwordSaving, setPasswordSaving] = useState(false)
  const [passwordMessage, setPasswordMessage] = useState('')
  const [passwordError, setPasswordError] = useState('')
  const navigate = useNavigate()
  const menuRef = useRef(null)

  useEffect(()=>{
    if (currentUser && currentUser.is_authenticated) {
      setUser(currentUser.username || null)
      setRole(currentUser.role || (currentUser.is_staff ? 'ADMIN' : 'USER'))
      setIsStaff(Boolean(currentUser.is_staff))
      return
    }

    let isMounted = true

    ;(async ()=>{
      const res = await api.getJSON('/api/auth/user/')
      if (!isMounted) return

      if (res.ok && res.body && res.body.is_authenticated) {
        setUser(res.body.username)
        setRole(res.body.role || (res.body.is_staff ? 'ADMIN' : 'USER'))
        setIsStaff(Boolean(res.body.is_staff))
      } else {
        setUser(null)
        setRole('USER')
        setIsStaff(false)
      }
    })()

    return () => {
      isMounted = false
    }
  },[currentUser])

  useEffect(() => {
    if (!menuOpen) return

    const handlePointerDown = (event) => {
      if (menuRef.current && !menuRef.current.contains(event.target)) {
        setMenuOpen(false)
      }
    }

    const handleKeyDown = (event) => {
      if (event.key === 'Escape') {
        setMenuOpen(false)
      }
    }

    document.addEventListener('mousedown', handlePointerDown)
    document.addEventListener('keydown', handleKeyDown)

    return () => {
      document.removeEventListener('mousedown', handlePointerDown)
      document.removeEventListener('keydown', handleKeyDown)
    }
  }, [menuOpen])

  useEffect(() => {
    if (!passwordModalOpen) return

    const handleKeyDown = (event) => {
      if (event.key === 'Escape') {
        setPasswordModalOpen(false)
      }
    }

    document.addEventListener('keydown', handleKeyDown)

    return () => {
      document.removeEventListener('keydown', handleKeyDown)
    }
  }, [passwordModalOpen])

  useEffect(() => {
    if (!passwordModalOpen) {
      setCurrentPassword('')
      setNewPassword('')
      setConfirmPassword('')
      setPasswordSaving(false)
      setPasswordMessage('')
      setPasswordError('')
    }
  }, [passwordModalOpen])

  async function doLogout(){
    await api.postJSON('/api/auth/logout/', {})
    setMenuOpen(false)
    setUser(null)
    setRole('USER')
    setIsStaff(false)
    navigate('/login')
  }

  async function doDeleteAccount(){
    const confirmed = window.confirm('Delete this account permanently? This cannot be undone.')
    if (!confirmed) return

    const res = await api.deleteJSON('/api/auth/user/')
    if (!res.ok) {
      window.alert('Unable to delete the account right now.')
      return
    }

    setMenuOpen(false)
    setUser(null)
    navigate('/login')
  }

  function openPasswordModal() {
    setMenuOpen(false)
    setPasswordMessage('')
    setPasswordError('')
    setPasswordModalOpen(true)
  }

  function goToForgotPassword() {
    setPasswordModalOpen(false)
    navigate('/forgot-password')
  }

  async function doChangePassword(event) {
    event.preventDefault()

    if (!currentPassword || !newPassword || !confirmPassword) {
      setPasswordError('All password fields are required.')
      return
    }

    if (newPassword !== confirmPassword) {
      setPasswordError('New passwords do not match.')
      return
    }

    setPasswordSaving(true)
    setPasswordMessage('')
    setPasswordError('')

    try {
      const res = await api.patchJSON('/api/auth/user/', {
        current_password: currentPassword,
        new_password: newPassword,
        confirm_password: confirmPassword,
      })

      if (res.ok) {
        setPasswordMessage(res.body?.detail || 'Password updated successfully.')
        setCurrentPassword('')
        setNewPassword('')
        setConfirmPassword('')
      } else {
        const detail = res.body && (res.body.detail || JSON.stringify(res.body))
        setPasswordError(detail || 'Failed to update password.')
      }
    } catch (err) {
      setPasswordError('Failed to update password.')
    } finally {
      setPasswordSaving(false)
    }
  }

  const userInitial = user ? user.charAt(0).toUpperCase() : 'U'

  return (
    <aside className="sidebar">
      <div className="sidebar-brand">
        <div className="logo-container" aria-hidden="true">
          <svg className="logo-icon" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5"></path>
          </svg>
        </div>
        <div className="brand-text">
          <h1 className="branding-title">StockLink IoT</h1>
          <p className="branding-subtitle">Feeder + Drinker Control Panel</p>
        </div>
      </div>

      <nav className="nav-menu" aria-label="Primary">
        <NavLink to="/dashboard/overview" className={({isActive})=> `nav-item ${isActive ? 'active' : ''}`}>Overview</NavLink>
        <NavLink to="/dashboard/schedules" className={({isActive})=> `nav-item ${isActive ? 'active' : ''}`}>Schedule Management</NavLink>
        <NavLink to="/dashboard/alerts" className={({isActive})=> `nav-item ${isActive ? 'active' : ''}`}>Notifications &amp; Alarms</NavLink>
        <NavLink to="/dashboard/logs" className={({isActive})=> `nav-item ${isActive ? 'active' : ''}`}>Feeding Logs/History</NavLink>
        {isStaff ? <NavLink to="/dashboard/settings" className={({isActive})=> `nav-item ${isActive ? 'active' : ''}`}>System Settings</NavLink> : null}
        {isStaff ? <NavLink to="/dashboard/admin-users" className={({isActive})=> `nav-item ${isActive ? 'active' : ''}`}>Admin Users</NavLink> : null}
      </nav>

      <div className="sidebar-footer">
        <div className={`user-chip-wrap ${menuOpen ? 'is-open' : ''}`} ref={menuRef}>
          <button
            type="button"
            className="user-chip"
            onClick={() => setMenuOpen((current) => !current)}
            aria-haspopup="menu"
            aria-expanded={menuOpen}
          >
            <div className="user-avatar" aria-hidden="true">{userInitial}</div>
            <div className="user-chip-copy">
              <div className="user-name">{user || 'StockLink User'}</div>
              <div className="user-role">{role === 'ADMIN' ? 'Administrator' : 'User'}</div>
            </div>
            <span className="user-chip-caret" aria-hidden="true">▾</span>
          </button>

          {menuOpen ? (
            <div className="user-chip-menu" role="menu" aria-label="Account actions">
              <button type="button" className="user-chip-menu-item" role="menuitem" onClick={openPasswordModal}>
                Change password
              </button>
              <button type="button" className="user-chip-menu-item danger" role="menuitem" onClick={doDeleteAccount}>
                Delete account
              </button>
            </div>
          ) : null}
        </div>

        <button type="button" className="sidebar-logout-btn" onClick={doLogout}>Logout</button>

        {passwordModalOpen ? (
          <div className="user-password-overlay" onClick={() => setPasswordModalOpen(false)}>
            <section className="user-password-modal" role="dialog" aria-modal="true" aria-label="Change password" onClick={e => e.stopPropagation()}>
              <div className="user-password-modal-head">
                <h3>Change Password</h3>
                <button type="button" className="user-password-close" onClick={() => setPasswordModalOpen(false)} aria-label="Close password dialog">x</button>
              </div>

              <p className="user-password-subtitle">Update your account password from the sidebar without leaving the page.</p>

              <form className="user-password-form" onSubmit={doChangePassword}>
                <label className="user-password-field">
                  <span>Current Password</span>
                  <input
                    type="password"
                    value={currentPassword}
                    onChange={e => setCurrentPassword(e.target.value)}
                    autoComplete="current-password"
                    required
                  />
                </label>

                <button type="button" className="user-password-forgot-link" onClick={goToForgotPassword}>
                  Forgot password?
                </button>

                <label className="user-password-field">
                  <span>New Password</span>
                  <input
                    type="password"
                    value={newPassword}
                    onChange={e => setNewPassword(e.target.value)}
                    autoComplete="new-password"
                    minLength={8}
                    required
                  />
                </label>

                <label className="user-password-field">
                  <span>Confirm New Password</span>
                  <input
                    type="password"
                    value={confirmPassword}
                    onChange={e => setConfirmPassword(e.target.value)}
                    autoComplete="new-password"
                    minLength={8}
                    required
                  />
                </label>

                {passwordMessage ? <p className="user-password-success">{passwordMessage}</p> : null}
                {passwordError ? <p className="user-password-error">{passwordError}</p> : null}

                <div className="user-password-actions">
                  <button type="button" className="user-password-btn user-password-btn-ghost" onClick={() => setPasswordModalOpen(false)} disabled={passwordSaving}>
                    Cancel
                  </button>
                  <button type="submit" className="user-password-btn user-password-btn-primary" disabled={passwordSaving}>
                    {passwordSaving ? 'Saving...' : 'Update Password'}
                  </button>
                </div>
              </form>
            </section>
          </div>
        ) : null}
      </div>
    </aside>

  )
}
