#include "network.h"

#if FERM_ENABLE_NETWORK
#include <WiFi.h>
#endif

namespace ferm {

namespace {
constexpr const char *SETUP_AP_SSID = "FermentDial-Setup";
constexpr const char *SETUP_AP_PASSWORD = "fermentdial";
IPAddress SETUP_IP(192, 168, 4, 1);
IPAddress SETUP_GATEWAY(192, 168, 4, 1);
IPAddress SETUP_MASK(255, 255, 255, 0);

String jsonFloat(float value, unsigned int decimals = 1) {
  return isnan(value) ? "null" : String(value, decimals);
}
}  // namespace

void NetworkManager::begin(const Settings &settings) {
  (void)settings;
  _snapshot = NetworkSnapshot{};
  _snapshot.wifiEnabled = FERM_ENABLE_NETWORK;

#if FERM_ENABLE_NETWORK
  _prefs.begin("net", false);
  _wifiSsid = _prefs.getString("ssid", FERM_WIFI_SSID);
  _wifiPassword = _prefs.getString("pass", FERM_WIFI_PASSWORD);
  _snapshot.wifiConfigured = _wifiSsid.length() > 0;
  _snapshot.ssid = _wifiSsid;

  WiFi.persistent(false);
  WiFi.setHostname(FERM_WIFI_HOSTNAME);

  if (_snapshot.wifiConfigured) {
    WiFi.mode(WIFI_STA);
    startWifi(millis());
    startWebServer();
  } else {
    startSetupPortal();
  }

  // TODO Stage 4: add MQTT client and Home Assistant discovery.
  // Local control must continue if Wi-Fi/MQTT/Home Assistant is unavailable.
#endif

#if FERM_ENABLE_OTA
  // TODO Stage 5: add ArduinoOTA. OTA restart hooks must force outputs OFF before reboot/update.
#endif
}

void NetworkManager::update(uint32_t nowMs, Settings &settings) {
#if FERM_ENABLE_NETWORK
  _settings = &settings;
  if (_serverStarted) {
    _server.handleClient();
  }
  if (_apMode) {
    _dns.processNextRequest();
    _snapshot.wifiConnected = false;
    _snapshot.ipAddress = WiFi.softAPIP().toString();
    _snapshot.status = "Setup AP";
    return;
  }

  wl_status_t status = WiFi.status();
  _snapshot.wifiConnected = status == WL_CONNECTED;
  if (_snapshot.wifiConnected) {
    _snapshot.ipAddress = WiFi.localIP().toString();
    _snapshot.status = _snapshot.ipAddress;
  } else {
    _snapshot.ipAddress = "";
    _snapshot.status = "Connecting";
    if (nowMs - _lastWifiAttemptMs >= 15000UL) {
      startWifi(nowMs);
    }
  }

  // TODO Stage 4: process MQTT setpoint/mode commands.
  // Never let MQTT override safety rules; it should only mutate Settings.
#else
  (void)settings;
  (void)nowMs;
#endif

#if FERM_ENABLE_OTA
  // TODO Stage 5: ArduinoOTA.handle().
#endif
}

void NetworkManager::publishState(uint32_t nowMs, const Settings &settings, const TemperatureSensor &sensor,
                                  const FermentationController &controller) {
  _webStatus.tempValid = sensor.isValid();
  _webStatus.tempF = sensor.temperatureF();
  _webStatus.targetF = settings.targetF;
  _webStatus.unitsFahrenheit = settings.unitsFahrenheit;
  _webStatus.mode = settings.mode;
  _webStatus.runtimeState = controller.runtimeState();
  _webStatus.faultCode = controller.faultCode();
  _webStatus.heaterOn = controller.heaterOn();
  _webStatus.pumpOn = controller.pumpOn();
  _webStatus.demoSensor = sensor.demoMode();

  if (nowMs - _lastPublishMs < 5000) {
    return;
  }
  _lastPublishMs = nowMs;

#if FERM_ENABLE_NETWORK
  // TODO Stage 4 MQTT topic namespace: fermentdial
  // Prefer unit-aware JSON state:
  // fermentdial/state -> {"temperature":68.1,"target":68.0,"unit":"F",...}
  // External integrations should use the unit field, not topic suffixes like _f.
#endif
}

bool NetworkManager::consumeSettingsChanged() {
  bool changed = _settingsChanged;
  _settingsChanged = false;
  return changed;
}

bool NetworkManager::requestSetupPortal() {
#if FERM_ENABLE_NETWORK
  startSetupPortal();
  return true;
#else
  return false;
#endif
}

void NetworkManager::startWifi(uint32_t nowMs) {
#if FERM_ENABLE_NETWORK
  if (_wifiSsid.length() == 0) {
    startSetupPortal();
    return;
  }
  _lastWifiAttemptMs = nowMs;
  _snapshot.status = "Connecting";
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(_wifiSsid.c_str(), _wifiPassword.c_str());
#else
  (void)nowMs;
#endif
}

void NetworkManager::startSetupPortal() {
#if FERM_ENABLE_NETWORK
  _apMode = true;
  _snapshot.wifiConfigured = false;
  _snapshot.ssid = SETUP_AP_SSID;
  _snapshot.status = "Setup AP";

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(SETUP_IP, SETUP_GATEWAY, SETUP_MASK);
  WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD);
  _snapshot.ipAddress = WiFi.softAPIP().toString();
  _dns.start(53, "*", SETUP_IP);
  startWebServer();
#endif
}

void NetworkManager::startWebServer() {
#if FERM_ENABLE_NETWORK
  if (_serverStarted) {
    return;
  }

  _server.on("/", HTTP_GET, [this]() {
    _server.send(200, "text/html", _apMode ? setupHtml() : pageHtml());
  });
  _server.on("/dashboard", HTTP_GET, [this]() {
    _server.send(200, "text/html", pageHtml());
  });
  _server.on("/wifi", HTTP_GET, [this]() {
    _server.send(200, "text/html", setupHtml());
  });
  _server.on("/wifi", HTTP_POST, [this]() {
    String ssid = _server.arg("ssid");
    String pass = _server.arg("pass");
    ssid.trim();
    if (ssid.length() == 0) {
      _server.send(400, "text/plain", "SSID is required");
      return;
    }
    _prefs.putString("ssid", ssid);
    _prefs.putString("pass", pass);
    _server.send(200, "text/html",
                 "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'>"
                 "<body style='font-family:sans-serif;padding:2rem;background:#071015;color:white'>"
                 "<h1>Saved</h1><p>Rebooting and joining Wi-Fi...</p></body>");
    delay(500);
    ESP.restart();
  });
  _server.on("/api/status", HTTP_GET, [this]() {
    _server.send(200, "application/json", statusJson());
  });
  _server.on("/api/settings", HTTP_POST, [this]() {
    handleSettingsPost();
  });
  _server.onNotFound([this]() {
    _server.sendHeader("Location", "/", true);
    _server.send(302, "text/plain", "");
  });

  _server.begin();
  _serverStarted = true;
#endif
}

void NetworkManager::handleSettingsPost() {
#if FERM_ENABLE_NETWORK
  if (_settings == nullptr) {
    _server.send(503, "application/json", "{\"ok\":false,\"error\":\"settings unavailable\"}");
    return;
  }

  bool changed = false;

  if (_server.hasArg("target")) {
    float target = _server.arg("target").toFloat();
    _settings->targetF = _settings->unitsFahrenheit ? target : cToF(target);
    changed = true;
  }

  if (_server.hasArg("mode")) {
    UserMode requestedMode = _settings->mode;
    if (!parseMode(_server.arg("mode"), requestedMode)) {
      _server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid mode\"}");
      return;
    }
    _settings->mode = requestedMode;
    changed = true;
  }

  if (_server.hasArg("hysteresisF")) {
    _settings->hysteresisF = _server.arg("hysteresisF").toFloat();
    changed = true;
  }

  if (_server.hasArg("tempOffsetF")) {
    _settings->tempOffsetF = _server.arg("tempOffsetF").toFloat();
    changed = true;
  }

  if (changed) {
    sanitizeSettings(*_settings);
    _settingsChanged = true;
  }

  _server.send(200, "application/json", statusJson());
#endif
}

bool NetworkManager::parseMode(const String &value, UserMode &mode) const {
  String normalized = value;
  normalized.trim();
  normalized.toUpperCase();

  if (normalized == "OFF") {
    mode = UserMode::Off;
    return true;
  }
  if (normalized == "AUTO") {
    mode = UserMode::Auto;
    return true;
  }
  if (normalized == "HEAT" || normalized == "HEAT_ONLY") {
    mode = UserMode::HeatOnly;
    return true;
  }
  if (normalized == "COOL" || normalized == "COOL_ONLY") {
    mode = UserMode::CoolOnly;
    return true;
  }
  return false;
}

String NetworkManager::statusJson() const {
  const float temperature = _webStatus.unitsFahrenheit ? _webStatus.tempF : fToC(_webStatus.tempF);
  const float target = _webStatus.unitsFahrenheit ? _webStatus.targetF : fToC(_webStatus.targetF);
  const char *unit = _webStatus.unitsFahrenheit ? "F" : "C";

  String json = "{";
  json += "\"wifiConnected\":" + String(_snapshot.wifiConnected ? "true" : "false") + ",";
  json += "\"wifiStatus\":\"" + _snapshot.status + "\",";
  json += "\"ip\":\"" + _snapshot.ipAddress + "\",";
  json += "\"demo\":" + String(_webStatus.demoSensor ? "true" : "false") + ",";
  json += "\"tempValid\":" + String(_webStatus.tempValid ? "true" : "false") + ",";
  json += "\"temperature\":" + jsonFloat(temperature) + ",";
  json += "\"target\":" + jsonFloat(target) + ",";
  json += "\"unit\":\"" + String(unit) + "\",";
  json += "\"mode\":\"" + String(modeTopicText(_webStatus.mode)) + "\",";
  json += "\"state\":\"" + String(stateText(_webStatus.runtimeState)) + "\",";
  json += "\"fault\":\"" + String(faultText(_webStatus.faultCode)) + "\",";
  json += "\"heater\":" + String(_webStatus.heaterOn ? "true" : "false") + ",";
  json += "\"pump\":" + String(_webStatus.pumpOn ? "true" : "false");
  json += "}";
  return json;
}

String NetworkManager::pageHtml() const {
  return R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FermentDial</title>
<style>
*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff}
main{max-width:860px;margin:auto;padding:18px}.shell{border-radius:24px;padding:20px;background:#101b24;box-shadow:0 12px 42px #0008}
.top{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}.brand{font-weight:800;letter-spacing:.03em}
.pill{border-radius:999px;padding:9px 12px;background:#1b2a34;color:#c9d8e8;font-size:14px}.demo{background:#493718;color:#ffd88a}
.hero{margin-top:16px;border-radius:22px;padding:24px;background:linear-gradient(145deg,#142938,#071015);min-height:230px}
.temp{font-size:76px;line-height:.95;font-weight:850;letter-spacing:-1px}.unit{font-size:30px;color:#bed0dc}.state{margin-top:10px;font-size:28px;font-weight:800}
.sub{margin-top:8px;color:#a9bac8}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin-top:14px}
.card{border-radius:18px;background:#0b151d;padding:14px}.label{color:#91a8b8;font-size:13px}.value{font-size:24px;font-weight:750;margin-top:4px}
.controls{display:grid;grid-template-columns:1.2fr .8fr;gap:14px;margin-top:14px}@media(max-width:700px){.controls{grid-template-columns:1fr}.temp{font-size:58px}}
.panel{border-radius:20px;background:#0b151d;padding:16px}.panel h2{font-size:16px;margin:0 0 12px;color:#d9e8f4}
.targetCtl{display:grid;grid-template-columns:54px 1fr 54px;gap:8px;align-items:center}
input,button{font:inherit;border:0;border-radius:14px;padding:14px}input{width:100%;background:#172633;color:#fff;text-align:center;font-size:28px;font-weight:800}
button{background:#1e3444;color:#ecf8ff;font-weight:800;cursor:pointer}button.primary{background:#0a91d8}.modes{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.active{background:#0a91d8!important}.danger{background:#6e1414!important}.heat{background:#af241f!important}.cool{background:#1063b8!important}
a{color:#79d4ff}.footer{margin-top:12px;color:#8da2b0;font-size:13px}
</style></head>
<body><main><div class="shell">
<div class="top"><div class="brand">FermentDial</div><div><span class="pill" id="wifi">Wi-Fi</span> <span class="pill demo" id="demo" hidden>DEMO SENSOR</span></div></div>
<section class="hero" id="hero">
<div class="temp"><span id="temp">--.-</span><span class="unit">F</span></div>
<div class="state" id="state">Loading</div><div class="sub" id="summary">Waiting for controller status</div>
</section>
<section class="grid">
<div class="card"><div class="label">Setpoint</div><div class="value"><span id="target">--.-</span><span class="unit">F</span></div></div>
<div class="card"><div class="label">Mode</div><div class="value" id="mode">OFF</div></div>
<div class="card"><div class="label">Heater</div><div class="value" id="heater">OFF</div></div>
<div class="card"><div class="label">Pump</div><div class="value" id="pump">OFF</div></div>
</section>
<section class="controls">
<div class="panel"><h2>Setpoint</h2><div class="targetCtl">
<button onclick="nudge(-0.1)">-</button><input id="targetInput" inputmode="decimal" step="0.1"><button onclick="nudge(0.1)">+</button>
</div><button class="primary" style="width:100%;margin-top:10px" onclick="saveTarget()">Save setpoint</button></div>
<div class="panel"><h2>Mode</h2><div class="modes">
<button id="btnOFF" class="danger" onclick="setMode('OFF')">OFF</button><button id="btnAUTO" onclick="setMode('AUTO')">AUTO</button>
<button id="btnHEAT_ONLY" class="heat" onclick="setMode('HEAT_ONLY')">HEAT</button><button id="btnCOOL_ONLY" class="cool" onclick="setMode('COOL_ONLY')">COOL</button>
</div></div>
</section>
<div class="footer"><span id="fault">NONE</span> - <a href="/wifi">Wi-Fi settings</a></div>
</div></main>
<script>
let last=null;
function qs(data){return new URLSearchParams(data).toString()}
async function post(data){
 await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:qs(data)});
 await tick();
}
function nudge(delta){const v=parseFloat(targetInput.value||'68');targetInput.value=(Math.round((v+delta)*10)/10).toFixed(1);saveTarget()}
function saveTarget(){post({target:targetInput.value})}
function setMode(mode){post({mode})}
async function tick(){
 const r=await fetch('/api/status'); const s=await r.json();
 last=s;
 const bg=s.state==='HEATING'?'#380707':s.state==='COOLING'?'#031a32':s.state==='FAULT'?'#360000':'#071015';
 document.body.style.background=bg; hero.style.background=s.state==='HEATING'?'linear-gradient(145deg,#7a1714,#160808)':s.state==='COOLING'?'linear-gradient(145deg,#0e5299,#071015)':s.state==='FAULT'?'linear-gradient(145deg,#6c1010,#120606)':'linear-gradient(145deg,#142938,#071015)';
 document.querySelectorAll('.unit').forEach(e=>e.textContent=s.unit);
 temp.textContent=s.tempValid?s.temperature.toFixed(1):'--.-';
 target.textContent=s.target.toFixed(1); if(document.activeElement!==targetInput)targetInput.value=s.target.toFixed(1);
 state.textContent=s.state; mode.textContent=s.mode; wifi.textContent=s.wifiConnected?s.ip:s.wifiStatus; demo.hidden=!s.demo;
 heater.textContent=s.heater?'ON':'OFF'; pump.textContent=s.pump?'ON':'OFF'; fault.textContent=s.fault;
 summary.textContent=s.tempValid?'Target '+s.target.toFixed(1)+s.unit+' - '+s.mode:'Sensor fault - outputs forced off';
 for(const id of ['OFF','AUTO','HEAT_ONLY','COOL_ONLY'])document.getElementById('btn'+id).classList.toggle('active',s.mode===id);
}
tick(); setInterval(tick,2000);
</script></body></html>)HTML";
}

String NetworkManager::setupHtml() const {
  return R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FermentDial Wi-Fi</title>
<style>
body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0}
main{max-width:480px;margin:auto;padding:24px}.card{border-radius:24px;background:#101b24;padding:22px}
input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0 16px;padding:14px;border-radius:14px;border:0}
button{background:#0aa4ff;color:white;font-weight:700}.hint{color:#a9bac8}
</style></head><body><main><div class="card">
<h1>FermentDial Wi-Fi</h1>
<p class="hint">Join your fermentation controller to your home Wi-Fi.</p>
<p><a href="/dashboard">Open live dashboard</a></p>
<form method="post" action="/wifi">
<label>Wi-Fi name</label><input name="ssid" autocomplete="off" required>
<label>Password</label><input name="pass" type="password">
<button type="submit">Save and reboot</button>
</form>
<p class="hint">Setup AP: FermentDial-Setup / fermentdial</p>
</div></main></body></html>)HTML";
}

}  // namespace ferm
