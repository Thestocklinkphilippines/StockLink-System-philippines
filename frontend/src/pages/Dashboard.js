import React, { useEffect, useState } from 'react'
import Sidebar from '../components/Sidebar'
import Header from '../components/Header'
import { Routes, Route, Navigate } from 'react-router-dom'
import Overview from './Overview'
import Schedules from './Schedules'
import Alerts from './Alerts'
import Logs from './Logs'
import Settings from './Settings'
import api from '../api'
import { useNavigate } from 'react-router-dom'
import '../styles/dashboard.css'

export default function Dashboard(){
  const [loading, setLoading] = useState(true)
  const navigate = useNavigate()

  useEffect(()=>{
    let mounted = true
    ;(async ()=>{
      try{
        const res = await api.getJSON('/api/auth/user/')
        if (!mounted) return
        if (!(res.ok && res.body && res.body.is_authenticated)){
          navigate('/login', { replace: true })
        } else {
          setLoading(false)
        }
      }catch(err){
        if (mounted) navigate('/login', { replace: true })
      }
    })()
    return ()=>{ mounted = false }
  }, [navigate])

  if (loading) {
    return (
      <div className="dashboard-loading-wrap">
        <div className="dashboard-loading-card">Checking session...</div>
      </div>
    )
  }

  return (
    <div className="dashboard-app">
      <Sidebar />
      <div className="dashboard-content">
        <div className="dashboard-header-wrap">
          <Header />
        </div>
        <div className="dashboard-card">
          <Routes>
            <Route path="/overview" element={<Overview/>} />
            <Route path="/schedules" element={<Schedules/>} />
            <Route path="/alerts" element={<Alerts/>} />
            <Route path="/logs" element={<Logs/>} />
            <Route path="/settings" element={<Settings/>} />
            <Route path="/" element={<Navigate to="/dashboard/overview" replace />} />
          </Routes>
        </div>
      </div>
    </div>
  )
}
