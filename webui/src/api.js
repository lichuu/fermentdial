/** Human-readable mode label (API still uses HEAT_ONLY / COOL_ONLY). */
export function formatMode(mode) {
  if (!mode) return 'OFF';
  return String(mode).replaceAll('_', ' ');
}

export function getStatus() {
  return fetch('/api/status').then((r) => r.json());
}

export function getHistory() {
  return fetch('/api/history').then((r) => r.json());
}

export function getHistoryCsv() {
  return fetch('/api/history.csv').then((r) => r.text());
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
  }).then((r) => r.json());
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
