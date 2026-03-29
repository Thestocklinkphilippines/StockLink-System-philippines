// Minimal API helper for SPA to call Django endpoints with CSRF and cookies
function getCookie(name) {
  const v = document.cookie.match('(^|;)\\s*' + name + '\\s*=\\s*([^;]+)');
  return v ? v.pop() : '';
}

export async function postJSON(url, data) {
  const res = await fetch(url, {
    method: 'POST',
    credentials: 'same-origin',
    headers: {
      'Content-Type': 'application/json',
      'X-CSRFToken': getCookie('csrftoken')
    },
    body: JSON.stringify(data)
  });
  const text = await res.text();
  try { return { ok: res.ok, status: res.status, body: JSON.parse(text) }; } catch(e) { return { ok: res.ok, status: res.status, body: text }; }
}

export async function patchJSON(url, data) {
  const res = await fetch(url, {
    method: 'PATCH',
    credentials: 'same-origin',
    headers: {
      'Content-Type': 'application/json',
      'X-CSRFToken': getCookie('csrftoken')
    },
    body: JSON.stringify(data)
  });
  const text = await res.text();
  try { return { ok: res.ok, status: res.status, body: JSON.parse(text) }; } catch(e) { return { ok: res.ok, status: res.status, body: text }; }
}

export async function getJSON(url) {
  const res = await fetch(url, { credentials: 'same-origin' });
  const text = await res.text();
  try { return { ok: res.ok, status: res.status, body: JSON.parse(text) }; } catch(e) { return { ok: res.ok, status: res.status, body: text }; }
}

let cachedDeviceId = null

export async function getActiveDeviceId() {
  if (cachedDeviceId) return cachedDeviceId

  const stored = localStorage.getItem('activeDeviceId')
  const listRes = await getJSON('/api/devices/')
  if (listRes.ok && listRes.body && Array.isArray(listRes.body.devices) && listRes.body.devices.length > 0) {
    const devices = listRes.body.devices
    const picked = stored && devices.includes(stored) ? stored : devices[0]
    cachedDeviceId = picked
    localStorage.setItem('activeDeviceId', picked)
    return picked
  }

  cachedDeviceId = stored || 'esp32-001'
  if (!stored) localStorage.setItem('activeDeviceId', cachedDeviceId)
  return cachedDeviceId
}

export default { getJSON, postJSON, patchJSON, getActiveDeviceId };
