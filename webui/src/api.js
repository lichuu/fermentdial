export function getStatus() {
  return fetch('/api/status').then((r) => r.json());
}

export function getHistory() {
  return fetch('/api/history').then((r) => r.json());
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

export function postSettings(fields) {
  return fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(fields).toString(),
  }).then((r) => r.json());
}

export function postForm(path, fields) {
  return fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(fields).toString(),
  });
}
