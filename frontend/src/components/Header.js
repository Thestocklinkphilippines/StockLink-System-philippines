import React, { useEffect, useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import api from '../api'
import './Header.css'

export default function Header(){
  const [user, setUser] = useState(null)
  const navigate = useNavigate()

  useEffect(()=>{
    (async ()=>{
      const res = await api.getJSON('/api/auth/user/')
      if (res.ok && res.body && res.body.is_authenticated) setUser(res.body.username)
    })()
  },[])

  async function doLogout(){
    await api.postJSON('/api/auth/logout/', {})
    setUser(null)
    navigate('/login')
  }

  return (
    <header className="header-shell">
      <h2 className="header-title">Dashboard</h2>
      <div>
        {user ? (
          <div className="header-user-row">
            <span className="header-user-name">Signed in as {user}</span>
            <button className="header-logout-btn" onClick={doLogout}>Logout</button>
          </div>
        ) : (
          <Link className="header-login-link" to="/login">Login</Link>
        )}
      </div>
    </header>
  )
}
