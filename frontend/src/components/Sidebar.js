import React, { useEffect, useState } from 'react'
import { NavLink, useNavigate } from 'react-router-dom'
import api from '../api'
import './Sidebar.css'

export default function Sidebar(){
  const [user, setUser] = useState(null)
  const navigate = useNavigate()

  useEffect(()=>{
    let isMounted = true

    ;(async ()=>{
      const res = await api.getJSON('/api/auth/user/')
      if (!isMounted) return

      if (res.ok && res.body && res.body.is_authenticated) {
        setUser(res.body.username)
      } else {
        setUser(null)
      }
    })()

    return () => {
      isMounted = false
    }
  },[])

  async function doLogout(){
    await api.postJSON('/api/auth/logout/', {})
    setUser(null)
    navigate('/login')
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
        <NavLink to="/dashboard/settings" className={({isActive})=> `nav-item ${isActive ? 'active' : ''}`}>System Settings</NavLink>
      </nav>

      <div className="sidebar-footer">
        <div className="user-chip">
          <div className="user-avatar" aria-hidden="true">{userInitial}</div>
          <div>
            <div className="user-name">{user || 'StockLink User'}</div>
            <div className="user-role">Administrator</div>
          </div>
        </div>
        <button type="button" className="sidebar-logout-btn" onClick={doLogout}>Logout</button>
      </div>
    </aside>

  )
}
