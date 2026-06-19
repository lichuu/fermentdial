#include "network.h"

#include "history.h"
#include "network_detail.h"
#include "time_sync.h"

#if FERM_ENABLE_NETWORK
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_random.h>
#endif

#if FERM_ENABLE_NETWORK && FERM_ENABLE_OTA
#include <Update.h>
#endif

#if FERM_ENABLE_NETWORK
#include "web_assets.h"
#endif

namespace ferm {

using namespace network_detail;

String NetworkManager::assetVersionQuery() const {
  // ?v= is for cache-busting. Due to no-cache/must-revalidate headers on js/css/manifest,
  // the query only meaningfully affects the long-lived icon assets.
  return String("?v=") + String(FIRMWARE_GIT_SHA);
}

String NetworkManager::webAppHead(const char *title, bool appCss,
                                    const char *extraHead) const {
  const String q = assetVersionQuery();
  String html = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#071015">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="FermentDial">
<meta name="mobile-web-app-capable" content="yes">
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="manifest" href="/manifest.webmanifest)HTML";
  html += q;
  html += R"HTML(">
<link rel="apple-touch-icon" href="/icon-192.png)HTML";
  html += q;
  html += R"HTML(">
)HTML";
  if (appCss) {
    html += R"HTML(<link rel="stylesheet" href="/app.css)HTML";
    html += q;
    html += R"HTML(">
)HTML";
  }
  if (extraHead != nullptr) {
    html += extraHead;
  }
  html += R"HTML(<title>)HTML";
  html += title;
  html += R"HTML(</title>
</head>)HTML";
  return html;
}

String NetworkManager::pageHtml() const {
  String html = webAppHead("FermentDial", /*appCss=*/true);
  html += R"HTML(
<body data-page="dashboard"><div id="app"></div>
<script src="/app.js)HTML";
  html += assetVersionQuery();
  html += R"HTML("></script>
</body></html>)HTML";
  return html;
}

String NetworkManager::settingsHtml() const {
  String html = webAppHead("FermentDial Settings", /*appCss=*/true);
  html += R"HTML(
<body data-page="settings"><div id="app"></div>
<script src="/app.js)HTML";
  html += assetVersionQuery();
  html += R"HTML("></script>
</body></html>)HTML";
  return html;
}

String NetworkManager::setupHtml() const {
  String html = webAppHead("FermentDial Wi-Fi", /*appCss=*/false, SETUP_STYLE);
  html += R"HTML(<body><main><div class="card">
<h1>FermentDial Wi-Fi</h1>
<p class="hint">Join your fermentation controller to your home Wi-Fi.</p>
<p><a href="/dashboard">Open live dashboard</a></p>
<p><a href="/settings">Device settings</a></p>
<form method="post" action="/wifi">
<label>Wi-Fi name</label><input name="ssid" autocomplete="off" required>
<button type="button" onclick="scanWifi()">Scan for networks</button>
<p class="hint scanStatus" id="wifiScanStatus">Select a scanned network or enter the name manually.</p>
<div class="networkList" id="wifiNetworks"></div>
<label>Password</label><input name="pass" type="password">
<button type="submit">Save and reboot</button>
</form>
<script>
function pickWifi(ssid){document.querySelector('input[name=ssid]').value=ssid}
async function scanWifi(){
 const status=document.getElementById('wifiScanStatus'),list=document.getElementById('wifiNetworks');
 status.textContent='Scanning...'; list.innerHTML='';
 try{
  const r=await fetch('/api/wifi/scan'); if(!r.ok)throw new Error('scan failed');
  const data=await r.json(),nets=(data.networks||[]).filter(n=>n.ssid);
  if(!nets.length){status.textContent='No networks found. Enter the name manually.';return}
  status.textContent='Select a network or enter the name manually.';
  for(const n of nets){
   const b=document.createElement('button'),name=document.createElement('span'),meta=document.createElement('span');
   b.type='button'; name.textContent=n.ssid; meta.className='networkMeta'; meta.textContent=(n.secure?'secured':'open')+' '+n.rssi+' dBm';
   b.appendChild(name); b.appendChild(meta); b.onclick=function(){pickWifi(n.ssid)}; list.appendChild(b);
  }
 }catch(e){status.textContent='Scan failed. Enter the name manually.'}
}
</script>
<p class="hint">Setup AP: )HTML";
  html += htmlEscape(setupApSsid());
  html += R"HTML( (open network)</p>
</div></main></body></html>)HTML";
  return html;
}

String NetworkManager::loginHtml(bool showError) const {
  String html = webAppHead("FermentDial Login", /*appCss=*/false, LOGIN_STYLE);
  html += R"HTML(<body><main><div class="card">
<h1>FermentDial</h1>
<p class="hint">Enter the admin password to reach device settings.</p>
)HTML";
  if (showError) {
    html += R"HTML(<p class="err">Incorrect password. Try again.</p>)HTML";
  }
  html += R"HTML(<form method="post" action="/login">
<label>Password</label><input name="password" type="password" autofocus required>
<button type="submit">Log in</button>
</form>
<p><a href="/dashboard">Back to dashboard</a></p>
</div></main></body></html>)HTML";
  return html;
}

String NetworkManager::firmwareHtml() const {
  constexpr const char *FIRMWARE_STYLE = R"HTML(<style>
html{background:#071015}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0;min-height:100vh}
main{max-width:520px;margin:auto;padding:24px}.card{border-radius:8px;background:#132428;border:1px solid #1e3840;padding:22px}
h1{margin-top:0;color:#b0d8f8}input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0 16px;padding:14px;border-radius:8px;border:1px solid #1e3840}
input{background:#102126;color:#d0e8f0}button{background:#356f89;color:white;font-weight:900}.hint{color:#a9bac8}.warn{color:#ffd178}
a{color:#79d4ff}
</style>)HTML";
  String html = webAppHead("FermentDial Firmware", /*appCss=*/false, FIRMWARE_STYLE);
  html += R"HTML(<body><main><div class="card">
<h1>Firmware Update</h1>
<p class="warn">Outputs turn off before the update starts. The controller reboots after a successful upload.</p>
<form method="post" action="/firmware" enctype="multipart/form-data">
<label>Firmware .bin</label><input type="file" name="firmware" accept=".bin" required>
<button type="submit">Upload and reboot</button>
</form>
<p class="hint">Build with <code>uv run platformio run -e m5stack_dial_demo</code>, then upload <code>.pio/build/m5stack_dial_demo/firmware.bin</code>.</p>
<p><a href="/wifi">Wi-Fi settings</a> - <a href="/dashboard">Dashboard</a></p>
</div></main></body></html>)HTML";
  return html;
}

} // namespace ferm
