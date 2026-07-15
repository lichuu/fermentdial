/** Human-readable mode label (API still uses HEAT_ONLY / COOL_ONLY). */
export function formatMode(mode) {
  if (!mode) return 'OFF';
  return String(mode).replaceAll('_', ' ');
}

/** GitHub Releases page for a firmware version (accepts "0.2.6" or "v0.2.6"). */
export function releaseTagUrl(version) {
  const v = String(version || '').replace(/^v/i, '').trim();
  if (!v) return 'https://github.com/lichuu/fermentdial/releases';
  return `https://github.com/lichuu/fermentdial/releases/tag/v${v}`;
}

async function parseJsonResponse(r) {
  if (!r.ok) {
    throw new Error('HTTP ' + r.status);
  }
  return r.json();
}

export function getStatus() {
  return fetch('/api/status').then(parseJsonResponse);
}

export function getHistory() {
  return fetch('/api/history').then(parseJsonResponse);
}

export function getHistoryCsv() {
  return fetch('/api/history.csv').then((r) => {
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return r.text();
  });
}

export function getWifiScan() {
  return fetch('/api/wifi/scan').then((r) => r.json());
}

export function getSettingsConfig() {
  return fetch('/api/settings').then((r) => r.json());
}

export function getProgram() {
  return fetch('/api/program').then((r) => r.json());
}

export function getEvents() {
  return fetch('/api/events').then((r) => r.json());
}

export function getSelfCheck() {
  return fetch('/api/selfcheck').then((r) => r.json());
}

export function postSettings(fields, options = {}) {
  return fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(fields).toString(),
    signal: options.signal,
  }).then(parseJsonResponse);
}

/** Live backlight preview while dragging; does not persist to NVS. */
export function postBrightnessPreview(brightness, options = {}) {
  return postSettings({ brightness, brightnessPreview: 1 }, options);
}

export function postForm(path, fields) {
  return fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(fields).toString(),
  });
}
