#include "network.h"

#include "history.h"
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

namespace {
constexpr const char *SETUP_AP_SSID_PREFIX = "FermentDial-Setup";
constexpr const char *BREWFATHER_DEFAULT_URL =
    "https://log.brewfather.net/stream";
constexpr uint32_t BREWFATHER_MIN_INTERVAL_SECONDS = 15UL * 60UL;
constexpr uint32_t BREWFATHER_MAX_INTERVAL_SECONDS = 24UL * 60UL * 60UL;
// The onboarding access point is open (no password) — it only exists before the
// device has joined a network, carries no secrets, and reboots away after setup.
// The web config pages also ship unlocked; users opt into a password under
// Settings > Security.

// On-brand favicon: a thermostat dial gauge in the dashboard palette. Served as
// SVG so it scales crisply and costs almost nothing in flash.
constexpr const char *FAVICON_SVG =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">"
    "<rect width=\"32\" height=\"32\" rx=\"7\" fill=\"#0d1b1e\"/>"
    "<path d=\"M8.2 23 A10 10 0 1 1 23.8 23\" fill=\"none\" stroke=\"#1e3840\" "
    "stroke-width=\"3\" stroke-linecap=\"round\"/>"
    "<path d=\"M8.2 23 A10 10 0 0 1 16 6\" fill=\"none\" stroke=\"#36c87a\" "
    "stroke-width=\"3\" stroke-linecap=\"round\"/>"
    "<line x1=\"16\" y1=\"16\" x2=\"21\" y2=\"10.5\" stroke=\"#b0d8f8\" "
    "stroke-width=\"2.4\" stroke-linecap=\"round\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"2.6\" fill=\"#b0d8f8\"/></svg>";

IPAddress SETUP_IP(192, 168, 4, 1);
IPAddress SETUP_GATEWAY(192, 168, 4, 1);
IPAddress SETUP_MASK(255, 255, 255, 0);

String deviceSuffix(bool uppercase) {
  char suffix[5];
  snprintf(suffix, sizeof(suffix), uppercase ? "%04X" : "%04x",
           static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFF));
  return String(suffix);
}

String setupApSsid() {
  const String suffix = deviceSuffix(true);
  return String(SETUP_AP_SSID_PREFIX) + "-" + suffix;
}

String hostnameToken(String value) {
  value.trim();
  value.toLowerCase();

  String token = "";
  token.reserve(value.length());
  bool pendingHyphen = false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alnum) {
      if (pendingHyphen && token.length() > 0) {
        token += '-';
      }
      token += c;
      pendingHyphen = false;
    } else {
      pendingHyphen = token.length() > 0;
    }
  }
  return token;
}

String networkHostname(const String &fermenterName) {
  String hostname = hostnameToken(FERM_WIFI_HOSTNAME_BASE);
  if (hostname.length() == 0) {
    hostname = "fermentdial";
  }

  const String name = hostnameToken(fermenterName);
  const String defaultName = hostnameToken(DEFAULT_FERMENTER_NAME);
  const String suffix = deviceSuffix(false);

  if (name.length() > 0 && name != defaultName) {
    hostname += "-";
    hostname += name;
  }

  const size_t maxPrefixLength = 63 - 1 - suffix.length();
  if (hostname.length() > maxPrefixLength) {
    hostname = hostname.substring(0, maxPrefixLength);
    while (hostname.endsWith("-")) {
      hostname.remove(hostname.length() - 1);
    }
  }

  hostname += "-";
  hostname += suffix;
  return hostname;
}

String jsonFloat(float value, unsigned int decimals = 1) {
  return isnan(value) ? "null" : String(value, decimals);
}

String jsonIntOrNull(int32_t value, int32_t nullValue = INT32_MIN) {
  return value == nullValue ? "null" : String(value);
}

String hydrometerTypeText(HydrometerType type) {
  switch (type) {
  case HydrometerType::Tilt:
    return "TILT";
  case HydrometerType::Rapt:
    return "RAPT";
  default:
    return "UNKNOWN";
  }
}

String hydrometerAgeText(uint32_t nowMs, uint32_t lastSeenMs) {
  if (lastSeenMs == 0) {
    return "null";
  }
  return String((nowMs - lastSeenMs) / 1000UL);
}

String hydrometerDeviceName(const HydrometerReading &reading, uint8_t index) {
  if (reading.label.length() > 0) {
    return reading.label;
  }
  if (reading.name.length() > 0) {
    return reading.name;
  }
  if (reading.color.length() > 0) {
    return hydrometerTypeText(reading.type) + " " + reading.color;
  }
  return String("Hydrometer ") + String(index + 1);
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

// Parse one encoded step: "type,exit,targetDisplay,durationSeconds,gravity,
// stableHours". Missing fields default to 0; out-of-range enums are clamped.
ProfileStep parseProgramStep(const String &stepStr, bool fahrenheit) {
  ProfileStep step;
  String parts[6];
  uint8_t count = 0;
  int fieldStart = 0;
  while (count < 6) {
    int comma = stepStr.indexOf(',', fieldStart);
    if (comma < 0) {
      parts[count++] = stepStr.substring(fieldStart);
      break;
    }
    parts[count++] = stepStr.substring(fieldStart, comma);
    fieldStart = comma + 1;
  }
  long typeVal = parts[0].toInt();
  if (typeVal < 0 || typeVal > static_cast<long>(StepType::ManualWait)) {
    typeVal = static_cast<long>(StepType::Hold);
  }
  step.type = static_cast<StepType>(typeVal);
  long exitVal = parts[1].toInt();
  if (exitVal < 0 || exitVal > static_cast<long>(StepExit::Manual)) {
    exitVal = static_cast<long>(StepExit::Time);
  }
  step.exit = static_cast<StepExit>(exitVal);
  step.targetC = fromDisplayTemp(parts[2].toFloat(), fahrenheit);
  float dur = parts[3].toFloat();
  step.durationSeconds = dur > 0 ? static_cast<uint32_t>(dur) : 0;
  step.gravityThreshold = parts[4].toFloat();
  long stable = parts[5].toInt();
  if (stable < 0) {
    stable = 0;
  } else if (stable > 65535) {
    stable = 65535;
  }
  step.stableHours = static_cast<uint16_t>(stable);
  return step;
}

// Parse a program's steps from "step;step;..." into the program slot.
void parseProgramSteps(ProgramSettings &program, const String &encoded,
                       bool fahrenheit) {
  program.stepCount = 0;
  const int len = encoded.length();
  int start = 0;
  while (start < len && program.stepCount < MAX_PROGRAM_STEPS) {
    int semi = encoded.indexOf(';', start);
    String stepStr =
        (semi < 0) ? encoded.substring(start) : encoded.substring(start, semi);
    stepStr.trim();
    if (stepStr.length() > 0) {
      program.steps[program.stepCount++] = parseProgramStep(stepStr, fahrenheit);
    }
    if (semi < 0) {
      break;
    }
    start = semi + 1;
  }
}

String htmlEscape(const String &value) {
  String escaped = "";
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '&') {
      escaped += F("&amp;");
    } else if (c == '<') {
      escaped += F("&lt;");
    } else if (c == '>') {
      escaped += F("&gt;");
    } else if (c == '"') {
      escaped += F("&quot;");
    } else if (c >= 0x20) {
      escaped += c;
    }
  }
  return escaped;
}

String prometheusLabelEscape(const String &value) {
  String escaped = "";
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '\\' || c == '"') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\n') {
      escaped += F("\\n");
    } else if (c >= 0x20) {
      escaped += c;
    }
  }
  return escaped;
}

String influxEscape(const String &value) {
  String escaped = "";
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == ' ' || c == ',' || c == '=' || c == '\\') {
      escaped += '\\';
    }
    if (c >= 0x20) {
      escaped += c;
    }
  }
  return escaped;
}

bool isUrlSafe(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
         c == '~';
}

// Sanitize a user-supplied base name so it is valid as both an InfluxDB
// measurement and a Prometheus metric-name prefix. Invalid characters become
// '_', empty falls back to "fermentdial", and a leading digit is prefixed so
// the result is a legal Prometheus identifier.
String sanitizeMetricBase(String value) {
  value.trim();
  String out = "";
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_';
    out += ok ? c : '_';
  }
  if (out.length() == 0) {
    return "fermentdial";
  }
  if (out.charAt(0) >= '0' && out.charAt(0) <= '9') {
    out = "_" + out;
  }
  return out;
}

String urlEncode(const String &value) {
  const char *hex = "0123456789ABCDEF";
  String encoded = "";
  encoded.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    uint8_t c = static_cast<uint8_t>(value.charAt(i));
    if (isUrlSafe(static_cast<char>(c))) {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String stripTrailingSlash(String value) {
  value.trim();
  while (value.endsWith("/")) {
    value.remove(value.length() - 1);
  }
  return value;
}

uint8_t runtimeStateNumber(RuntimeState state) {
  return static_cast<uint8_t>(state);
}

uint8_t userModeNumber(UserMode mode) {
  return static_cast<uint8_t>(mode);
}

const char *influxTargetValue(InfluxExportTarget target) {
  switch (target) {
  case InfluxExportTarget::V2:
    return "2";
  case InfluxExportTarget::V3:
    return "3";
  case InfluxExportTarget::VictoriaMetrics:
    return "vm";
  case InfluxExportTarget::V1:
  default:
    return "1";
  }
}

InfluxExportTarget parseInfluxTarget(String value) {
  value.trim();
  value.toLowerCase();
  if (value == "2" || value == "v2") {
    return InfluxExportTarget::V2;
  }
  if (value == "3" || value == "v3") {
    return InfluxExportTarget::V3;
  }
  if (value == "vm" || value == "victoriametrics") {
    return InfluxExportTarget::VictoriaMetrics;
  }
  return InfluxExportTarget::V1;
}

const char *exportPayloadScopeValue(ExportPayloadScope scope) {
  switch (scope) {
  case ExportPayloadScope::HydrometerOnly:
    return "hydrometer";
  case ExportPayloadScope::ControllerAndHydrometer:
  default:
    return "all";
  }
}

ExportPayloadScope parseExportPayloadScope(String value) {
  value.trim();
  value.toLowerCase();
  if (value == "hydrometer" || value == "hydrometer_only" ||
      value == "hydro") {
    return ExportPayloadScope::HydrometerOnly;
  }
  return ExportPayloadScope::ControllerAndHydrometer;
}

void appendField(String &fields, const String &field) {
  if (fields.length() > 0) {
    fields += ",";
  }
  fields += field;
}

void appendJsonField(String &json, const String &field) {
  if (!json.endsWith("{")) {
    json += ",";
  }
  json += field;
}

void appendJsonNumber(String &json, const char *name, float value,
                      unsigned int decimals) {
  appendJsonField(json, jsonString(String(name)) + ":" +
                            String(value, decimals));
}

String brewfatherDeviceState(RuntimeState state, UserMode mode,
                             bool diacetylRestActive) {
  if (mode == UserMode::Off || state == RuntimeState::Off) {
    return "off";
  }
  if (diacetylRestActive) {
    return "on";
  }
  switch (state) {
  case RuntimeState::Heating:
    return "heating";
  case RuntimeState::Cooling:
    return "cooling";
  case RuntimeState::Fault:
    return "fault";
  case RuntimeState::Idle:
    return "on";
  case RuntimeState::Boot:
  default:
    return "off";
  }
}

String normalizeBrewfatherUrl(String value) {
  value.trim();
  if (value.length() == 0) {
    return BREWFATHER_DEFAULT_URL;
  }
  return value;
}

String normalizeBrewfatherId(String value) {
  value.trim();
  const int idIndex = value.indexOf("id=");
  if (idIndex >= 0) {
    int start = idIndex + 3;
    int end = value.indexOf('&', start);
    if (end < 0) {
      end = value.length();
    }
    value = value.substring(start, end);
    value.trim();
  }
  return value;
}

// Pull a single cookie value out of a raw Cookie header ("a=1; b=2").
String cookieValue(const String &cookieHeader, const char *name) {
  const String key = String(name) + "=";
  int idx = cookieHeader.indexOf(key);
  if (idx < 0) {
    return "";
  }
  idx += key.length();
  int end = cookieHeader.indexOf(';', idx);
  if (end < 0) {
    end = cookieHeader.length();
  }
  String value = cookieHeader.substring(idx, end);
  value.trim();
  return value;
}

} // namespace

void NetworkManager::begin(const Settings &settings,
                           const HydrometerManager &hydrometer) {
  _snapshot = NetworkSnapshot{};
  _hydrometer = &hydrometer;
  _snapshot.wifiEnabled = FERM_ENABLE_NETWORK;
  _snapshot.otaEnabled = FERM_ENABLE_OTA;
  _hostname = networkHostname(settings.fermenterName);
  _snapshot.hostname = _hostname;
  _webStatus.fermenterName = settings.fermenterName;
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _webStatus.profiles[i] = settings.profiles[i];
  }
  _webStatus.activeProfile = activeProfileIndex(settings);
  _webStatus.liveTargetC = currentTargetC(settings);
  _webStatus.diacetylRestActive = settings.diacetylRestActive;
  _webStatus.diacetylRestTargetC = settings.diacetylRestTargetC;
  _webStatus.diacetylRestDurationSeconds =
      settings.diacetylRestDurationSeconds;
  _webStatus.diacetylRestRemainingSeconds =
      settings.diacetylRestRemainingSeconds;
  _webStatus.diacetylRestReturnProfile =
      diacetylRestReturnProfileIndex(settings);
  _webStatus.coolOnDeltaC = settings.coolOnDeltaC;
  _webStatus.heatOnDeltaC = settings.heatOnDeltaC;
  _webStatus.holdDeltaC = settings.holdDeltaC;
  _webStatus.tempOffsetC = settings.tempOffsetC;
  _webStatus.unitsFahrenheit = settings.unitsFahrenheit;
  _webStatus.brightness = settings.brightness;
  _webStatus.hydrometerBleEnabled = settings.hydrometerBleEnabled;
  _webStatus.hydrometerScanType = settings.hydrometerScanType;
  _webStatus.gradualCrashEnabled = settings.gradualCrashEnabled;
  _webStatus.gradualCrashStepC = settings.gradualCrashStepC;
  _webStatus.gradualCrashStepIntervalHours = settings.gradualCrashStepIntervalHours;
  _webStatus.mode = settings.mode;

#if FERM_ENABLE_NETWORK
  _prefs.begin("net", false);
  loadInfluxConfig();
  loadMqttConfig();
  loadBrewfatherConfig();
  _wifiSsid = _prefs.getString("ssid", FERM_WIFI_SSID);
  _wifiPassword = _prefs.getString("pass", FERM_WIFI_PASSWORD);
  // Ships unlocked: config pages are open until the user sets a password.
  _adminPassword = _prefs.getString("adminPass", "");
  _snapshot.wifiConfigured = _wifiSsid.length() > 0;
  _snapshot.ssid = _wifiSsid;

  WiFi.persistent(false);

  if (_snapshot.wifiConfigured) {
    WiFi.mode(WIFI_STA);
    startWifi(millis());
    startWebServer();
  } else {
    startSetupPortal();
  }

  // MQTT publishing is driven from update()/publishMqtt(). Home Assistant
  // discovery is not implemented yet; local control continues regardless of
  // Wi-Fi/MQTT availability.
  _mqtt.setBufferSize(4096);
  timeSyncBegin();
#endif

}

void NetworkManager::setFirmwareUpdateSafetyCallback(
    FirmwareUpdateSafetyCallback callback) {
  _firmwareUpdateSafetyCallback = callback;
}

void NetworkManager::update(uint32_t nowMs, Settings &settings) {
#if FERM_ENABLE_NETWORK
  _settings = &settings;
  _snapshot.mqttEnabled = _mqttConfig.enabled;
  const String nextHostname = networkHostname(settings.fermenterName);
  if (nextHostname != _hostname) {
    _hostname = nextHostname;
    _snapshot.hostname = _hostname;
  }
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
  timeSyncLoop(nowMs, _snapshot.wifiConnected);
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

  if (_mqttConfig.enabled && _snapshot.wifiConnected &&
      _mqttConfig.host.length() > 0) {
    if (!_mqtt.connected()) {
      mqttConnect(nowMs);
    }
    _mqtt.loop();
  } else if (_mqtt.connected()) {
    _mqtt.disconnect();
  }
  _snapshot.mqttConnected = _mqtt.connected();
  // TODO: subscribe to setpoint/mode commands. MQTT must never override
  // safety rules; it should only mutate Settings.
#else
  (void)settings;
  (void)nowMs;
#endif
}

void NetworkManager::publishState(uint32_t nowMs, const Settings &settings,
                                  const TemperatureSensor &sensor,
                                  const FermentationController &controller,
                                  const HydrometerManager &hydrometer) {
  _hydrometer = &hydrometer;
  _webStatus.tempValid = sensor.isValid();
  _webStatus.tempC = sensor.temperatureC();
  recordHistory(nowMs, _webStatus.tempValid, _webStatus.tempC);
  _webStatus.fermenterName = settings.fermenterName;
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _webStatus.profiles[i] = settings.profiles[i];
  }
  _webStatus.activeProfile = activeProfileIndex(settings);
  _webStatus.liveTargetC = currentTargetC(settings);
  _webStatus.diacetylRestActive = settings.diacetylRestActive;
  _webStatus.diacetylRestTargetC = settings.diacetylRestTargetC;
  _webStatus.diacetylRestDurationSeconds =
      settings.diacetylRestDurationSeconds;
  _webStatus.diacetylRestRemainingSeconds =
      settings.diacetylRestRemainingSeconds;
  _webStatus.diacetylRestReturnProfile =
      diacetylRestReturnProfileIndex(settings);
  _webStatus.coolOnDeltaC = settings.coolOnDeltaC;
  _webStatus.heatOnDeltaC = settings.heatOnDeltaC;
  _webStatus.holdDeltaC = settings.holdDeltaC;
  _webStatus.tempOffsetC = settings.tempOffsetC;
  _webStatus.unitsFahrenheit = settings.unitsFahrenheit;
  _webStatus.brightness = settings.brightness;
  _webStatus.hydrometerBleEnabled = settings.hydrometerBleEnabled;
  _webStatus.hydrometerScanType = settings.hydrometerScanType;
  _webStatus.gradualCrashEnabled = settings.gradualCrashEnabled;
  _webStatus.gradualCrashStepC = settings.gradualCrashStepC;
  _webStatus.gradualCrashStepIntervalHours = settings.gradualCrashStepIntervalHours;
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
  publishInflux(nowMs);
  publishMqtt(nowMs);
  publishBrewfather(nowMs);
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
  _snapshot.hostname = _hostname;
  WiFi.disconnect(false, false);
  WiFi.setHostname(_hostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(_wifiSsid.c_str(), _wifiPassword.c_str());
#else
  (void)nowMs;
#endif
}

void NetworkManager::startSetupPortal() {
#if FERM_ENABLE_NETWORK
  const String ssid = setupApSsid();
  _apMode = true;
  _snapshot.wifiConfigured = false;
  _snapshot.ssid = ssid;
  _snapshot.status = "Setup AP";

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(SETUP_IP, SETUP_GATEWAY, SETUP_MASK);
  WiFi.softAP(ssid.c_str());  // open network for onboarding
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

  // Expose the Cookie header so the session check can read it.
  static const char *kHeaderKeys[] = {"Cookie"};
  _server.collectHeaders(kHeaderKeys, 1);

  _server.on("/", HTTP_GET, [this]() {
    _server.send(200, "text/html; charset=utf-8",
                 _apMode ? setupHtml() : pageHtml());
  });
  _server.on("/dashboard", HTTP_GET, [this]() {
    _server.send(200, "text/html; charset=utf-8", pageHtml());
  });
  _server.on("/favicon.svg", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "max-age=86400");
    _server.send(200, "image/svg+xml", FAVICON_SVG);
  });
  _server.on("/app.js", HTTP_GET, [this]() {
    _server.sendHeader("Content-Encoding", "gzip");
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send_P(200, "application/javascript; charset=utf-8",
                   reinterpret_cast<PGM_P>(APP_JS_GZ), APP_JS_GZ_LEN);
  });
  _server.on("/app.css", HTTP_GET, [this]() {
    _server.sendHeader("Content-Encoding", "gzip");
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send_P(200, "text/css; charset=utf-8",
                   reinterpret_cast<PGM_P>(APP_CSS_GZ), APP_CSS_GZ_LEN);
  });
  _server.on("/login", HTTP_GET, [this]() { handleLogin(); });
  _server.on("/login", HTTP_POST, [this]() { handleLogin(); });
  _server.on("/logout", HTTP_GET, [this]() { handleLogout(); });
  _server.on("/logout", HTTP_POST, [this]() { handleLogout(); });
  _server.on("/settings", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "text/html; charset=utf-8", settingsHtml());
  });
  _server.on("/settings/device", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    handleDeviceSettingsPost();
  });
  _server.on("/settings/security", HTTP_POST,
             [this]() { handleSecurityPost(); });
  _server.on("/settings/influx", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    handleInfluxSettingsPost();
  });
  _server.on("/settings/mqtt", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    handleMqttSettingsPost();
  });
  _server.on("/settings/brewfather", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    handleBrewfatherSettingsPost();
  });
  _server.on("/metrics", HTTP_GET, [this]() {
    _server.send(200, "text/plain; version=0.0.4; charset=utf-8",
                 metricsText(millis()));
  });
  _server.on("/wifi", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "text/html; charset=utf-8", setupHtml());
  });
  _server.on("/wifi", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    String ssid = _server.arg("ssid");
    String pass = _server.arg("pass");
    ssid.trim();
    if (ssid.length() == 0) {
      _server.send(400, "text/plain", "SSID is required");
      return;
    }
    _prefs.putString("ssid", ssid);
    if (pass.length() > 0 || _wifiPassword.length() == 0) {
      _prefs.putString("pass", pass);
    }
    _wifiSsid = ssid;
    if (pass.length() > 0 || _wifiPassword.length() == 0) {
      _wifiPassword = pass;
    }
    _server.send(200, "text/html; charset=utf-8",
                 "<!doctype html><meta name='viewport' "
                 "content='width=device-width,initial-scale=1'>"
                 "<body "
                 "style='font-family:sans-serif;padding:2rem;background:#"
                 "071015;color:white'>"
                 "<h1>Saved</h1><p>Rebooting and joining Wi-Fi...</p></body>");
    delay(500);
    ESP.restart();
  });
#if FERM_ENABLE_OTA
  _server.on("/firmware", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "text/html; charset=utf-8", firmwareHtml());
  });
  _server.on(
      "/firmware", HTTP_POST,
      [this]() {
        if (!requireAuth()) {
          return;
        }
        const bool ok = _firmwareUpdateOk && !_firmwareUpdateHadError;
        const String message =
            ok ? "Firmware updated. Rebooting..."
               : (_firmwareUpdateError.length() > 0 ? _firmwareUpdateError
                                                     : "Firmware update failed");
        String html =
            "<!doctype html><meta name='viewport' "
            "content='width=device-width,initial-scale=1'>"
            "<body style='font-family:sans-serif;padding:2rem;background:#"
            "071015;color:white'><h1>" +
            String(ok ? "Update complete" : "Update failed") + "</h1><p>" +
            message + "</p><p><a href='/firmware'>Back</a></p></body>";
        _server.sendHeader("Connection", "close");
        _server.send(ok ? 200 : 500, "text/html; charset=utf-8", html);
        if (ok) {
          delay(800);
          ESP.restart();
        }
      },
      [this]() { handleFirmwareUpload(); });
#endif
  _server.on("/api/status", HTTP_GET,
             [this]() {
               _server.send(200, "application/json", statusJson(millis()));
             });
  _server.on("/api/history", HTTP_GET, [this]() {
    _server.send(200, "application/json", historyJson());
  });
  _server.on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
  _server.on("/api/settings", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "application/json", settingsConfigJson());
  });
  _server.on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
  _server.on("/api/program", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "application/json", programJson());
  });
  _server.on("/api/events", HTTP_GET, [this]() {
    _server.send(200, "application/json",
                 _eventLog != nullptr ? _eventLog->toJson() : String("[]"));
  });
  _server.on("/api/history.csv", HTTP_GET, [this]() {
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.sendHeader("Content-Disposition",
                       "attachment; filename=fermentdial-history.csv");
    _server.send(200, "text/csv", "");
    _server.sendContent(HISTORY_CSV_HEADER);
    streamHistoryFile(HISTORY_CSV_PRIOR_PATH);
    streamHistoryFile(HISTORY_CSV_PATH);
    _server.sendContent("");
  });
  _server.onNotFound([this]() {
    _server.sendHeader("Location", "/", true);
    _server.send(302, "text/plain", "");
  });

  _server.begin();
  _serverStarted = true;
#endif
}

void NetworkManager::handleFirmwareUpload() {
#if FERM_ENABLE_NETWORK && FERM_ENABLE_OTA
  HTTPUpload &upload = _server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    _firmwareUpdateInProgress = true;
    _firmwareUpdateHadError = false;
    _firmwareUpdateOk = false;
    _firmwareUpdateError = "";

    // Reject before a single byte is written if the session isn't authorized;
    // the WRITE/END steps bail out on this error so nothing is ever flashed.
    if (!isAuthed()) {
      _firmwareUpdateHadError = true;
      _firmwareUpdateError = "Authentication required";
      return;
    }

    if (_firmwareUpdateSafetyCallback != nullptr) {
      _firmwareUpdateSafetyCallback();
    }

    Serial.print(F("Firmware upload: "));
    Serial.println(upload.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      _firmwareUpdateHadError = true;
      _firmwareUpdateError = "Unable to begin firmware update";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!_firmwareUpdateHadError &&
        Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      _firmwareUpdateHadError = true;
      _firmwareUpdateError = "Firmware write failed";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!_firmwareUpdateHadError && Update.end(true)) {
      _firmwareUpdateOk = true;
      Serial.println(F("Firmware upload complete"));
    } else {
      _firmwareUpdateHadError = true;
      if (_firmwareUpdateError.length() == 0) {
        _firmwareUpdateError = "Firmware verification failed";
      }
      Update.printError(Serial);
    }
    _firmwareUpdateInProgress = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    _firmwareUpdateHadError = true;
    _firmwareUpdateError = "Firmware upload aborted";
    _firmwareUpdateInProgress = false;
    Serial.println(F("Firmware upload aborted"));
  }
#endif
}

void NetworkManager::handleWifiScan() {
#if FERM_ENABLE_NETWORK
  const int count = WiFi.scanNetworks(false, true);
  if (count < 0) {
    _server.send(500, "application/json",
                 "{\"error\":\"Wi-Fi scan failed\",\"networks\":[]}");
    return;
  }

  String json = "{\"networks\":[";
  for (int i = 0; i < count; ++i) {
    if (i > 0) {
      json += ",";
    }
    json += "{\"ssid\":" + jsonString(WiFi.SSID(i)) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"channel\":" + String(WiFi.channel(i)) + ",";
    json += "\"secure\":" +
            String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false"
                                                             : "true") +
            "}";
  }
  json += "]}";
  WiFi.scanDelete();
  _server.send(200, "application/json", json);
#endif
}

String NetworkManager::newSessionToken() {
#if FERM_ENABLE_NETWORK
  char buf[33];
  for (int i = 0; i < 4; ++i) {
    snprintf(buf + i * 8, 9, "%08x", static_cast<unsigned>(esp_random()));
  }
  return String(buf);
#else
  return "";
#endif
}

bool NetworkManager::isAuthed() {
#if FERM_ENABLE_NETWORK
  if (_apMode) {
    return true;  // setup portal is already gated by the AP Wi-Fi password
  }
  if (_adminPassword.length() == 0) {
    return true;  // lock explicitly disabled
  }
  if (_sessionToken.length() == 0) {
    return false;  // no active session
  }
  return cookieValue(_server.header("Cookie"), "fdsession") == _sessionToken;
#else
  return true;
#endif
}

bool NetworkManager::requireAuth() {
#if FERM_ENABLE_NETWORK
  if (isAuthed()) {
    return true;
  }
  _server.sendHeader("Location", "/login", true);
  _server.send(302, "text/plain", "");
  return false;
#else
  return true;
#endif
}

void NetworkManager::handleLogin() {
#if FERM_ENABLE_NETWORK
  if (_server.method() == HTTP_GET) {
    if (isAuthed()) {
      _server.sendHeader("Location", "/settings", true);
      _server.send(302, "text/plain", "");
      return;
    }
    _server.send(200, "text/html; charset=utf-8", loginHtml(false));
    return;
  }

  // POST: validate the submitted password and start a session.
  const String pass = _server.arg("password");
  if (_adminPassword.length() > 0 && pass == _adminPassword) {
    _sessionToken = newSessionToken();
    _server.sendHeader("Set-Cookie", "fdsession=" + _sessionToken +
                                         "; Path=/; HttpOnly; SameSite=Lax; "
                                         "Max-Age=604800");
    _server.sendHeader("Location", "/settings", true);
    _server.send(302, "text/plain", "");
    return;
  }
  _server.send(401, "text/html; charset=utf-8", loginHtml(true));
#endif
}

void NetworkManager::handleLogout() {
#if FERM_ENABLE_NETWORK
  _sessionToken = "";
  _server.sendHeader("Set-Cookie", "fdsession=; Path=/; Max-Age=0");
  _server.sendHeader("Location", "/dashboard", true);
  _server.send(302, "text/plain", "");
#endif
}

void NetworkManager::handleSecurityPost() {
#if FERM_ENABLE_NETWORK
  if (!requireAuth()) {
    return;
  }
  const String np = _server.arg("newPassword");
  const String cp = _server.arg("confirmPassword");
  if (np != cp) {
    _server.send(400, "text/html; charset=utf-8",
                 "<!doctype html><meta name='viewport' "
                 "content='width=device-width,initial-scale=1'>"
                 "<body style='font-family:sans-serif;padding:2rem;"
                 "background:#071015;color:#f8fbff'><h1>Passwords did not "
                 "match</h1><p><a href='/settings'>Back to settings</a></p>"
                 "</body>");
    return;
  }
  _adminPassword = np;
  _prefs.putString("adminPass", _adminPassword);
  // Re-issue the current session so this browser stays in; any other browser's
  // old token is now invalid. Disabling the lock just clears the session.
  if (_adminPassword.length() > 0) {
    _sessionToken = newSessionToken();
    _server.sendHeader("Set-Cookie", "fdsession=" + _sessionToken +
                                         "; Path=/; HttpOnly; SameSite=Lax; "
                                         "Max-Age=604800");
  } else {
    _sessionToken = "";
    _server.sendHeader("Set-Cookie", "fdsession=; Path=/; Max-Age=0");
  }
  _server.sendHeader("Location", "/settings", true);
  _server.send(302, "text/plain", "");
#endif
}

void NetworkManager::handleDeviceSettingsPost() {
#if FERM_ENABLE_NETWORK
  if (_settings == nullptr) {
    _server.send(503, "text/plain", "settings unavailable");
    return;
  }
  if (_server.hasArg("fermenterName")) {
    _settings->fermenterName = _server.arg("fermenterName");
    sanitizeSettings(*_settings);
    _webStatus.fermenterName = _settings->fermenterName;
    _hostname = networkHostname(_settings->fermenterName);
    _snapshot.hostname = _hostname;
    _settingsChanged = true;
  }
  if (_server.hasArg("brightness")) {
    int brightness = _server.arg("brightness").toInt();
    if (brightness < MIN_BRIGHTNESS) {
      brightness = MIN_BRIGHTNESS;
    } else if (brightness > MAX_BRIGHTNESS) {
      brightness = MAX_BRIGHTNESS;
    }
    _settings->brightness = static_cast<uint8_t>(brightness);
    _webStatus.brightness = _settings->brightness;
    _settingsChanged = true;
  }
  _server.sendHeader("Location", "/settings", true);
  _server.send(303, "text/plain", "");
#endif
}

void NetworkManager::handleInfluxSettingsPost() {
#if FERM_ENABLE_NETWORK
  _influx.enabled = _server.hasArg("influxEnabled");
  if (_server.hasArg("influxTarget")) {
    _influx.target = parseInfluxTarget(_server.arg("influxTarget"));
  }
  if (_server.hasArg("influxUrl")) {
    _influx.url = _server.arg("influxUrl");
    _influx.url.trim();
  }
  if (_server.hasArg("influxDatabase")) {
    _influx.database = _server.arg("influxDatabase");
    _influx.database.trim();
  }
  if (_server.hasArg("influxRetentionPolicy")) {
    _influx.retentionPolicy = _server.arg("influxRetentionPolicy");
    _influx.retentionPolicy.trim();
  }
  if (_server.hasArg("influxUsername")) {
    _influx.username = _server.arg("influxUsername");
    _influx.username.trim();
  }
  if (_server.hasArg("influxPassword") &&
      _server.arg("influxPassword").length() > 0) {
    _influx.password = _server.arg("influxPassword");
  }
  if (_server.hasArg("clearInfluxPassword")) {
    _influx.password = "";
  }
  if (_server.hasArg("influxOrg")) {
    _influx.org = _server.arg("influxOrg");
    _influx.org.trim();
  }
  if (_server.hasArg("influxBucket")) {
    _influx.bucket = _server.arg("influxBucket");
    _influx.bucket.trim();
  }
  if (_server.hasArg("influxToken") &&
      _server.arg("influxToken").length() > 0) {
    _influx.token = _server.arg("influxToken");
  }
  if (_server.hasArg("clearInfluxToken")) {
    _influx.token = "";
  }
  if (_server.hasArg("influxInterval")) {
    uint32_t interval = _server.arg("influxInterval").toInt();
    if (interval < 10) {
      interval = 10;
    }
    if (interval > 3600) {
      interval = 3600;
    }
    _influx.intervalSeconds = interval;
  }
  if (_influx.database.length() == 0) {
    _influx.database = "fermentdial";
  }
  if (_influx.bucket.length() == 0) {
    _influx.bucket = "fermentdial";
  }
  if (_server.hasArg("influxMeasurement")) {
    _influx.measurement = sanitizeMetricBase(_server.arg("influxMeasurement"));
  }
  saveInfluxConfig();
  _lastInfluxStatus = _influx.enabled ? "Saved" : "Disabled";
  _server.sendHeader("Location", "/settings", true);
  _server.send(303, "text/plain", "");
#endif
}

void NetworkManager::loadInfluxConfig() {
#if FERM_ENABLE_NETWORK
  _influx.enabled = _prefs.getBool("influxEn", false);
  uint8_t target = _prefs.getUChar("influxTarget", 1);
  if (target < 1 || target > 4) {
    target = 1;
  }
  _influx.target = static_cast<InfluxExportTarget>(target);
  _influx.url = _prefs.getString("influxUrl", "");
  _influx.database = _prefs.getString("influxDb", "fermentdial");
  _influx.retentionPolicy = _prefs.getString("influxRp", "");
  _influx.username = _prefs.getString("influxUser", "");
  _influx.password = _prefs.getString("influxPass", "");
  _influx.org = _prefs.getString("influxOrg", "");
  _influx.bucket = _prefs.getString("influxBucket", "fermentdial");
  _influx.token = _prefs.getString("influxToken", "");
  _influx.measurement =
      sanitizeMetricBase(_prefs.getString("influxMeas", "fermentdial"));
  _influx.intervalSeconds = _prefs.getUInt("influxEvery", 30);
  if (_influx.intervalSeconds < 10) {
    _influx.intervalSeconds = 10;
  }
  if (_influx.intervalSeconds > 3600) {
    _influx.intervalSeconds = 3600;
  }
  _lastInfluxStatus = _influx.enabled ? "Waiting" : "Disabled";
#endif
}

void NetworkManager::saveInfluxConfig() {
#if FERM_ENABLE_NETWORK
  _prefs.putBool("influxEn", _influx.enabled);
  _prefs.putUChar("influxTarget", static_cast<uint8_t>(_influx.target));
  _prefs.putString("influxUrl", _influx.url);
  _prefs.putString("influxDb", _influx.database);
  _prefs.putString("influxRp", _influx.retentionPolicy);
  _prefs.putString("influxUser", _influx.username);
  _prefs.putString("influxPass", _influx.password);
  _prefs.putString("influxOrg", _influx.org);
  _prefs.putString("influxBucket", _influx.bucket);
  _prefs.putString("influxToken", _influx.token);
  _prefs.putString("influxMeas", _influx.measurement);
  _prefs.putUInt("influxEvery", _influx.intervalSeconds);
#endif
}

String NetworkManager::influxLineProtocol(uint32_t nowMs) const {
  const uint8_t profileIndex =
      _webStatus.activeProfile < PROFILE_COUNT ? _webStatus.activeProfile : 0;
  const ProfileSettings &profile = _webStatus.profiles[profileIndex];
  HydrometerReading selected;
  if (_hydrometer != nullptr && _settings != nullptr) {
    selected = _hydrometer->selectedReading(*_settings, nowMs);
  }

  String fields = "";
  if (_webStatus.tempValid && !isnan(_webStatus.tempC)) {
    appendField(fields, "temp_c=" + String(_webStatus.tempC, 3));
  }
  appendField(fields, "target_c=" + String(_webStatus.liveTargetC, 3));
  appendField(fields, "diacetyl_rest_active=" +
                          String(_webStatus.diacetylRestActive ? "1i" : "0i"));
  appendField(fields, "diacetyl_rest_target_c=" +
                          String(_webStatus.diacetylRestTargetC, 3));
  appendField(fields, "diacetyl_rest_remaining_s=" +
                          String(_webStatus.diacetylRestRemainingSeconds) +
                          "i");
  appendField(fields, "cool_on_delta_c=" + String(_webStatus.coolOnDeltaC, 3));
  appendField(fields, "heat_on_delta_c=" + String(_webStatus.heatOnDeltaC, 3));
  appendField(fields, "hold_delta_c=" + String(_webStatus.holdDeltaC, 3));
  appendField(fields, "offset_c=" + String(_webStatus.tempOffsetC, 3));
  appendField(fields, String("sensor_valid=") +
                          (_webStatus.tempValid ? "1i" : "0i"));
  appendField(fields,
              String("heater_on=") + (_webStatus.heaterOn ? "1i" : "0i"));
  appendField(fields,
              String("pump_on=") + (_webStatus.pumpOn ? "1i" : "0i"));
  appendField(fields,
              String("demo=") + (_webStatus.demoSensor ? "1i" : "0i"));
  appendField(fields,
              "mode=" + String(userModeNumber(_webStatus.mode)) + "i");
  appendField(fields, "state=" +
                          String(runtimeStateNumber(_webStatus.runtimeState)) +
                          "i");
  appendField(fields, "fault=" +
                          String(static_cast<uint8_t>(_webStatus.faultCode)) +
                          "i");
  if (selected.valid) {
    appendField(fields, "hydrometer_gravity=" + String(selected.gravity, 3));
    appendField(fields, "hydrometer_temperature_c=" +
                            String(selected.temperatureC, 3));
    appendField(fields, "hydrometer_rssi=" + String(selected.rssi) + "i");
    if (!isnan(selected.batteryV)) {
      appendField(fields, "hydrometer_battery_v=" + String(selected.batteryV, 2));
    }
    if (!isnan(selected.originalGravity)) {
      appendField(fields, "hydrometer_original_gravity=" +
                              String(selected.originalGravity, 3));
    }
    if (!isnan(selected.abv)) {
      appendField(fields, "hydrometer_abv=" + String(selected.abv, 2));
    }
    appendField(fields, "hydrometer_stable_s=" +
                            String(selected.stableSeconds) + "i");
    appendField(fields, "hydrometer_stale=" +
                            String(selected.stale ? "1i" : "0i"));
  }

  String line = _influx.measurement;
  line += ",fermenter=" + influxEscape(_webStatus.fermenterName);
  line += ",profile=" + influxEscape(profile.name);
  line += " ";
  line += fields;
  return line;
}

void NetworkManager::publishInflux(uint32_t nowMs) {
#if FERM_ENABLE_NETWORK
  if (!_influx.enabled) {
    _lastInfluxStatus = "Disabled";
    return;
  }
  if (!_snapshot.wifiConnected || _apMode) {
    _lastInfluxStatus = "Waiting for Wi-Fi";
    return;
  }
  const uint32_t intervalMs = _influx.intervalSeconds * 1000UL;
  if (nowMs - _lastInfluxPublishMs < intervalMs) {
    return;
  }
  _lastInfluxPublishMs = nowMs;

  String base = stripTrailingSlash(_influx.url);
  if (base.length() == 0) {
    _lastInfluxStatus = "No URL";
    return;
  }

  String endpoint = base;
  if (_influx.target == InfluxExportTarget::V2) {
    if (endpoint.indexOf("/api/v2/write") < 0) {
      endpoint += "/api/v2/write";
    }
    endpoint += (endpoint.indexOf('?') >= 0 ? "&" : "?");
    endpoint += "bucket=" + urlEncode(_influx.bucket) + "&precision=s";
    if (_influx.org.length() > 0) {
      endpoint += "&org=" + urlEncode(_influx.org);
    }
  } else if (_influx.target == InfluxExportTarget::V3) {
    if (endpoint.indexOf("/api/v3/write_lp") < 0) {
      endpoint += "/api/v3/write_lp";
    }
    endpoint += (endpoint.indexOf('?') >= 0 ? "&" : "?");
    endpoint += "db=" + urlEncode(_influx.database) + "&precision=second";
  } else {
    if (!endpoint.endsWith("/write") && endpoint.indexOf("/influx/write") < 0) {
      endpoint += "/write";
    }
    if (_influx.target == InfluxExportTarget::V1) {
      endpoint += (endpoint.indexOf('?') >= 0 ? "&" : "?");
      endpoint += "db=" + urlEncode(_influx.database) + "&precision=s";
      if (_influx.retentionPolicy.length() > 0) {
        endpoint += "&rp=" + urlEncode(_influx.retentionPolicy);
      }
      if (_influx.username.length() > 0) {
        endpoint += "&u=" + urlEncode(_influx.username);
      }
      if (_influx.password.length() > 0) {
        endpoint += "&p=" + urlEncode(_influx.password);
      }
    }
  }

  HTTPClient http;
  if (!http.begin(endpoint)) {
    _lastInfluxStatus = "Bad URL";
    return;
  }
  http.addHeader("Content-Type", "text/plain");
  if (_influx.target == InfluxExportTarget::V2 && _influx.token.length() > 0) {
    http.addHeader("Authorization", "Token " + _influx.token);
  } else if (_influx.target == InfluxExportTarget::V3 &&
             _influx.token.length() > 0) {
    http.addHeader("Authorization", "Bearer " + _influx.token);
  } else if (_influx.target == InfluxExportTarget::VictoriaMetrics) {
    http.addHeader("Stream-Mode", "1");
  }

  const int code = http.POST(influxLineProtocol(nowMs));
  _lastInfluxStatusCode = code;
  if (code >= 200 && code < 300) {
    _lastInfluxStatus = "OK " + String(code);
  } else if (code > 0) {
    _lastInfluxStatus = "HTTP " + String(code);
  } else {
    _lastInfluxStatus = "POST failed " + String(code);
  }
  http.end();
#else
  (void)nowMs;
#endif
}

namespace {
String mqttBaseTopic(const MqttConfig &config) {
  String base = config.baseTopic;
  base.trim();
  if (base.length() == 0) {
    base = "fermentdial";
  }
  while (base.endsWith("/")) {
    base = base.substring(0, base.length() - 1);
  }
  return base;
}
} // namespace

void NetworkManager::loadMqttConfig() {
#if FERM_ENABLE_NETWORK
  _mqttConfig.enabled = _prefs.getBool("mqttEn", false);
  _mqttConfig.payloadScope = parseExportPayloadScope(
      _prefs.getString("mqttScope", exportPayloadScopeValue(
                                      ExportPayloadScope::ControllerAndHydrometer)));
  _mqttConfig.haDiscovery = _prefs.getBool("mqttHaDisc", true);
  _mqttConfig.discoveryPrefix =
      _prefs.getString("mqttDiscPfx", "homeassistant");
  _mqttConfig.discoveryPrefix.trim();
  if (_mqttConfig.discoveryPrefix.length() == 0) {
    _mqttConfig.discoveryPrefix = "homeassistant";
  }
  _mqttConfig.host = _prefs.getString("mqttHost", "");
  _mqttConfig.port = _prefs.getUShort("mqttPort", 1883);
  _mqttConfig.username = _prefs.getString("mqttUser", "");
  _mqttConfig.password = _prefs.getString("mqttPass", "");
  _mqttConfig.baseTopic = _prefs.getString("mqttTopic", "fermentdial");
  _mqttConfig.intervalSeconds = _prefs.getUInt("mqttEvery", 30);
  if (_mqttConfig.baseTopic.length() == 0) {
    _mqttConfig.baseTopic = "fermentdial";
  }
  if (_mqttConfig.port == 0) {
    _mqttConfig.port = 1883;
  }
  if (_mqttConfig.intervalSeconds < 5) {
    _mqttConfig.intervalSeconds = 5;
  }
  if (_mqttConfig.intervalSeconds > 3600) {
    _mqttConfig.intervalSeconds = 3600;
  }
  _lastMqttStatus = _mqttConfig.enabled ? "Waiting" : "Disabled";
#endif
}

void NetworkManager::saveMqttConfig() {
#if FERM_ENABLE_NETWORK
  _prefs.putBool("mqttEn", _mqttConfig.enabled);
  _prefs.putString("mqttScope",
                   exportPayloadScopeValue(_mqttConfig.payloadScope));
  _prefs.putBool("mqttHaDisc", _mqttConfig.haDiscovery);
  _prefs.putString("mqttDiscPfx", _mqttConfig.discoveryPrefix);
  _prefs.putString("mqttHost", _mqttConfig.host);
  _prefs.putUShort("mqttPort", _mqttConfig.port);
  _prefs.putString("mqttUser", _mqttConfig.username);
  _prefs.putString("mqttPass", _mqttConfig.password);
  _prefs.putString("mqttTopic", _mqttConfig.baseTopic);
  _prefs.putUInt("mqttEvery", _mqttConfig.intervalSeconds);
#endif
}

void NetworkManager::handleMqttSettingsPost() {
#if FERM_ENABLE_NETWORK
  _mqttConfig.enabled = _server.hasArg("mqttEnabled");
  if (_server.hasArg("mqttPayloadScope")) {
    _mqttConfig.payloadScope =
        parseExportPayloadScope(_server.arg("mqttPayloadScope"));
  }
  _mqttConfig.haDiscovery = _server.hasArg("mqttHaDiscovery");
  if (_server.hasArg("mqttDiscoveryPrefix")) {
    _mqttConfig.discoveryPrefix = _server.arg("mqttDiscoveryPrefix");
    _mqttConfig.discoveryPrefix.trim();
    if (_mqttConfig.discoveryPrefix.length() == 0) {
      _mqttConfig.discoveryPrefix = "homeassistant";
    }
  }
  if (_server.hasArg("mqttHost")) {
    _mqttConfig.host = _server.arg("mqttHost");
    _mqttConfig.host.trim();
  }
  if (_server.hasArg("mqttPort")) {
    uint32_t port = _server.arg("mqttPort").toInt();
    if (port == 0 || port > 65535) {
      port = 1883;
    }
    _mqttConfig.port = static_cast<uint16_t>(port);
  }
  if (_server.hasArg("mqttUsername")) {
    _mqttConfig.username = _server.arg("mqttUsername");
    _mqttConfig.username.trim();
  }
  if (_server.hasArg("mqttPassword") &&
      _server.arg("mqttPassword").length() > 0) {
    _mqttConfig.password = _server.arg("mqttPassword");
  }
  if (_server.hasArg("clearMqttPassword")) {
    _mqttConfig.password = "";
  }
  if (_server.hasArg("mqttTopic")) {
    _mqttConfig.baseTopic = _server.arg("mqttTopic");
    _mqttConfig.baseTopic.trim();
  }
  if (_mqttConfig.baseTopic.length() == 0) {
    _mqttConfig.baseTopic = "fermentdial";
  }
  if (_server.hasArg("mqttInterval")) {
    uint32_t interval = _server.arg("mqttInterval").toInt();
    if (interval < 5) {
      interval = 5;
    }
    if (interval > 3600) {
      interval = 3600;
    }
    _mqttConfig.intervalSeconds = interval;
  }
  saveMqttConfig();
  // Force a fresh connection so the new broker/topic take effect immediately.
  _mqtt.disconnect();
  // Topic/prefix may have changed, so re-announce HA discovery configs.
  haResetAnnounced();
  _lastMqttAttemptMs = 0;
  _lastMqttPublishMs = 0;
  _lastMqttStatus = _mqttConfig.enabled ? "Saved" : "Disabled";
  _server.sendHeader("Location", "/settings", true);
  _server.send(303, "text/plain", "");
#endif
}

void NetworkManager::mqttConnect(uint32_t nowMs) {
#if FERM_ENABLE_NETWORK
  if (nowMs - _lastMqttAttemptMs < 5000UL) {
    return;
  }
  _lastMqttAttemptMs = nowMs;

  _mqtt.setServer(_mqttConfig.host.c_str(), _mqttConfig.port);

  const String base = mqttBaseTopic(_mqttConfig);
  const String availabilityTopic = base + "/availability";
  const String clientId = _hostname.length() > 0 ? _hostname : String("fermentdial");

  bool ok;
  if (_mqttConfig.username.length() > 0) {
    ok = _mqtt.connect(clientId.c_str(), _mqttConfig.username.c_str(),
                       _mqttConfig.password.c_str(), availabilityTopic.c_str(),
                       0, true, "offline");
  } else {
    ok = _mqtt.connect(clientId.c_str(), nullptr, nullptr,
                       availabilityTopic.c_str(), 0, true, "offline");
  }

  if (ok) {
    _mqtt.publish(availabilityTopic.c_str(), "online", true);
    // A fresh session (or broker restart) may have dropped retained discovery
    // configs; re-announce them on the next hydrometer publish.
    haResetAnnounced();
    _lastMqttStatus = "Connected";
  } else {
    _lastMqttStatus = "Connect failed (" + String(_mqtt.state()) + ")";
  }
#else
  (void)nowMs;
#endif
}

void NetworkManager::publishMqtt(uint32_t nowMs) {
#if FERM_ENABLE_NETWORK
  if (!_mqttConfig.enabled) {
    _lastMqttStatus = "Disabled";
    return;
  }
  if (!_snapshot.wifiConnected || _apMode) {
    _lastMqttStatus = "Waiting for Wi-Fi";
    return;
  }
  if (_mqttConfig.host.length() == 0) {
    _lastMqttStatus = "No broker";
    return;
  }
  if (!_mqtt.connected()) {
    return; // update() drives reconnect with backoff
  }
  const uint32_t intervalMs = _mqttConfig.intervalSeconds * 1000UL;
  if (nowMs - _lastMqttPublishMs < intervalMs) {
    return;
  }
  _lastMqttPublishMs = nowMs;

  if (_mqttConfig.payloadScope == ExportPayloadScope::HydrometerOnly) {
    publishMqttHydrometers(nowMs);
    return;
  }

  const String stateTopic = mqttBaseTopic(_mqttConfig) + "/state";
  const String payload = statusJson(nowMs);
  const bool ok = _mqtt.publish(stateTopic.c_str(), payload.c_str(), true);
  _lastMqttStatus = ok ? "Published" : "Publish failed (payload too large?)";
#else
  (void)nowMs;
#endif
}

void NetworkManager::loadBrewfatherConfig() {
#if FERM_ENABLE_NETWORK
  _brewfather.enabled = _prefs.getBool("bfEn", false);
  _brewfather.payloadScope = parseExportPayloadScope(
      _prefs.getString("bfScope", exportPayloadScopeValue(
                                      ExportPayloadScope::ControllerAndHydrometer)));
  _brewfather.url =
      normalizeBrewfatherUrl(_prefs.getString("bfUrl", BREWFATHER_DEFAULT_URL));
  _brewfather.loggingId =
      normalizeBrewfatherId(_prefs.getString("bfId", ""));
  _brewfather.deviceName = _prefs.getString("bfName", "");
  _brewfather.deviceName.trim();
  _brewfather.intervalSeconds = _prefs.getUInt(
      "bfEvery", BREWFATHER_MIN_INTERVAL_SECONDS);
  if (_brewfather.intervalSeconds < BREWFATHER_MIN_INTERVAL_SECONDS) {
    _brewfather.intervalSeconds = BREWFATHER_MIN_INTERVAL_SECONDS;
  }
  if (_brewfather.intervalSeconds > BREWFATHER_MAX_INTERVAL_SECONDS) {
    _brewfather.intervalSeconds = BREWFATHER_MAX_INTERVAL_SECONDS;
  }
  _lastBrewfatherStatus = _brewfather.enabled ? "Waiting" : "Disabled";
#endif
}

void NetworkManager::saveBrewfatherConfig() {
#if FERM_ENABLE_NETWORK
  _prefs.putBool("bfEn", _brewfather.enabled);
  _prefs.putString("bfScope",
                   exportPayloadScopeValue(_brewfather.payloadScope));
  _prefs.putString("bfUrl", _brewfather.url);
  _prefs.putString("bfId", _brewfather.loggingId);
  _prefs.putString("bfName", _brewfather.deviceName);
  _prefs.putUInt("bfEvery", _brewfather.intervalSeconds);
#endif
}

void NetworkManager::handleBrewfatherSettingsPost() {
#if FERM_ENABLE_NETWORK
  _brewfather.enabled = _server.hasArg("brewfatherEnabled");
  if (_server.hasArg("brewfatherPayloadScope")) {
    _brewfather.payloadScope =
        parseExportPayloadScope(_server.arg("brewfatherPayloadScope"));
  }
  if (_server.hasArg("brewfatherUrl")) {
    _brewfather.url = normalizeBrewfatherUrl(_server.arg("brewfatherUrl"));
  }
  if (_server.hasArg("brewfatherLoggingId")) {
    _brewfather.loggingId =
        normalizeBrewfatherId(_server.arg("brewfatherLoggingId"));
  }
  if (_server.hasArg("brewfatherDeviceName")) {
    _brewfather.deviceName = _server.arg("brewfatherDeviceName");
    _brewfather.deviceName.trim();
  }
  if (_server.hasArg("brewfatherInterval")) {
    // Parse signed so a negative entry clamps to the floor, not a huge uint.
    long interval = _server.arg("brewfatherInterval").toInt();
    if (interval < static_cast<long>(BREWFATHER_MIN_INTERVAL_SECONDS)) {
      interval = BREWFATHER_MIN_INTERVAL_SECONDS;
    }
    if (interval > static_cast<long>(BREWFATHER_MAX_INTERVAL_SECONDS)) {
      interval = BREWFATHER_MAX_INTERVAL_SECONDS;
    }
    _brewfather.intervalSeconds = static_cast<uint32_t>(interval);
  }
  saveBrewfatherConfig();
  _lastBrewfatherPublishMs = 0;
  _lastBrewfatherStatus = _brewfather.enabled ? "Saved" : "Disabled";
  _server.sendHeader("Location", "/settings#monitoring", true);
  _server.send(303, "text/plain", "");
#endif
}

String NetworkManager::brewfatherPayload(uint32_t nowMs,
                                         bool &hasValue) const {
  hasValue = false;
  String deviceName = _brewfather.deviceName;
  deviceName.trim();
  if (deviceName.length() == 0) {
    deviceName = _hostname.length() > 0
                     ? _hostname
                     : String("FermentDial-") + deviceSuffix(true);
  }
  const uint8_t profileIndex =
      _webStatus.activeProfile < PROFILE_COUNT ? _webStatus.activeProfile : 0;

  HydrometerReading selected;
  if (_hydrometer != nullptr && _settings != nullptr) {
    selected = _hydrometer->selectedReading(*_settings, nowMs);
  }

  String json = "{";
  appendJsonField(json, "\"name\":" + jsonString(deviceName));
  if (_brewfather.payloadScope == ExportPayloadScope::HydrometerOnly) {
    if (selected.valid && !isnan(selected.temperatureC)) {
      appendJsonNumber(json, "temp", selected.temperatureC, 2);
      appendJsonField(json, "\"temp_unit\":\"C\"");
      hasValue = true;
    }
  } else if (_webStatus.tempValid && !isnan(_webStatus.tempC)) {
    appendJsonNumber(json, "temp", _webStatus.tempC, 2);
    appendJsonField(json, "\"temp_unit\":\"C\"");
    hasValue = true;
  } else if (selected.valid && !isnan(selected.temperatureC)) {
    appendJsonNumber(json, "temp", selected.temperatureC, 2);
    appendJsonField(json, "\"temp_unit\":\"C\"");
    hasValue = true;
  }
  if (_brewfather.payloadScope != ExportPayloadScope::HydrometerOnly &&
      !isnan(_webStatus.liveTargetC)) {
    appendJsonNumber(json, "temp_target", _webStatus.liveTargetC, 2);
  }
  if (selected.valid && gravityIsValid(selected.gravity)) {
    appendJsonNumber(json, "gravity", selected.gravity, 3);
    appendJsonField(json, "\"gravity_unit\":\"G\"");
    hasValue = true;
  }
  if (selected.valid && !isnan(selected.batteryV)) {
    appendJsonNumber(json, "battery", selected.batteryV, 2);
  }
  if (selected.valid) {
    appendJsonField(json, "\"rssi\":" + String(selected.rssi));
  }
  appendJsonField(json, "\"device_source\":" + jsonString(FIRMWARE_NAME));
  appendJsonField(json, "\"report_source\":" + jsonString(_hostname));
  if (_brewfather.payloadScope == ExportPayloadScope::HydrometerOnly) {
    appendJsonField(json, "\"comment\":" + jsonString("Hydrometer export"));
  } else {
    appendJsonField(json, "\"device_state\":" +
                              jsonString(brewfatherDeviceState(
                                  _webStatus.runtimeState, _webStatus.mode,
                                  _webStatus.diacetylRestActive)));
    appendJsonField(json, "\"comment\":" +
                              jsonString(String("Profile ") +
                                         _webStatus.profiles[profileIndex].name));
  }
  json += "}";
  return json;
}

String NetworkManager::brewfatherHydrometerPayload(
    const HydrometerReading &reading, const String &deviceName,
    bool &hasValue) const {
  hasValue = false;

  String json = "{";
  appendJsonField(json, "\"name\":" + jsonString(deviceName));
  if (!isnan(reading.temperatureC)) {
    appendJsonNumber(json, "temp", reading.temperatureC, 2);
    appendJsonField(json, "\"temp_unit\":\"C\"");
    hasValue = true;
  }
  if (gravityIsValid(reading.gravity)) {
    appendJsonNumber(json, "gravity", reading.gravity, 3);
    appendJsonField(json, "\"gravity_unit\":\"G\"");
    hasValue = true;
  }
  if (!isnan(reading.batteryV)) {
    appendJsonNumber(json, "battery", reading.batteryV, 2);
  }
  appendJsonField(json, "\"rssi\":" + String(reading.rssi));
  appendJsonField(json, "\"device_source\":" + jsonString(FIRMWARE_NAME));
  appendJsonField(json, "\"report_source\":" + jsonString(_hostname));
  appendJsonField(json,
                  "\"comment\":" +
                      jsonString(hydrometerDeviceName(reading, 0)));
  json += "}";
  return json;
}

void NetworkManager::publishBrewfather(uint32_t nowMs) {
#if FERM_ENABLE_NETWORK
  if (!_brewfather.enabled) {
    _lastBrewfatherStatus = "Disabled";
    return;
  }
  if (!_snapshot.wifiConnected || _apMode) {
    _lastBrewfatherStatus = "Waiting for Wi-Fi";
    return;
  }
  if (_brewfather.loggingId.length() == 0 &&
      _brewfather.url.indexOf("id=") < 0) {
    _lastBrewfatherStatus = "No logging ID";
    return;
  }
  const uint32_t intervalMs = _brewfather.intervalSeconds * 1000UL;
  if (_lastBrewfatherPublishMs != 0 &&
      nowMs - _lastBrewfatherPublishMs < intervalMs) {
    return;
  }

  String endpoint = _brewfather.url;
  if (_brewfather.loggingId.length() > 0 && endpoint.indexOf("id=") < 0) {
    endpoint += (endpoint.indexOf('?') >= 0 ? "&" : "?");
    endpoint += "id=" + urlEncode(_brewfather.loggingId);
  }

  if (_brewfather.payloadScope == ExportPayloadScope::HydrometerOnly) {
    if (_hydrometer == nullptr) {
      _lastBrewfatherStatus = "No hydrometer";
      return;
    }

    uint8_t published = 0;
    for (uint8_t i = 0; i < _hydrometer->deviceCount(); ++i) {
      const HydrometerReading &reading = _hydrometer->device(i);
      const bool stale =
          reading.lastSeenMs == 0 ||
          (nowMs - reading.lastSeenMs) > 5UL * 60UL * 1000UL;
      if (!reading.valid || stale) {
        continue;
      }

      String deviceName = _brewfather.deviceName;
      deviceName.trim();
      const String hydrometerName = hydrometerDeviceName(reading, i);
      if (deviceName.length() == 0) {
        deviceName = hydrometerName;
      } else if (hydrometerName.length() > 0 &&
                 deviceName.indexOf(hydrometerName) < 0) {
        deviceName += " ";
        deviceName += hydrometerName;
      }

      bool hasValue = false;
      const String payload =
          brewfatherHydrometerPayload(reading, deviceName, hasValue);
      if (!hasValue) {
        continue;
      }

      HTTPClient http;
      if (!http.begin(endpoint)) {
        _lastBrewfatherStatus = "Bad URL";
        return;
      }
      http.addHeader("Content-Type", "application/json");

      _lastBrewfatherPublishMs = nowMs;
      const int code = http.POST(payload);
      http.end();
      _lastBrewfatherStatusCode = code;
      if (code >= 200 && code < 300) {
        ++published;
        continue;
      }
      _lastBrewfatherStatus =
          code > 0 ? "HTTP " + String(code) : "POST failed " + String(code);
      return;
    }

    _lastBrewfatherStatus = published > 0
                                ? "OK " + String(published) + " hydrometer(s)"
                                : "No hydrometer value";
    return;
  }

  bool hasValue = false;
  const String payload = brewfatherPayload(nowMs, hasValue);
  if (!hasValue) {
    _lastBrewfatherStatus = "No value";
    return;
  }

  HTTPClient http;
  if (!http.begin(endpoint)) {
    _lastBrewfatherStatus = "Bad URL";
    return;
  }
  http.addHeader("Content-Type", "application/json");

  // Stamp only once we actually send: a value-less or bad-URL early return
  // above must not consume the interval, but an attempted POST (success or
  // transient failure) throttles retries to the configured cadence.
  _lastBrewfatherPublishMs = nowMs;
  const int code = http.POST(payload);
  _lastBrewfatherStatusCode = code;
  if (code >= 200 && code < 300) {
    _lastBrewfatherStatus = "OK " + String(code);
  } else if (code > 0) {
    _lastBrewfatherStatus = "HTTP " + String(code);
  } else {
    _lastBrewfatherStatus = "POST failed " + String(code);
  }
  http.end();
#else
  (void)nowMs;
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

  if (_server.hasArg("fermenterName")) {
    _settings->fermenterName = _server.arg("fermenterName");
    changed = true;
  }

  if (_server.hasArg("profile")) {
    int requestedProfile = _server.arg("profile").toInt();
    if (requestedProfile < 0 || requestedProfile >= PROFILE_COUNT) {
      _server.send(400, "application/json",
                   "{\"ok\":false,\"error\":\"invalid profile\"}");
      return;
    }
    cancelDiacetylRest(*_settings);
    _settings->activeProfile = static_cast<uint8_t>(requestedProfile);
    applyActiveProfileTarget(*_settings);  // recall the profile's preset
    changed = true;
  }

  if (_server.hasArg("target")) {
    float target = _server.arg("target").toFloat();
    setCurrentTargetC(
        *_settings, fromDisplayTemp(target, _settings->unitsFahrenheit));
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

  const bool unitsF = _settings->unitsFahrenheit;

  if (_server.hasArg("coolOn")) {
    _settings->coolOnDeltaC =
        fromDisplayDelta(_server.arg("coolOn").toFloat(), unitsF);
    changed = true;
  }

  if (_server.hasArg("heatOn")) {
    _settings->heatOnDeltaC =
        fromDisplayDelta(_server.arg("heatOn").toFloat(), unitsF);
    changed = true;
  }

  if (_server.hasArg("hold")) {
    _settings->holdDeltaC =
        fromDisplayDelta(_server.arg("hold").toFloat(), unitsF);
    changed = true;
  }

  if (_server.hasArg("tempOffset")) {
    _settings->tempOffsetC =
        fromDisplayDelta(_server.arg("tempOffset").toFloat(), unitsF);
    changed = true;
  }

  if (_server.hasArg("hydrometerBleEnabled")) {
    _settings->hydrometerBleEnabled = _server.arg("hydrometerBleEnabled").toInt() != 0;
    changed = true;
  }

  if (_server.hasArg("hydrometerScanType")) {
    String scanTypeArg = _server.arg("hydrometerScanType");
    scanTypeArg.trim();
    scanTypeArg.toUpperCase();
    HydrometerScanType scanType = HydrometerScanType::Unknown;
    bool scanTypeValid = true;
    if (scanTypeArg == "OFF" || scanTypeArg == "UNKNOWN" ||
        scanTypeArg.length() == 0) {
      scanType = HydrometerScanType::Unknown;
    } else if (scanTypeArg == "TILT") {
      scanType = HydrometerScanType::Tilt;
    } else if (scanTypeArg == "RAPT") {
      scanType = HydrometerScanType::Rapt;
    } else {
      scanTypeValid = false;
    }
    if (scanTypeValid) {
      _settings->hydrometerScanType = scanType;
      clearHydrometerSelection(*_settings);
      changed = true;
    }
  }

  if (_server.hasArg("hydrometerSelectKey")) {
    String selectedKey = _server.arg("hydrometerSelectKey");
    selectedKey.trim();
    if (selectedKey != _settings->hydrometerSelectionKey) {
      _settings->hydrometerSelectionKey = selectedKey;
      const HydrometerScanType inferredScanType =
          hydrometerScanTypeFromKey(selectedKey);
      if (inferredScanType != HydrometerScanType::Unknown &&
          inferredScanType != _settings->hydrometerScanType) {
        _settings->hydrometerScanType = inferredScanType;
      }
      resetHydrometerSession(*_settings);
      changed = true;
    }
  }

  if (_server.hasArg("hydrometerClearSelection")) {
    if (_settings->hydrometerSelectionKey.length() > 0) {
      clearHydrometerSelection(*_settings);
      changed = true;
    }
  }

  if (_server.hasArg("hydrometerResetOg")) {
    resetHydrometerSession(*_settings);
    changed = true;
  }

  if (_server.hasArg("dRestTarget")) {
    _settings->diacetylRestTargetC =
        fromDisplayTemp(_server.arg("dRestTarget").toFloat(), unitsF);
    changed = true;
  }

  if (_server.hasArg("dRestHours")) {
    const float hours = _server.arg("dRestHours").toFloat();
    uint32_t seconds = static_cast<uint32_t>(lroundf(hours * 3600.0f));
    seconds = clampU32(seconds, MIN_DIACETYL_REST_DURATION_SECONDS,
                       MAX_DIACETYL_REST_DURATION_SECONDS);
    _settings->diacetylRestDurationSeconds =
        ((seconds + DIACETYL_REST_DURATION_STEP_SECONDS / 2) /
         DIACETYL_REST_DURATION_STEP_SECONDS) *
        DIACETYL_REST_DURATION_STEP_SECONDS;
    if (_settings->diacetylRestActive &&
        _settings->diacetylRestRemainingSeconds >
            _settings->diacetylRestDurationSeconds) {
      _settings->diacetylRestRemainingSeconds =
          _settings->diacetylRestDurationSeconds;
    }
    changed = true;
  }

  if (_server.hasArg("dRestReturnProfile")) {
    int requestedProfile = _server.arg("dRestReturnProfile").toInt();
    if (requestedProfile < 0 || requestedProfile >= PROFILE_COUNT) {
      _server.send(400, "application/json",
                   "{\"ok\":false,\"error\":\"invalid D-rest return profile\"}");
      return;
    }
    _settings->diacetylRestReturnProfile =
        static_cast<uint8_t>(requestedProfile);
    changed = true;
  }

  if (_server.hasArg("dRestAction")) {
    String action = _server.arg("dRestAction");
    action.trim();
    action.toLowerCase();
    if (action == "start") {
      startDiacetylRest(*_settings);
    } else if (action == "end") {
      completeDiacetylRest(*_settings);
    } else if (action == "cancel") {
      cancelDiacetylRest(*_settings);
    } else {
      _server.send(400, "application/json",
                   "{\"ok\":false,\"error\":\"invalid D-rest action\"}");
      return;
    }
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
      _settings->profiles[i].targetC =
          fromDisplayTemp(_server.arg(targetArg).toFloat(), unitsF);
      changed = true;
    }
  }

  if (_server.hasArg("gradualCrashEnabled")) {
    _settings->gradualCrashEnabled =
        _server.arg("gradualCrashEnabled").toInt() != 0;
    changed = true;
  }
  if (_server.hasArg("gradualCrashStep")) {
    _settings->gradualCrashStepC = fromDisplayDelta(
        _server.arg("gradualCrashStep").toFloat(), unitsF);
    changed = true;
  }
  if (_server.hasArg("gradualCrashStepHours")) {
    _settings->gradualCrashStepIntervalHours =
        static_cast<uint32_t>(max(0.0f, _server.arg("gradualCrashStepHours").toFloat()));
    changed = true;
  }
  if (_server.hasArg("programSaveSlot") && _server.hasArg("programSteps")) {
    long slot = _server.arg("programSaveSlot").toInt();
    if (slot >= 0 && slot < PROGRAM_SLOT_COUNT) {
      parseProgramSteps(_settings->programs[slot], _server.arg("programSteps"),
                        _settings->unitsFahrenheit);
      changed = true;
    }
  }
  if (_server.hasArg("programAction")) {
    String action = _server.arg("programAction");
    action.trim();
    action.toLowerCase();
    if (action == "start") {
      long slot = _server.hasArg("programSlot")
                      ? _server.arg("programSlot").toInt()
                      : 0;
      if (slot < 0 || slot >= PROGRAM_SLOT_COUNT) {
        slot = 0;
      }
      startProgram(*_settings, static_cast<uint8_t>(slot));
    } else if (action == "stop") {
      stopProgram(*_settings);
    } else if (action == "skip") {
      _settings->programManualAdvance = true;
    }
    changed = true;
  }

  if (changed) {
    sanitizeSettings(*_settings);
    _webStatus.fermenterName = _settings->fermenterName;
    for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
      _webStatus.profiles[i] = _settings->profiles[i];
    }
    _webStatus.activeProfile = activeProfileIndex(*_settings);
    _webStatus.liveTargetC = currentTargetC(*_settings);
    _webStatus.diacetylRestActive = _settings->diacetylRestActive;
    _webStatus.diacetylRestTargetC = _settings->diacetylRestTargetC;
    _webStatus.diacetylRestDurationSeconds =
        _settings->diacetylRestDurationSeconds;
    _webStatus.diacetylRestRemainingSeconds =
        _settings->diacetylRestRemainingSeconds;
    _webStatus.diacetylRestReturnProfile =
        diacetylRestReturnProfileIndex(*_settings);
    _webStatus.coolOnDeltaC = _settings->coolOnDeltaC;
    _webStatus.heatOnDeltaC = _settings->heatOnDeltaC;
    _webStatus.holdDeltaC = _settings->holdDeltaC;
    _webStatus.tempOffsetC = _settings->tempOffsetC;
    _webStatus.unitsFahrenheit = _settings->unitsFahrenheit;
    _webStatus.mode = _settings->mode;
    _webStatus.hydrometerBleEnabled = _settings->hydrometerBleEnabled;
    _webStatus.hydrometerScanType = _settings->hydrometerScanType;
    _webStatus.gradualCrashEnabled = _settings->gradualCrashEnabled;
    _webStatus.gradualCrashStepC = _settings->gradualCrashStepC;
    _webStatus.gradualCrashStepIntervalHours =
        _settings->gradualCrashStepIntervalHours;
    _settingsChanged = true;
  }

  _server.send(200, "application/json", statusJson(millis()));
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

namespace {

// Hydrometers go stale for export after 5 minutes without a fresh advert,
// matching the BLE scanner's own expiry.
constexpr uint32_t HYDROMETER_STALE_MS = 5UL * 60UL * 1000UL;

bool hydrometerStale(const HydrometerReading &reading, uint32_t nowMs) {
  return reading.lastSeenMs == 0 ||
         (nowMs - reading.lastSeenMs) > HYDROMETER_STALE_MS;
}

// Turn a hydrometer key ("tilt:red", "rapt:aa:bb:cc..") into an MQTT- and
// HA-safe slug ("tilt_red", "rapt_aabbcc..").
String hydrometerSlug(const HydrometerReading &reading, uint8_t index) {
  const String src =
      reading.key.length() > 0 ? reading.key : String("hydro_") + String(index);
  String slug;
  slug.reserve(src.length());
  for (size_t i = 0; i < src.length(); ++i) {
    const char c = src.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
      slug += c;
    } else if (c >= 'A' && c <= 'Z') {
      slug += static_cast<char>(c - 'A' + 'a');
    } else {
      slug += '_';
    }
  }
  return slug;
}

// One Home Assistant sensor entity per metric, keyed to a field in the
// per-device state JSON via value_template.
struct HaMetric {
  const char *object;       // discovery object id + unique_id suffix
  const char *name;         // entity name shown under the device
  const char *valueKey;     // value_json key in the state payload
  const char *deviceClass;  // HA device_class, or nullptr
  const char *unit;         // unit_of_measurement, or nullptr
  const char *stateClass;   // state_class, or nullptr
  const char *icon;         // mdi icon, or nullptr
  bool diagnostic;          // entity_category: diagnostic
};

constexpr HaMetric HA_METRICS[] = {
    {"gravity", "Gravity", "gravity", nullptr, "SG", "measurement", "mdi:water",
     false},
    {"temperature", "Temperature", "temp", "temperature", "°C",
     "measurement", nullptr, false},
    {"abv", "ABV", "abv", nullptr, "%", "measurement", "mdi:glass-mug-variant",
     false},
    {"velocity", "Gravity velocity", "velocity", nullptr, "SG/day", nullptr,
     "mdi:speedometer", false},
    {"stable", "Stable time", "stable_s", "duration", "s", nullptr, nullptr,
     false},
    {"battery", "Battery", "battery", "voltage", "V", "measurement", nullptr,
     true},
    {"rssi", "Signal", "rssi", "signal_strength", "dBm", "measurement", nullptr,
     true},
};

} // namespace

String NetworkManager::mqttHydrometerState(const HydrometerReading &reading,
                                           uint32_t nowMs) const {
  // Field names here must match the value_template keys in publishHaDiscovery.
  String json = "{";
  appendJsonField(json, "\"gravity\":" + jsonFloat(reading.gravity, 4));
  appendJsonField(json, "\"temp\":" + jsonFloat(reading.temperatureC, 2));
  appendJsonField(json, "\"rssi\":" + String(reading.rssi));
  appendJsonField(json, "\"battery\":" + jsonFloat(reading.batteryV, 2));
  appendJsonField(json, "\"abv\":" + jsonFloat(reading.abv, 1));
  appendJsonField(json, "\"velocity\":" +
                            (reading.gravityVelocityValid
                                 ? jsonFloat(reading.gravityVelocity, 4)
                                 : String("null")));
  appendJsonField(json, "\"stable_s\":" + String(reading.stableSeconds));
  appendJsonField(json,
                  "\"age_s\":" + hydrometerAgeText(nowMs, reading.lastSeenMs));
  json += "}";
  return json;
}

void NetworkManager::publishHaDiscovery(const HydrometerReading &reading,
                                        const String &slug,
                                        const String &stateTopic) {
#if FERM_ENABLE_NETWORK
  const String base = mqttBaseTopic(_mqttConfig);
  const String availabilityTopic = base + "/availability";
  const String bridgeId = String("fermentdial_") + deviceSuffix(false);
  const String deviceId = bridgeId + "_" + slug;

  // HA marks an entity unavailable if no state arrives within expire_after; we
  // stop publishing stale readings, so keep this comfortably above the publish
  // interval to avoid flapping.
  uint32_t expire = _mqttConfig.intervalSeconds * 2 + 60;
  if (expire < 600) {
    expire = 600;
  }

  // Shared device block links every metric entity under one HA device, nested
  // beneath the FermentDial bridge via via_device.
  const String device =
      "{\"ids\":[" + jsonString(deviceId) +
      "],\"name\":" + jsonString(hydrometerDeviceName(reading, 0)) +
      ",\"mf\":\"FermentDial\",\"mdl\":" +
      jsonString(hydrometerTypeText(reading.type)) +
      ",\"via_device\":" + jsonString(bridgeId) + "}";

  for (const HaMetric &m : HA_METRICS) {
    String cfg = "{";
    appendJsonField(cfg, "\"name\":" + jsonString(m.name));
    appendJsonField(cfg,
                    "\"uniq_id\":" + jsonString(deviceId + "_" + m.object));
    appendJsonField(cfg, "\"stat_t\":" + jsonString(stateTopic));
    appendJsonField(cfg, "\"val_tpl\":" +
                             jsonString(String("{{ value_json.") + m.valueKey +
                                        " }}"));
    appendJsonField(cfg, "\"avty_t\":" + jsonString(availabilityTopic));
    appendJsonField(cfg, "\"exp_aft\":" + String(expire));
    if (m.deviceClass != nullptr) {
      appendJsonField(cfg, "\"dev_cla\":" + jsonString(m.deviceClass));
    }
    if (m.unit != nullptr) {
      appendJsonField(cfg, "\"unit_of_meas\":" + jsonString(m.unit));
    }
    if (m.stateClass != nullptr) {
      appendJsonField(cfg, "\"stat_cla\":" + jsonString(m.stateClass));
    }
    if (m.icon != nullptr) {
      appendJsonField(cfg, "\"ic\":" + jsonString(m.icon));
    }
    if (m.diagnostic) {
      appendJsonField(cfg, "\"ent_cat\":\"diagnostic\"");
    }
    appendJsonField(cfg, "\"dev\":" + device);
    cfg += "}";

    const String topic = _mqttConfig.discoveryPrefix + "/sensor/" + deviceId +
                         "/" + m.object + "/config";
    _mqtt.publish(topic.c_str(), cfg.c_str(), true);
  }
#else
  (void)reading;
  (void)slug;
  (void)stateTopic;
#endif
}

void NetworkManager::publishMqttHydrometers(uint32_t nowMs) {
#if FERM_ENABLE_NETWORK
  if (_hydrometer == nullptr) {
    _lastMqttStatus = "No hydrometer";
    return;
  }
  const String base = mqttBaseTopic(_mqttConfig);
  uint8_t published = 0;
  for (uint8_t i = 0; i < _hydrometer->deviceCount(); ++i) {
    const HydrometerReading &reading = _hydrometer->device(i);
    if (!reading.valid) {
      continue;
    }
    const String slug = hydrometerSlug(reading, i);
    const String stateTopic = base + "/hydrometer/" + slug + "/state";

    // Announce each device's HA discovery configs once (re-announced on
    // reconnect or settings change), then refresh only the state topic.
    if (_mqttConfig.haDiscovery && !haAlreadyAnnounced(slug)) {
      publishHaDiscovery(reading, slug, stateTopic);
      haMarkAnnounced(slug);
    }

    // Skip stale readings so HA expires the entity instead of us refreshing it
    // with old values.
    if (hydrometerStale(reading, nowMs)) {
      continue;
    }
    const String payload = mqttHydrometerState(reading, nowMs);
    if (_mqtt.publish(stateTopic.c_str(), payload.c_str(), true)) {
      ++published;
    }
  }
  _lastMqttStatus =
      published > 0 ? "Published " + String(published) + " hydrometer(s)"
                    : "No fresh hydrometer";
#else
  (void)nowMs;
#endif
}

bool NetworkManager::haAlreadyAnnounced(const String &slug) const {
  for (uint8_t i = 0; i < _haAnnouncedCount; ++i) {
    if (_haAnnounced[i] == slug) {
      return true;
    }
  }
  return false;
}

void NetworkManager::haMarkAnnounced(const String &slug) {
  if (haAlreadyAnnounced(slug)) {
    return;
  }
  if (_haAnnouncedCount < HA_MAX_ANNOUNCED) {
    _haAnnounced[_haAnnouncedCount++] = slug;
  }
}

void NetworkManager::haResetAnnounced() {
  for (uint8_t i = 0; i < HA_MAX_ANNOUNCED; ++i) {
    _haAnnounced[i] = "";
  }
  _haAnnouncedCount = 0;
}

void NetworkManager::recordHistory(uint32_t nowMs, bool valid, float tempC) {
  if (!valid || isnan(tempC)) {
    return;  // only store real readings; gaps just leave the trace shorter
  }
  if (_historyCount > 0 && (nowMs - _lastSampleMs) < HISTORY_INTERVAL_MS) {
    return;
  }
  _lastSampleMs = nowMs;
  _history[_historyHead] = static_cast<int16_t>(lroundf(tempC * 10.0f));
  _historyHead = (_historyHead + 1) % HISTORY_LEN;
  if (_historyCount < HISTORY_LEN) {
    _historyCount++;
  }
}

String NetworkManager::historyJson() const {
  // Oldest sample first so the client can plot left-to-right.
  const uint8_t start =
      _historyCount < HISTORY_LEN ? 0 : _historyHead;
  String out = "{\"intervalMs\":" + String(HISTORY_INTERVAL_MS) + ",\"tempsC\":[";
  for (uint8_t i = 0; i < _historyCount; ++i) {
    if (i > 0) {
      out += ",";
    }
    out += String(_history[(start + i) % HISTORY_LEN] / 10.0f, 1);
  }
  out += "]}";
  return out;
}

void NetworkManager::streamHistoryFile(const char *path) {
#if FERM_ENABLE_NETWORK
  File f = LittleFS.open(path, "r");
  if (!f) {
    return;
  }
  char buf[257];
  while (f.available()) {
    size_t n = f.readBytes(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    _server.sendContent(String(buf));
  }
  f.close();
#else
  (void)path;
#endif
}

String NetworkManager::programJson() const {
  String json = "{";
  json += "\"stepTypeNames\":[\"Hold\",\"Ramp\",\"Crash\",\"DRest\","
          "\"ManualWait\"],";
  json += "\"exitTypeNames\":[\"Time\",\"GravityBelow\",\"GravityStable\","
          "\"VelocityBelow\",\"Manual\"],";
  const bool f = (_settings != nullptr) ? _settings->unitsFahrenheit
                                        : _webStatus.unitsFahrenheit;
  json += "\"unit\":" + jsonString(String(unitLabel(f))) + ",";
  json += "\"maxSteps\":" + String(MAX_PROGRAM_STEPS) + ",";
  json += "\"programs\":[";
  for (uint8_t i = 0; i < PROGRAM_SLOT_COUNT; ++i) {
    if (i > 0) {
      json += ",";
    }
    const uint8_t slot = profileSlotForProgramIndex(i);
    const String name = (_settings != nullptr)
                            ? _settings->profiles[slot].name
                            : String(defaultProfileName(slot));
    json += "{\"index\":" + String(i) + ",\"slot\":" + String(slot) +
            ",\"name\":" + jsonString(name) + ",\"steps\":[";
    if (_settings != nullptr) {
      const ProgramSettings &program = _settings->programs[i];
      for (uint8_t s = 0; s < program.stepCount; ++s) {
        if (s > 0) {
          json += ",";
        }
        const ProfileStep &st = program.steps[s];
        json += "{\"type\":" + String(static_cast<int>(st.type)) +
                ",\"exit\":" + String(static_cast<int>(st.exit)) +
                ",\"target\":" + jsonFloat(toDisplayTemp(st.targetC, f)) +
                ",\"durationSeconds\":" + String(st.durationSeconds) +
                ",\"gravityThreshold\":" + jsonFloat(st.gravityThreshold, 3) +
                ",\"stableHours\":" + String(st.stableHours) + "}";
      }
    }
    json += "]}";
  }
  json += "]}";
  return json;
}

String NetworkManager::statusJson(uint32_t nowMs) const {
  const bool f = _webStatus.unitsFahrenheit;
  const float temperature = toDisplayTemp(_webStatus.tempC, f);
  const uint8_t profileIndex =
      _webStatus.activeProfile < PROFILE_COUNT ? _webStatus.activeProfile : 0;
  const float target = toDisplayTemp(_webStatus.liveTargetC, f);  // live setpoint
  const float coolOn = toDisplayDelta(_webStatus.coolOnDeltaC, f);
  const float heatOn = toDisplayDelta(_webStatus.heatOnDeltaC, f);
  const float hold = toDisplayDelta(_webStatus.holdDeltaC, f);
  const float tempOffset = toDisplayDelta(_webStatus.tempOffsetC, f);
  const float dRestTarget = toDisplayTemp(_webStatus.diacetylRestTargetC, f);
  const float dRestDurationHours =
      _webStatus.diacetylRestDurationSeconds / 3600.0f;
  const float dRestRemainingHours =
      _webStatus.diacetylRestRemainingSeconds / 3600.0f;
  const uint8_t dRestReturnProfile =
      _webStatus.diacetylRestReturnProfile < PROFILE_COUNT
          ? _webStatus.diacetylRestReturnProfile
          : static_cast<uint8_t>(ProfileSlot::Ale);
  const char *unit = unitLabel(f);

  HydrometerReading selected;
  if (_hydrometer != nullptr && _settings != nullptr) {
    selected = _hydrometer->selectedReading(*_settings, nowMs);
  }

  String json = "{";
  json += "\"wifiConnected\":" +
          String(_snapshot.wifiConnected ? "true" : "false") + ",";
  json += "\"wifiStatus\":" + jsonString(_snapshot.status) + ",";
  json += "\"ip\":" + jsonString(_snapshot.ipAddress) + ",";
  json += "\"hostname\":" + jsonString(_snapshot.hostname) + ",";
  const Timestamp clock = nowEpochOrUptime(nowMs);
  json += "\"clock\":{";
  json += "\"wallClock\":" + String(clock.wallClock ? "true" : "false") + ",";
  json += "\"seconds\":" + String(clock.seconds);
  json += "},";
  json += "\"otaEnabled\":" + String(FERM_ENABLE_OTA ? "true" : "false") + ",";
  json += "\"firmwareVersion\":" + jsonString(FIRMWARE_VERSION) + ",";
  json += "\"firmwareGitSha\":" + jsonString(FIRMWARE_GIT_SHA) + ",";
  json += "\"demo\":" + String(_webStatus.demoSensor ? "true" : "false") + ",";
  json +=
      "\"tempValid\":" + String(_webStatus.tempValid ? "true" : "false") + ",";
  json += "\"fermenterName\":" + jsonString(_webStatus.fermenterName) + ",";
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
    const float profileTarget = toDisplayTemp(_webStatus.profiles[i].targetC, f);
    const float profileDefault = toDisplayTemp(defaultProfileTargetC(i), f);
    json += "{\"index\":" + String(i) +
            ",\"name\":" + jsonString(_webStatus.profiles[i].name) +
            ",\"target\":" + jsonFloat(profileTarget) +
            ",\"default\":" + jsonFloat(profileDefault) + "}";
  }
  json += "],";
  json += "\"program\":{";
  if (_settings != nullptr) {
    const Settings &s = *_settings;
    const uint8_t runIdx =
        s.programRunIndex < PROGRAM_SLOT_COUNT ? s.programRunIndex : 0;
    const uint8_t stepCount = s.programs[runIdx].stepCount;
    json += "\"active\":" + String(s.programActive ? "true" : "false") + ",";
    json += "\"runIndex\":" + String(runIdx) + ",";
    json += "\"slot\":" + String(profileSlotForProgramIndex(runIdx)) + ",";
    json += "\"stepIndex\":" + String(s.programStepIndex) + ",";
    json += "\"stepCount\":" + String(stepCount) + ",";
    if (s.programActive && s.programStepIndex < stepCount) {
      const ProfileStep &st = s.programs[runIdx].steps[s.programStepIndex];
      const StepExit ex = effectiveStepExit(st);
      uint32_t remaining = 0;
      if (ex == StepExit::Time &&
          st.durationSeconds > s.programStepElapsedSeconds) {
        remaining = st.durationSeconds - s.programStepElapsedSeconds;
      }
      json += "\"stepType\":" + String(static_cast<int>(st.type)) + ",";
      json += "\"stepExit\":" + String(static_cast<int>(ex)) + ",";
      json += "\"stepTarget\":" + jsonFloat(toDisplayTemp(st.targetC, f)) + ",";
      json += "\"stepElapsedSeconds\":" + String(s.programStepElapsedSeconds) +
              ",";
      json += "\"stepDurationSeconds\":" + String(st.durationSeconds) + ",";
      json += "\"stepRemainingSeconds\":" + String(remaining);
    } else {
      json += "\"stepType\":0,\"stepExit\":0,\"stepTarget\":null,"
              "\"stepElapsedSeconds\":0,\"stepDurationSeconds\":0,"
              "\"stepRemainingSeconds\":0";
    }
  } else {
    json += "\"active\":false,\"runIndex\":0,\"slot\":5,\"stepIndex\":0,"
            "\"stepCount\":0,\"stepType\":0,\"stepExit\":0,\"stepTarget\":null,"
            "\"stepElapsedSeconds\":0,\"stepDurationSeconds\":0,"
            "\"stepRemainingSeconds\":0";
  }
  json += "},";
  json += "\"gradualCrashEnabled\":" +
          String(_webStatus.gradualCrashEnabled ? "true" : "false") + ",";
  json += "\"gradualCrashStep\":" +
          jsonFloat(toDisplayDelta(_webStatus.gradualCrashStepC, f), 1) + ",";
  json += "\"gradualCrashStepHours\":" +
          String(_webStatus.gradualCrashStepIntervalHours) + ",";
  json += "\"coolOn\":" + jsonFloat(coolOn) + ",";
  json += "\"heatOn\":" + jsonFloat(heatOn) + ",";
  json += "\"hold\":" + jsonFloat(hold) + ",";
  json += "\"tempOffset\":" + jsonFloat(tempOffset) + ",";
  json += "\"diacetylRest\":{";
  json += "\"active\":" +
          String(_webStatus.diacetylRestActive ? "true" : "false") + ",";
  json += "\"target\":" + jsonFloat(dRestTarget) + ",";
  json += "\"durationSeconds\":" +
          String(_webStatus.diacetylRestDurationSeconds) + ",";
  json += "\"durationHours\":" + jsonFloat(dRestDurationHours) + ",";
  json += "\"remainingSeconds\":" +
          String(_webStatus.diacetylRestRemainingSeconds) + ",";
  json += "\"remainingHours\":" + jsonFloat(dRestRemainingHours) + ",";
  json += "\"returnProfile\":" + String(dRestReturnProfile) + ",";
  json += "\"returnProfileName\":" +
          jsonString(_webStatus.profiles[dRestReturnProfile].name) + "},";
  json += "\"unit\":\"" + String(unit) + "\",";
  json += "\"brightness\":" + String(_webStatus.brightness) + ",";
  json += "\"mode\":\"" + String(modeTopicText(_webStatus.mode)) + "\",";
  json +=
      "\"state\":\"" +
      String(_webStatus.diacetylRestActive
                 ? "D_REST"
                 : _webStatus.runtimeState == RuntimeState::Cooling &&
                         profileIndex == static_cast<uint8_t>(ProfileSlot::Crash)
                 ? "CRASHING"
                 : stateText(_webStatus.runtimeState)) +
      "\",";
  json += "\"fault\":\"" + String(faultText(_webStatus.faultCode)) + "\",";
  json += "\"heater\":" + String(_webStatus.heaterOn ? "true" : "false") + ",";
  json += "\"pump\":" + String(_webStatus.pumpOn ? "true" : "false") + ",";
  json += "\"hydrometer\":{";
  json += "\"enabled\":" +
          String((_hydrometer != nullptr && _settings != nullptr &&
                  _hydrometer->enabled(*_settings))
                     ? "true"
                     : "false") +
          ",";
  json += "\"selected\":" +
          String(selected.selected ? "true" : "false") + ",";
  json += "\"valid\":" + String(selected.valid ? "true" : "false") + ",";
  json += "\"stale\":" + String(selected.stale ? "true" : "false") + ",";
  json += "\"type\":" + jsonString(hydrometerTypeText(selected.type)) + ",";
  json += "\"scanType\":" +
          jsonString(hydrometerScanTypeText(_settings != nullptr
                                                ? _settings->hydrometerScanType
                                                : HydrometerScanType::Unknown)) +
          ",";
  json += "\"key\":" + jsonString(selected.key) + ",";
  json += "\"label\":" + jsonString(selected.label) + ",";
  json += "\"name\":" + jsonString(selected.name) + ",";
  json += "\"address\":" + jsonString(selected.address) + ",";
  json += "\"color\":" + jsonString(selected.color) + ",";
  json += "\"gravity\":" + jsonFloat(selected.gravity, 3) + ",";
  json += "\"temperature\":" + jsonFloat(toDisplayTemp(selected.temperatureC, f)) + ",";
  json += "\"rssi\":" + String(selected.rssi) + ",";
  json += "\"batteryV\":" + jsonFloat(selected.batteryV, 2) + ",";
  json += "\"gravityVelocity\":" + jsonFloat(selected.gravityVelocity, 4) + ",";
  json += "\"gravityVelocityValid\":" +
          String(selected.gravityVelocityValid ? "true" : "false") + ",";
  json += "\"originalGravity\":" + jsonFloat(selected.originalGravity, 3) + ",";
  json += "\"abv\":" + jsonFloat(selected.abv, 1) + ",";
  json += "\"stableSeconds\":" + String(selected.stableSeconds) + ",";
  json += "\"lastSeenSeconds\":" + hydrometerAgeText(nowMs, selected.lastSeenMs);
  json += "},";
  json += "\"hydrometerDevices\":[";
  if (_hydrometer != nullptr) {
    for (uint8_t i = 0; i < _hydrometer->deviceCount(); ++i) {
      if (i > 0) {
        json += ",";
      }
      const HydrometerReading &d = _hydrometer->device(i);
      const bool selectedDevice =
          _settings != nullptr &&
          d.key.length() > 0 && d.key == _settings->hydrometerSelectionKey;
      json += "{";
      json += "\"selected\":" + String(selectedDevice ? "true" : "false") + ",";
      json += "\"valid\":" + String(d.valid ? "true" : "false") + ",";
      json += "\"type\":" + jsonString(hydrometerTypeText(d.type)) + ",";
      json += "\"key\":" + jsonString(d.key) + ",";
      json += "\"label\":" + jsonString(d.label) + ",";
      json += "\"name\":" + jsonString(d.name) + ",";
      json += "\"address\":" + jsonString(d.address) + ",";
      json += "\"color\":" + jsonString(d.color) + ",";
      json += "\"gravity\":" + jsonFloat(d.gravity, 3) + ",";
      json += "\"temperature\":" + jsonFloat(toDisplayTemp(d.temperatureC, f)) + ",";
      json += "\"rssi\":" + String(d.rssi) + ",";
      json += "\"batteryV\":" + jsonFloat(d.batteryV, 2) + ",";
      json += "\"lastSeenSeconds\":" + hydrometerAgeText(nowMs, d.lastSeenMs) + ",";
      const bool stale =
          d.lastSeenMs == 0 ||
          (nowMs - d.lastSeenMs) > 5UL * 60UL * 1000UL;
      json += "\"stale\":" + String(stale ? "true" : "false");
      json += "}";
    }
  }
  json += "]";
  json += "}";
  return json;
}

String NetworkManager::settingsConfigJson() const {
  const String apSsid = setupApSsid();
  String json = "{";
  json += "\"hostname\":" + jsonString(_hostname) + ",";
  json += "\"firmwareName\":" + jsonString(FIRMWARE_NAME) + ",";
  json += "\"firmwareVersion\":" + jsonString(FIRMWARE_VERSION) + ",";
  json += "\"passwordSet\":" +
          String(_adminPassword.length() > 0 ? "true" : "false") + ",";
  json += "\"wifiSsid\":" + jsonString(_wifiSsid) + ",";
  json += "\"wifiIp\":" + jsonString(_snapshot.ipAddress) + ",";
  json += "\"apSsid\":" + jsonString(apSsid) + ",";

  json += "\"influx\":{";
  json += "\"enabled\":" + String(_influx.enabled ? "true" : "false") + ",";
  json += "\"target\":" + jsonString(influxTargetValue(_influx.target)) + ",";
  json += "\"measurement\":" + jsonString(_influx.measurement) + ",";
  json += "\"url\":" + jsonString(_influx.url) + ",";
  json += "\"database\":" + jsonString(_influx.database) + ",";
  json += "\"retentionPolicy\":" + jsonString(_influx.retentionPolicy) + ",";
  json += "\"username\":" + jsonString(_influx.username) + ",";
  json += "\"org\":" + jsonString(_influx.org) + ",";
  json += "\"bucket\":" + jsonString(_influx.bucket) + ",";
  json += "\"intervalSeconds\":" + String(_influx.intervalSeconds) + ",";
  json += "\"lastStatus\":" + jsonString(_lastInfluxStatus);
  json += "},";

  json += "\"brewfather\":{";
  json += "\"enabled\":" + String(_brewfather.enabled ? "true" : "false") + ",";
  json += "\"payloadScope\":" +
          jsonString(exportPayloadScopeValue(_brewfather.payloadScope)) + ",";
  json += "\"loggingId\":" + jsonString(_brewfather.loggingId) + ",";
  json += "\"deviceName\":" + jsonString(_brewfather.deviceName) + ",";
  json += "\"url\":" + jsonString(_brewfather.url) + ",";
  json += "\"intervalSeconds\":" + String(_brewfather.intervalSeconds) + ",";
  json += "\"lastStatus\":" + jsonString(_lastBrewfatherStatus);
  json += "},";

  json += "\"mqtt\":{";
  json += "\"enabled\":" + String(_mqttConfig.enabled ? "true" : "false") + ",";
  json += "\"payloadScope\":" +
          jsonString(exportPayloadScopeValue(_mqttConfig.payloadScope)) + ",";
  json += "\"haDiscovery\":" +
          String(_mqttConfig.haDiscovery ? "true" : "false") + ",";
  json += "\"discoveryPrefix\":" + jsonString(_mqttConfig.discoveryPrefix) + ",";
  json += "\"host\":" + jsonString(_mqttConfig.host) + ",";
  json += "\"port\":" + String(_mqttConfig.port) + ",";
  json += "\"username\":" + jsonString(_mqttConfig.username) + ",";
  json += "\"baseTopic\":" + jsonString(_mqttConfig.baseTopic) + ",";
  json += "\"intervalSeconds\":" + String(_mqttConfig.intervalSeconds) + ",";
  json += "\"computedBaseTopic\":" + jsonString(mqttBaseTopic(_mqttConfig)) + ",";
  json += "\"lastStatus\":" + jsonString(_lastMqttStatus);
  json += "}";

  json += "}";
  return json;
}

String NetworkManager::metricsText(uint32_t nowMs) const {
  const uint8_t profileIndex =
      _webStatus.activeProfile < PROFILE_COUNT ? _webStatus.activeProfile : 0;
  const ProfileSettings &profile = _webStatus.profiles[profileIndex];
  const String labels =
      "fermenter=\"" + prometheusLabelEscape(_webStatus.fermenterName) +
      "\",profile=\"" + prometheusLabelEscape(profile.name) + "\"";
  HydrometerReading selected;
  if (_hydrometer != nullptr && _settings != nullptr) {
    selected = _hydrometer->selectedReading(*_settings, nowMs);
  }

  const String p = _influx.measurement;
  String metrics = "";
  metrics += "# HELP " + p + "_firmware_info Firmware build info.\n";
  metrics += "# TYPE " + p + "_firmware_info gauge\n";
  metrics += p + "_firmware_info{version=\"" +
             prometheusLabelEscape(FIRMWARE_VERSION) + "\"} 1\n";
  metrics += "# HELP " + p + "_sensor_valid Sensor validity, 1 when valid.\n";
  metrics += "# TYPE " + p + "_sensor_valid gauge\n";
  metrics += p + "_sensor_valid{" + labels + "} " +
             String(_webStatus.tempValid ? 1 : 0) + "\n";
  if (_webStatus.tempValid && !isnan(_webStatus.tempC)) {
    metrics += "# HELP " + p + "_temperature_celsius Current temperature.\n";
    metrics += "# TYPE " + p + "_temperature_celsius gauge\n";
    metrics += p + "_temperature_celsius{" + labels + "} " +
               String(_webStatus.tempC, 3) + "\n";
  }
  metrics += "# HELP " + p + "_target_celsius Active target temperature.\n";
  metrics += "# TYPE " + p + "_target_celsius gauge\n";
  metrics += p + "_target_celsius{" + labels + "} " +
             String(_webStatus.liveTargetC, 3) + "\n";
  metrics += "# HELP " + p +
             "_diacetyl_rest_active Diacetyl rest active, 1 when active.\n";
  metrics += "# TYPE " + p + "_diacetyl_rest_active gauge\n";
  metrics += p + "_diacetyl_rest_active{" + labels + "} " +
             String(_webStatus.diacetylRestActive ? 1 : 0) + "\n";
  metrics += "# HELP " + p +
             "_diacetyl_rest_remaining_seconds Diacetyl rest remaining time.\n";
  metrics += "# TYPE " + p + "_diacetyl_rest_remaining_seconds gauge\n";
  metrics += p + "_diacetyl_rest_remaining_seconds{" + labels + "} " +
             String(_webStatus.diacetylRestRemainingSeconds) + "\n";
  metrics += "# HELP " + p + "_heater_on Heater output state.\n";
  metrics += "# TYPE " + p + "_heater_on gauge\n";
  metrics += p + "_heater_on{" + labels + "} " +
             String(_webStatus.heaterOn ? 1 : 0) + "\n";
  metrics += "# HELP " + p + "_pump_on Pump output state.\n";
  metrics += "# TYPE " + p + "_pump_on gauge\n";
  metrics += p + "_pump_on{" + labels + "} " +
             String(_webStatus.pumpOn ? 1 : 0) + "\n";
  metrics += "# HELP " + p + "_mode Current control mode as an enum.\n";
  metrics += "# TYPE " + p + "_mode gauge\n";
  metrics += p + "_mode{" + labels + ",mode=\"" +
             prometheusLabelEscape(modeTopicText(_webStatus.mode)) + "\"} " +
             String(userModeNumber(_webStatus.mode)) + "\n";
  metrics += "# HELP " + p + "_runtime_state Current runtime state as an enum.\n";
  metrics += "# TYPE " + p + "_runtime_state gauge\n";
  metrics += p + "_runtime_state{" + labels + ",state=\"" +
             prometheusLabelEscape(stateText(_webStatus.runtimeState)) +
             "\"} " + String(runtimeStateNumber(_webStatus.runtimeState)) +
             "\n";
  metrics += "# HELP " + p + "_fault_code Current fault code as an enum.\n";
  metrics += "# TYPE " + p + "_fault_code gauge\n";
  metrics += p + "_fault_code{" + labels + ",fault=\"" +
             prometheusLabelEscape(faultText(_webStatus.faultCode)) + "\"} " +
             String(static_cast<uint8_t>(_webStatus.faultCode)) + "\n";
  metrics += "# HELP " + p + "_hydrometer_enabled Hydrometer support enabled.\n";
  metrics += "# TYPE " + p + "_hydrometer_enabled gauge\n";
  metrics += p + "_hydrometer_enabled{" + labels + "} " +
             String((_hydrometer != nullptr && _settings != nullptr &&
                     _hydrometer->enabled(*_settings))
                        ? 1
                        : 0) + "\n";
  metrics += "# HELP " + p + "_hydrometer_valid Selected hydrometer valid.\n";
  metrics += "# TYPE " + p + "_hydrometer_valid gauge\n";
  metrics += p + "_hydrometer_valid{" + labels + "} " +
             String(selected.valid ? 1 : 0) + "\n";
  if (selected.valid) {
    metrics += "# HELP " + p + "_hydrometer_gravity Specific gravity.\n";
    metrics += "# TYPE " + p + "_hydrometer_gravity gauge\n";
    metrics += p + "_hydrometer_gravity{" + labels + ",type=\"" +
               prometheusLabelEscape(hydrometerTypeText(selected.type)) +
               "\"} " + String(selected.gravity, 3) + "\n";
    metrics += "# HELP " + p + "_hydrometer_temperature_c Hydrometer temp.\n";
    metrics += "# TYPE " + p + "_hydrometer_temperature_c gauge\n";
    metrics += p + "_hydrometer_temperature_c{" + labels + "} " +
               String(selected.temperatureC, 3) + "\n";
    metrics += "# HELP " + p + "_hydrometer_rssi Bluetooth RSSI.\n";
    metrics += "# TYPE " + p + "_hydrometer_rssi gauge\n";
    metrics += p + "_hydrometer_rssi{" + labels + "} " +
               String(selected.rssi) + "\n";
    if (!isnan(selected.batteryV)) {
      metrics += "# HELP " + p + "_hydrometer_battery_v Battery voltage.\n";
      metrics += "# TYPE " + p + "_hydrometer_battery_v gauge\n";
      metrics += p + "_hydrometer_battery_v{" + labels + "} " +
                 String(selected.batteryV, 2) + "\n";
    }
    if (!isnan(selected.originalGravity)) {
      metrics += "# HELP " + p +
                 "_hydrometer_original_gravity Captured original gravity.\n";
      metrics += "# TYPE " + p + "_hydrometer_original_gravity gauge\n";
      metrics += p + "_hydrometer_original_gravity{" + labels + "} " +
                 String(selected.originalGravity, 3) + "\n";
    }
    if (!isnan(selected.abv)) {
      metrics += "# HELP " + p + "_hydrometer_abv Estimated ABV.\n";
      metrics += "# TYPE " + p + "_hydrometer_abv gauge\n";
      metrics += p + "_hydrometer_abv{" + labels + "} " +
                 String(selected.abv, 2) + "\n";
    }
    metrics += "# HELP " + p + "_hydrometer_stable_seconds Stable duration.\n";
    metrics += "# TYPE " + p + "_hydrometer_stable_seconds gauge\n";
    metrics += p + "_hydrometer_stable_seconds{" + labels + "} " +
               String(selected.stableSeconds) + "\n";
    metrics += "# HELP " + p + "_hydrometer_stale Selected hydrometer stale.\n";
    metrics += "# TYPE " + p + "_hydrometer_stale gauge\n";
    metrics += p + "_hydrometer_stale{" + labels + "} " +
               String(selected.stale ? 1 : 0) + "\n";
  }
  return metrics;
}

String NetworkManager::pageHtml() const {
  return R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="/app.css">
<title>FermentDial</title>
</head>
<body data-page="dashboard"><div id="app"></div>
<script src="/app.js"></script>
</body></html>)HTML";
}

String NetworkManager::settingsHtml() const {
  return R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="/app.css">
<title>FermentDial Settings</title>
</head>
<body data-page="settings"><div id="app"></div>
<script src="/app.js"></script>
</body></html>)HTML";
}

String NetworkManager::setupHtml() const {
  String html = R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="/favicon.svg" type="image/svg+xml">
<title>FermentDial Wi-Fi</title>
<style>
html{background:#071015}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0;min-height:100vh}
main{max-width:480px;margin:auto;padding:24px}.card{border-radius:8px;background:#132428;border:1px solid #1e3840;padding:22px}
input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0 16px;padding:14px;border-radius:8px;border:1px solid #1e3840}
input{background:#102126;color:#d0e8f0}button{background:#356f89;color:white;font-weight:900}.hint{color:#a9bac8}a{color:#79d4ff}
.scanStatus{min-height:20px;margin:0 0 8px}.networkList{display:grid;gap:6px;margin:0 0 16px}.networkList button{display:flex;justify-content:space-between;gap:10px;background:#102126;color:#f8fbff;font-weight:700;text-align:left;margin:0}.networkMeta{color:#a9bac8;font-size:12px;white-space:nowrap}
</style></head><body><main><div class="card">
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
  String html = R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="/favicon.svg" type="image/svg+xml">
<title>FermentDial Login</title>
<style>
html{background:#071015}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0;min-height:100vh}
main{max-width:420px;margin:auto;padding:24px}.card{border-radius:8px;background:#132428;border:1px solid #1e3840;padding:22px}
h1{margin-top:0;color:#b0d8f8}input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0 16px;padding:14px;border-radius:8px;border:1px solid #1e3840}
input{background:#102126;color:#d0e8f0}button{background:#356f89;color:white;font-weight:900}.hint{color:#a9bac8}.err{color:#ffb4a8;font-weight:700}a{color:#79d4ff}
</style></head><body><main><div class="card">
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
  return R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="/favicon.svg" type="image/svg+xml">
<title>FermentDial Firmware</title>
<style>
html{background:#071015}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0;min-height:100vh}
main{max-width:520px;margin:auto;padding:24px}.card{border-radius:8px;background:#132428;border:1px solid #1e3840;padding:22px}
h1{margin-top:0;color:#b0d8f8}input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0 16px;padding:14px;border-radius:8px;border:1px solid #1e3840}
input{background:#102126;color:#d0e8f0}button{background:#356f89;color:white;font-weight:900}.hint{color:#a9bac8}.warn{color:#ffd178}
a{color:#79d4ff}
</style></head><body><main><div class="card">
<h1>Firmware Update</h1>
<p class="warn">Outputs turn off before the update starts. The controller reboots after a successful upload.</p>
<form method="post" action="/firmware" enctype="multipart/form-data">
<label>Firmware .bin</label><input type="file" name="firmware" accept=".bin" required>
<button type="submit">Upload and reboot</button>
</form>
<p class="hint">Build with <code>uv run platformio run -e m5stack_dial_demo</code>, then upload <code>.pio/build/m5stack_dial_demo/firmware.bin</code>.</p>
<p><a href="/wifi">Wi-Fi settings</a> - <a href="/dashboard">Dashboard</a></p>
</div></main></body></html>)HTML";
}

} // namespace ferm
