import React from 'react'
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import Login from './pages/Login'
import Register from './pages/Register'
import Dashboard from './pages/Dashboard'
import './styles/main.css'

function App(){
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<Navigate to="/login" replace />} />
        <Route path="/login" element={<Login/>} />
        <Route path="/auth/login" element={<Login/>} />
        <Route path="/register" element={<Register/>} />
        <Route path="/auth/register" element={<Register/>} />
        <Route path="/dashboard/*" element={<Dashboard/>} />
      </Routes>
    </BrowserRouter>
  )
}

export default App
