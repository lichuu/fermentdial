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

String jsonString(const String &value) {
  String escaped = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '"' || c == '\\') {
      escaped += '\\';
    }
    if (c >= 0x20) {
      escaped += c;
    }
  }
  escaped += "\"";
  return escaped;
}
} // namespace

void NetworkManager::begin(const Settings &settings) {
  _snapshot = NetworkSnapshot{};
  _snapshot.wifiEnabled = FERM_ENABLE_NETWORK;
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _webStatus.profiles[i] = settings.profiles[i];
  }
  _webStatus.activeProfile = activeProfileIndex(settings);
  _webStatus.coolOnDeltaC = settings.coolOnDeltaC;
  _webStatus.heatOnDeltaC = settings.heatOnDeltaC;
  _webStatus.holdDeltaC = settings.holdDeltaC;
  _webStatus.tempOffsetC = settings.tempOffsetC;
  _webStatus.unitsFahrenheit = settings.unitsFahrenheit;
  _webStatus.mode = settings.mode;

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
  // TODO Stage 5: add ArduinoOTA. OTA restart hooks must force outputs OFF
  // before reboot/update.
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

void NetworkManager::publishState(uint32_t nowMs, const Settings &settings,
                                  const TemperatureSensor &sensor,
                                  const FermentationController &controller) {
  _webStatus.tempValid = sensor.isValid();
  _webStatus.tempC = sensor.temperatureC();
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _webStatus.profiles[i] = settings.profiles[i];
  }
  _webStatus.activeProfile = activeProfileIndex(settings);
  _webStatus.coolOnDeltaC = settings.coolOnDeltaC;
  _webStatus.heatOnDeltaC = settings.heatOnDeltaC;
  _webStatus.holdDeltaC = settings.holdDeltaC;
  _webStatus.tempOffsetC = settings.tempOffsetC;
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
  // External integrations should use the unit field, not topic suffixes like
  // _f.
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
  _server.on("/dashboard", HTTP_GET,
             [this]() { _server.send(200, "text/html", pageHtml()); });
  _server.on("/wifi", HTTP_GET,
             [this]() { _server.send(200, "text/html", setupHtml()); });
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
                 "<!doctype html><meta name='viewport' "
                 "content='width=device-width,initial-scale=1'>"
                 "<body "
                 "style='font-family:sans-serif;padding:2rem;background:#"
                 "071015;color:white'>"
                 "<h1>Saved</h1><p>Rebooting and joining Wi-Fi...</p></body>");
    delay(500);
    ESP.restart();
  });
  _server.on("/api/status", HTTP_GET,
             [this]() { _server.send(200, "application/json", statusJson()); });
  _server.on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
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
    _server.send(503, "application/json",
                 "{\"ok\":false,\"error\":\"settings unavailable\"}");
    return;
  }

  bool changed = false;

  if (_server.hasArg("profile")) {
    int requestedProfile = _server.arg("profile").toInt();
    if (requestedProfile < 0 || requestedProfile >= PROFILE_COUNT) {
      _server.send(400, "application/json",
                   "{\"ok\":false,\"error\":\"invalid profile\"}");
      return;
    }
    _settings->activeProfile = static_cast<uint8_t>(requestedProfile);
    changed = true;
  }

  if (_server.hasArg("target")) {
    float target = _server.arg("target").toFloat();
    setActiveTargetC(*_settings,
                     _settings->unitsFahrenheit ? fToC(target) : target);
    changed = true;
  }

  if (_server.hasArg("mode")) {
    UserMode requestedMode = _settings->mode;
    if (!parseMode(_server.arg("mode"), requestedMode)) {
      _server.send(400, "application/json",
                   "{\"ok\":false,\"error\":\"invalid mode\"}");
      return;
    }
    _settings->mode = requestedMode;
    changed = true;
  }

  if (_server.hasArg("coolOn")) {
    float coolOn = _server.arg("coolOn").toFloat();
    _settings->coolOnDeltaC =
        _settings->unitsFahrenheit ? deltaFToC(coolOn) : coolOn;
    changed = true;
  }

  if (_server.hasArg("heatOn")) {
    float heatOn = _server.arg("heatOn").toFloat();
    _settings->heatOnDeltaC =
        _settings->unitsFahrenheit ? deltaFToC(heatOn) : heatOn;
    changed = true;
  }

  if (_server.hasArg("hold")) {
    float hold = _server.arg("hold").toFloat();
    _settings->holdDeltaC = _settings->unitsFahrenheit ? deltaFToC(hold) : hold;
    changed = true;
  }

  if (_server.hasArg("tempOffset")) {
    float offset = _server.arg("tempOffset").toFloat();
    _settings->tempOffsetC =
        _settings->unitsFahrenheit ? deltaFToC(offset) : offset;
    changed = true;
  }

  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    String nameArg = String("profile") + String(i) + "Name";
    String targetArg = String("profile") + String(i) + "Target";
    if (_server.hasArg(nameArg)) {
      _settings->profiles[i].name = _server.arg(nameArg);
      changed = true;
    }
    if (_server.hasArg(targetArg)) {
      float target = _server.arg(targetArg).toFloat();
      _settings->profiles[i].targetC =
          _settings->unitsFahrenheit ? fToC(target) : target;
      changed = true;
    }
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
  const float temperature =
      _webStatus.unitsFahrenheit ? cToF(_webStatus.tempC) : _webStatus.tempC;
  const uint8_t profileIndex =
      _webStatus.activeProfile < PROFILE_COUNT ? _webStatus.activeProfile : 0;
  const float targetC = _webStatus.profiles[profileIndex].targetC;
  const float target = _webStatus.unitsFahrenheit ? cToF(targetC) : targetC;
  const float coolOn = _webStatus.unitsFahrenheit
                           ? deltaCToF(_webStatus.coolOnDeltaC)
                           : _webStatus.coolOnDeltaC;
  const float heatOn = _webStatus.unitsFahrenheit
                           ? deltaCToF(_webStatus.heatOnDeltaC)
                           : _webStatus.heatOnDeltaC;
  const float hold = _webStatus.unitsFahrenheit
                         ? deltaCToF(_webStatus.holdDeltaC)
                         : _webStatus.holdDeltaC;
  const float tempOffset = _webStatus.unitsFahrenheit
                               ? deltaCToF(_webStatus.tempOffsetC)
                               : _webStatus.tempOffsetC;
  const char *unit = _webStatus.unitsFahrenheit ? "F" : "C";

  String json = "{";
  json += "\"wifiConnected\":" +
          String(_snapshot.wifiConnected ? "true" : "false") + ",";
  json += "\"wifiStatus\":" + jsonString(_snapshot.status) + ",";
  json += "\"ip\":" + jsonString(_snapshot.ipAddress) + ",";
  json += "\"demo\":" + String(_webStatus.demoSensor ? "true" : "false") + ",";
  json +=
      "\"tempValid\":" + String(_webStatus.tempValid ? "true" : "false") + ",";
  json += "\"temperature\":" + jsonFloat(temperature) + ",";
  json += "\"target\":" + jsonFloat(target) + ",";
  json += "\"activeProfile\":" + String(profileIndex) + ",";
  json +=
      "\"profileName\":" + jsonString(_webStatus.profiles[profileIndex].name) +
      ",";
  json += "\"profiles\":[";
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    if (i > 0) {
      json += ",";
    }
    const float profileTarget = _webStatus.unitsFahrenheit
                                    ? cToF(_webStatus.profiles[i].targetC)
                                    : _webStatus.profiles[i].targetC;
    json += "{\"index\":" + String(i) +
            ",\"name\":" + jsonString(_webStatus.profiles[i].name) +
            ",\"target\":" + jsonFloat(profileTarget) + "}";
  }
  json += "],";
  json += "\"coolOn\":" + jsonFloat(coolOn) + ",";
  json += "\"heatOn\":" + jsonFloat(heatOn) + ",";
  json += "\"hold\":" + jsonFloat(hold) + ",";
  json += "\"tempOffset\":" + jsonFloat(tempOffset) + ",";
  json += "\"unit\":\"" + String(unit) + "\",";
  json += "\"mode\":\"" + String(modeTopicText(_webStatus.mode)) + "\",";
  json +=
      "\"state\":\"" +
      String(_webStatus.runtimeState == RuntimeState::Cooling &&
                     profileIndex == static_cast<uint8_t>(ProfileSlot::Crash)
                 ? "CRASHING"
                 : stateText(_webStatus.runtimeState)) +
      "\",";
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
:root{--bg:#0d1b1e;--face:#091418;--panel:#132428;--panel2:#1b3540;--line:#1e3840;--muted:#6a9aaa;--text:#d0e8f0;--accent:#b0d8f8;--blue:#356f89;--cool:#b0d8f8;--heat:#e36018;--ok:#36c87a;--gold:#ffd178;--fault:#e44840}
*{box-sizing:border-box}body{margin:0;font-family:"Trebuchet MS","Avenir Next",Verdana,sans-serif;background:var(--bg);color:var(--text)}
main{max-width:1040px;margin:auto;padding:16px}.shell{padding:0}
.top{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;margin-bottom:12px}.brand{font-weight:900;font-size:19px;color:var(--accent)}.brand span{color:var(--text)}
.statusbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.pill{border:1px solid var(--line);border-radius:999px;padding:7px 10px;background:#102126;color:var(--accent);font-size:13px}.demo{border-color:#5c4118;color:var(--gold);background:#1c160b}
.hero{border:1px solid var(--line);border-top:4px solid var(--ok);border-radius:8px;padding:18px;background:var(--face);box-shadow:inset 0 0 0 1px #071015}
body[data-state=heating] .hero{border-top-color:var(--heat)}body[data-state=cooling] .hero,body[data-state=crashing] .hero{border-top-color:var(--cool)}body[data-state=fault] .hero{border-top-color:var(--fault)}
.heroTop{display:flex;justify-content:space-between;gap:12px;color:var(--muted);font-size:13px;text-transform:uppercase}.readout{display:flex;align-items:flex-end;justify-content:space-between;gap:14px;margin-top:12px}
.tempBlock{min-width:0}.tempLine{display:flex;align-items:flex-start}.temp{font-size:78px;line-height:.9;font-weight:900;letter-spacing:0}.unit{font-size:28px;color:var(--accent);margin-left:7px;margin-top:8px}.state{font-size:25px;font-weight:900;margin-top:8px;color:var(--ok)}body[data-state=heating] .state{color:var(--heat)}body[data-state=cooling] .state,body[data-state=crashing] .state{color:var(--cool)}body[data-state=fault] .state{color:var(--fault)}
.sub{margin-top:6px;color:var(--muted)}.outputs{display:grid;gap:8px;min-width:116px}.out{border:1px solid var(--line);border-radius:8px;padding:10px;background:#102126;color:var(--muted);font-weight:900;text-align:center}.out.on.heat{background:#3b1807;color:#ffd8bf;border-color:var(--heat)}.out.on.cool{background:#123040;color:var(--accent);border-color:var(--cool)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-top:12px}.card,.panel{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:13px}.label{color:var(--muted);font-size:12px;text-transform:uppercase}.value{font-size:23px;font-weight:900;margin-top:4px}
.controls{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px}.panel h2{font-size:15px;margin:0 0 11px;color:#d9e8f4}.targetCtl{display:grid;grid-template-columns:50px 1fr 50px;gap:8px;align-items:center}
input,button,select{font:inherit;border:1px solid var(--line);border-radius:8px;padding:12px}input,select{width:100%;background:#102126;color:var(--text)}input{text-align:center;font-size:22px;font-weight:900}button{background:#234858;color:var(--text);font-weight:900;cursor:pointer}button.primary{background:var(--blue);border-color:#3f819d;color:#fff}.modes{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.active{background:var(--accent)!important;border-color:var(--accent)!important;color:#081317!important}.danger{background:#321418}.heat{background:#3b1807}.cool{background:#123040}.profileRows{display:grid;gap:8px}.profileRow{display:grid;grid-template-columns:1fr 110px;gap:8px}.thresholds{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}.thresholds label{color:var(--muted);font-size:13px}
a{color:#79d4ff}.footer{margin-top:12px;color:#8da2b0;font-size:13px}@media(max-width:760px){main{padding:12px}.controls{grid-template-columns:1fr}.readout{display:block}.outputs{grid-template-columns:1fr 1fr;margin-top:14px}.temp{font-size:58px}.unit{font-size:22px}.thresholds{grid-template-columns:1fr}}
</style></head>
<body><main><div class="shell">
<div class="top"><div class="brand">Ferment<span>Dial</span></div><div class="statusbar"><span class="pill" id="wifi">Wi-Fi</span><span class="pill demo" id="demo" hidden>DEMO SENSOR</span></div></div>
<section class="hero" id="hero">
<div class="heroTop"><span id="profileNameHero">Ferment</span><span id="targetHero">target --.-F</span></div>
<div class="readout"><div class="tempBlock"><div class="tempLine"><span class="temp" id="temp">--.-</span><span class="unit">F</span></div>
<div class="state" id="state">Loading</div><div class="sub" id="summary">Waiting for controller status</div></div>
<div class="outputs"><div class="out heat" id="heater">HEATER OFF</div><div class="out cool" id="pump">PUMP OFF</div></div></div>
</section>
<section class="grid">
<div class="card"><div class="label">Profile</div><div class="value" id="profileName">Ferment</div></div>
<div class="card"><div class="label">Setpoint</div><div class="value"><span id="target">--.-</span><span class="unit">F</span></div></div>
<div class="card"><div class="label">Mode</div><div class="value" id="mode">OFF</div></div>
</section>
<section class="controls">
<div class="panel"><h2>Active Profile</h2>
<select id="profileSelect" onchange="selectProfile()"></select>
<div class="targetCtl" style="margin-top:10px">
<button onclick="nudge(-0.1)">-</button><input id="targetInput" inputmode="decimal" step="0.1"><button onclick="nudge(0.1)">+</button>
</div><button class="primary" style="width:100%;margin-top:10px" onclick="saveActive()">Save active profile</button></div>
<div class="panel"><h2>Mode</h2><div class="modes">
<button id="btnOFF" class="danger" onclick="setMode('OFF')">OFF</button><button id="btnAUTO" onclick="setMode('AUTO')">AUTO</button>
<button id="btnHEAT_ONLY" class="heat" onclick="setMode('HEAT_ONLY')">HEAT</button><button id="btnCOOL_ONLY" class="cool" onclick="setMode('COOL_ONLY')">COOL</button>
</div></div>
<div class="panel"><h2>Profiles</h2><div id="profileRows" class="profileRows"></div>
<button class="primary" style="width:100%;margin-top:10px" onclick="saveProfiles()">Save profiles</button></div>
<div class="panel"><h2>Control</h2><div class="thresholds">
<label>Cool on <input id="coolOnInput" inputmode="decimal" step="0.1"></label>
<label>Heat on <input id="heatOnInput" inputmode="decimal" step="0.1"></label>
<label>Hold band <input id="holdInput" inputmode="decimal" step="0.1"></label>
<label>Offset <input id="offsetInput" inputmode="decimal" step="0.1"></label>
</div><button class="primary" style="width:100%;margin-top:10px" onclick="saveControl()">Save control</button></div>
</section>
<div class="footer"><span id="fault">NONE</span> - <a href="/wifi">Wi-Fi settings</a></div>
</div></main>
<script>
let last=null;
const deg=String.fromCharCode(176);
function qs(data){return new URLSearchParams(data).toString()}
async function post(data){
 await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:qs(data)});
 await tick();
}
function nudge(delta){const v=parseFloat(targetInput.value||'68');targetInput.value=(Math.round((v+delta)*10)/10).toFixed(1);saveActive()}
function saveActive(){post({profile:profileSelect.value,target:targetInput.value})}
function selectProfile(){post({profile:profileSelect.value})}
function setMode(mode){post({mode})}
function saveControl(){post({coolOn:coolOnInput.value,heatOn:heatOnInput.value,hold:holdInput.value,tempOffset:offsetInput.value})}
function saveProfiles(){
 const data={};
 for(const p of last.profiles){
  data['profile'+p.index+'Name']=document.getElementById('pName'+p.index).value;
  data['profile'+p.index+'Target']=document.getElementById('pTarget'+p.index).value;
 }
 post(data);
}
function attr(v){return String(v).replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;')}
function renderProfiles(s){
 profileSelect.innerHTML=s.profiles.map(p=>`<option value="${p.index}">${attr(p.name)}</option>`).join('');
 profileSelect.value=s.activeProfile;
 profileRows.innerHTML=s.profiles.map(p=>`<div class="profileRow"><input id="pName${p.index}" maxlength="15" value="${attr(p.name)}"><input id="pTarget${p.index}" inputmode="decimal" step="0.1" value="${p.target.toFixed(1)}"></div>`).join('');
}
async function tick(){
 const r=await fetch('/api/status'); const s=await r.json();
 last=s;
 document.body.dataset.state=s.state.toLowerCase();
 document.querySelectorAll('.unit').forEach(e=>e.textContent=deg+s.unit);
 temp.textContent=s.tempValid?s.temperature.toFixed(1):'--.-';
 profileName.textContent=s.profileName; profileNameHero.textContent=s.profileName; target.textContent=s.target.toFixed(1); targetHero.textContent='target '+s.target.toFixed(1)+deg+s.unit; if(document.activeElement!==targetInput)targetInput.value=s.target.toFixed(1);
 if(!document.querySelector('.profileRow input:focus')&&document.activeElement!==profileSelect)renderProfiles(s);
 if(document.activeElement!==coolOnInput)coolOnInput.value=s.coolOn.toFixed(1);
 if(document.activeElement!==heatOnInput)heatOnInput.value=s.heatOn.toFixed(1);
 if(document.activeElement!==holdInput)holdInput.value=s.hold.toFixed(1);
 if(document.activeElement!==offsetInput)offsetInput.value=s.tempOffset.toFixed(1);
 state.textContent=s.state; mode.textContent=s.mode; wifi.textContent=s.wifiConnected?s.ip:s.wifiStatus; demo.hidden=!s.demo;
 heater.textContent=s.heater?'HEATER ON':'HEATER OFF'; pump.textContent=s.pump?'PUMP ON':'PUMP OFF'; heater.classList.toggle('on',s.heater); pump.classList.toggle('on',s.pump); fault.textContent=s.fault;
 summary.textContent=s.tempValid?s.mode+' mode':'Sensor fault - outputs forced off';
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

} // namespace ferm
