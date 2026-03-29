import React from 'react'
import { createRoot } from 'react-dom/client'
import App from './App'
import { WINDOW_KEY } from './pollingConfig'

const GLOBAL_POLLING_INTERVAL_MS = 3000
window[WINDOW_KEY] = GLOBAL_POLLING_INTERVAL_MS

const container = document.getElementById('root')
const root = createRoot(container)
root.render(<App />)
