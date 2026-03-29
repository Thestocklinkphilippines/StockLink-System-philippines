const FALLBACK_POLLING_INTERVAL_MS = 3000
const WINDOW_KEY = '__THESIS_POLLING_INTERVAL_MS__'

export function getPollingIntervalMs() {
  const configured = window[WINDOW_KEY]
  if (Number.isFinite(configured) && configured > 0) {
    return configured
  }
  return FALLBACK_POLLING_INTERVAL_MS
}

export { WINDOW_KEY, FALLBACK_POLLING_INTERVAL_MS }
