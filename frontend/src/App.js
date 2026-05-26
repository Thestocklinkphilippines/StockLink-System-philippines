import React from 'react'
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import Login from './pages/Login'
import Register from './pages/Register'
import VerifyEmail from './pages/VerifyEmail'
import ResetPassword from './pages/ResetPassword'
import Dashboard from './pages/Dashboard'
import TermsOfService from './pages/TermsOfService'
import PrivacyPolicy from './pages/PrivacyPolicy'
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
        <Route path="/verify-email" element={<VerifyEmail/>} />
        <Route path="/forgot-password" element={<ResetPassword/>} />
        <Route path="/reset-password" element={<ResetPassword/>} />
        <Route path="/terms-of-service" element={<TermsOfService/>} />
        <Route path="/privacy-policy" element={<PrivacyPolicy/>} />
        <Route path="/dashboard/*" element={<Dashboard/>} />
      </Routes>
    </BrowserRouter>
  )
}

export default App
