#include "network.h"

#if FERM_ENABLE_NETWORK
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_random.h>
#endif

#if FERM_ENABLE_NETWORK && FERM_ENABLE_OTA
#include <Update.h>
#endif

namespace ferm {

namespace {
constexpr const char *SETUP_AP_SSID_PREFIX = "FermentDial-Setup";
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

String selected(InfluxExportTarget actual, InfluxExportTarget expected) {
  return actual == expected ? " selected" : "";
}

String checked(bool value) { return value ? " checked" : ""; }

void appendField(String &fields, const String &field) {
  if (fields.length() > 0) {
    fields += ",";
  }
  fields += field;
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
  _webStatus.mode = settings.mode;

#if FERM_ENABLE_NETWORK
  _prefs.begin("net", false);
  loadInfluxConfig();
  loadMqttConfig();
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
  _mqtt.setBufferSize(2048);
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
    _server.send(200, "text/html", _apMode ? setupHtml() : pageHtml());
  });
  _server.on("/dashboard", HTTP_GET,
             [this]() { _server.send(200, "text/html", pageHtml()); });
  _server.on("/favicon.svg", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "max-age=86400");
    _server.send(200, "image/svg+xml", FAVICON_SVG);
  });
  _server.on("/login", HTTP_GET, [this]() { handleLogin(); });
  _server.on("/login", HTTP_POST, [this]() { handleLogin(); });
  _server.on("/logout", HTTP_GET, [this]() { handleLogout(); });
  _server.on("/logout", HTTP_POST, [this]() { handleLogout(); });
  _server.on("/settings", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "text/html", settingsHtml());
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
  _server.on("/metrics", HTTP_GET, [this]() {
    _server.send(200, "text/plain; version=0.0.4; charset=utf-8",
                 metricsText(millis()));
  });
  _server.on("/wifi", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "text/html", setupHtml());
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
#if FERM_ENABLE_OTA
  _server.on("/firmware", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "text/html", firmwareHtml());
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
        _server.send(ok ? 200 : 500, "text/html", html);
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
  _server.on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
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
    _server.send(200, "text/html", loginHtml(false));
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
  _server.send(401, "text/html", loginHtml(true));
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
    _server.send(400, "text/html",
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

  const String stateTopic = mqttBaseTopic(_mqttConfig) + "/state";
  const String payload = statusJson(nowMs);
  const bool ok = _mqtt.publish(stateTopic.c_str(), payload.c_str(), true);
  _lastMqttStatus = ok ? "Published" : "Publish failed (payload too large?)";
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
    if (scanTypeArg == "TILT") {
      scanType = HydrometerScanType::Tilt;
    } else if (scanTypeArg == "RAPT") {
      scanType = HydrometerScanType::Rapt;
    }
    if (scanType != HydrometerScanType::Unknown) {
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
          : static_cast<uint8_t>(ProfileSlot::Ferment);
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
<title>FermentDial</title>
<style>
:root{--bg:#0d1b1e;--face:#091418;--panel:#132428;--panel2:#1b3540;--line:#1e3840;--muted:#6a9aaa;--text:#d0e8f0;--accent:#b0d8f8;--blue:#356f89;--cool:#b0d8f8;--heat:#e36018;--ok:#36c87a;--gold:#ffd178;--fault:#e44840}
html{background:var(--bg)}*{box-sizing:border-box}body{margin:0;font-family:"Trebuchet MS","Avenir Next",Verdana,sans-serif;background:var(--bg);color:var(--text);min-height:100vh}
main{max-width:1040px;margin:auto;padding:16px}.shell{padding:0}
.top{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;margin-bottom:12px;position:relative}.brand{font-weight:900;font-size:19px;color:var(--accent)}.brand span{color:var(--text)}.deviceName{color:var(--muted);font-weight:900;margin-top:2px}
.menuBtn{width:auto;margin:0;padding:8px 12px;font-size:18px;line-height:1;background:#102126;color:var(--accent);border:1px solid var(--line);border-radius:8px;cursor:pointer}.menu{display:none;position:absolute;right:0;top:calc(100% + 6px);z-index:9;min-width:170px;background:var(--panel);border:1px solid var(--line);border-radius:8px;overflow:hidden;box-shadow:0 10px 26px rgba(0,0,0,.45)}.menu.open{display:block}.menu a{display:block;padding:12px 14px;color:var(--text);text-decoration:none;border-bottom:1px solid var(--line)}.menu a:last-child{border-bottom:0}.menu a:hover{background:#102126;color:var(--accent)}.menu a.current{color:var(--accent);font-weight:900}.menu a.sub{padding-left:30px;font-size:13px;color:var(--muted)}.menu a.sub.current{color:var(--accent)}
.statusbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.pill{border:1px solid var(--line);border-radius:999px;padding:7px 10px;background:#102126;color:var(--accent);font-size:13px}.demo{border-color:#5c4118;color:var(--gold);background:#1c160b}
.hero{position:relative;overflow:hidden;border:1px solid var(--line);border-top:4px solid var(--ok);border-radius:8px;padding:18px;background:var(--face);box-shadow:inset 0 0 0 1px #071015}#spark{position:absolute;inset:0;width:100%;height:100%;z-index:0;pointer-events:none}.heroTop,.readout{position:relative;z-index:1;text-shadow:0 1px 4px rgba(4,12,16,.92),0 0 2px rgba(4,12,16,.92)}
body[data-state=heating] .hero{border-top-color:var(--heat)}body[data-state=cooling] .hero,body[data-state=crashing] .hero{border-top-color:var(--cool)}body[data-state=fault] .hero{border-top-color:var(--fault)}
.heroTop{display:flex;justify-content:space-between;gap:12px;color:var(--muted);font-size:13px;text-transform:uppercase}.readout{display:flex;align-items:flex-end;justify-content:space-between;gap:14px;margin-top:12px}
.tempBlock{min-width:0}.tempLine{display:flex;align-items:flex-start}.temp{font-size:78px;line-height:.9;font-weight:900;letter-spacing:0}.unit{font-size:28px;color:var(--accent);margin-left:7px;margin-top:8px}.state{font-size:25px;font-weight:900;margin-top:8px;color:var(--ok)}body[data-state=heating] .state{color:var(--heat)}body[data-state=cooling] .state,body[data-state=crashing] .state{color:var(--cool)}body[data-state=fault] .state{color:var(--fault)}
.sub{margin-top:6px;color:var(--muted)}.outputs{display:grid;gap:8px;min-width:116px}.out{border:1px solid var(--line);border-radius:8px;padding:10px;background:#102126;color:var(--muted);font-weight:900;text-align:center}.out.on.heat{background:#3b1807;color:#ffd8bf;border-color:var(--heat)}.out.on.cool{background:#123040;color:var(--accent);border-color:var(--cool)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-top:12px}.card,.panel{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:13px}.label{color:var(--muted);font-size:12px;text-transform:uppercase}.value{font-size:23px;font-weight:900;margin-top:4px}
.controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px;margin-top:12px}.panel h2{font-size:15px;margin:0 0 11px;color:#d9e8f4}.targetCtl{display:grid;grid-template-columns:50px 1fr 50px;gap:8px;align-items:center}.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
input,button,select{font:inherit;border:1px solid var(--line);border-radius:8px;padding:12px}input,select{width:100%;background:#102126;color:var(--text)}input{text-align:center;font-size:22px;font-weight:900}.nameInput{text-align:left;font-size:20px}.fieldLabel{display:block;color:var(--muted);font-size:13px}button{background:#234858;color:var(--text);font-weight:900;cursor:pointer}button.primary{background:var(--blue);border-color:#3f819d;color:#fff}.modes{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.active{background:var(--accent)!important;border-color:var(--accent)!important;color:#081317!important}.danger{background:#321418}.heat{background:#3b1807}.cool{background:#123040}
a{color:#79d4ff}.footer{margin-top:12px;color:#8da2b0;font-size:13px}@media(max-width:760px){main{padding:12px}.controls{grid-template-columns:1fr}.readout{display:block}.outputs{grid-template-columns:1fr 1fr;margin-top:14px}.temp{font-size:58px}.unit{font-size:22px}}
</style></head>
<body><main><div class="shell">
<div class="top"><div><div class="brand">Ferment<span>Dial</span></div><div class="deviceName" id="fermenterNameTop">Fermenter</div></div><div class="statusbar"><span class="pill" id="wifi">Wi-Fi</span><span class="pill demo" id="demo" hidden>DEMO SENSOR</span><button class="menuBtn" type="button" onclick="toggleMenu(event)" aria-label="Menu">&#9776;</button></div>
<nav class="menu" id="menu"><a href="/dashboard" class="current">Dashboard</a><a href="/settings">Settings</a><a href="/settings#profiles" class="sub">Profiles</a><a href="/settings#controller" class="sub">Controller</a><a href="/settings#system" class="sub">System</a><a href="/settings#monitoring" class="sub">Monitoring</a><a href="/metrics">Metrics</a></nav></div>
<section class="hero" id="hero">
<canvas id="spark"></canvas>
<div class="heroTop"><span id="fermenterNameHero">Fermenter</span><span id="targetHero">target --.-F</span></div>
<div class="readout"><div class="tempBlock"><div class="tempLine"><span class="temp" id="temp">--.-</span><span class="unit">F</span></div>
<div class="state" id="state">Loading</div><div class="sub" id="summary">Waiting for controller status</div></div>
<div class="outputs"><div class="out heat" id="heater">HEATER OFF</div><div class="out cool" id="pump">PUMP OFF</div></div></div>
</section>
<section class="grid">
<div class="card"><div class="label">Fermenter</div><div class="value" id="fermenterName">Fermenter</div></div>
<div class="card"><div class="label">Profile</div><div class="value" id="profileName">Ferment</div></div>
<div class="card"><div class="label">Setpoint</div><div class="value"><span id="target">--.-</span><span class="unit">F</span></div></div>
<div class="card"><div class="label">Mode</div><div class="value" id="mode">OFF</div></div>
</section>
<section class="controls">
<div class="panel"><h2>Profile</h2>
<select id="profileSelect" onchange="selectProfile()"></select>
<div class="fieldLabel" style="margin-top:12px">Setpoint</div>
<div class="targetCtl" style="margin-top:6px">
<button onclick="nudge(-0.1)">-</button><input id="targetInput" inputmode="decimal" step="0.1" onchange="applyTarget()"><button onclick="nudge(0.1)">+</button>
</div>
<p class="sub" style="font-size:12px;margin-top:10px">Pick a profile to recall its preset, or nudge the live setpoint. Edit presets in <a href="/settings#profiles">Settings</a>.</p>
</div>
<div class="panel"><h2>Mode</h2><div class="modes">
<button id="btnOFF" class="danger" onclick="setMode('OFF')">OFF</button><button id="btnAUTO" onclick="setMode('AUTO')">AUTO</button>
<button id="btnHEAT_ONLY" class="heat" onclick="setMode('HEAT_ONLY')">HEAT</button><button id="btnCOOL_ONLY" class="cool" onclick="setMode('COOL_ONLY')">COOL</button>
</div></div>
<div class="panel"><h2>Diacetyl Rest</h2>
<div class="row">
<label class="fieldLabel">Rest temp<input id="dRestTargetInput" inputmode="decimal" step="0.1"></label>
<label class="fieldLabel">Hours<input id="dRestHoursInput" inputmode="numeric" step="24" min="24" max="96"></label>
</div>
<label class="fieldLabel" style="margin-top:8px">After rest<select id="dRestReturnSelect"></select></label>
<div class="modes" style="margin-top:10px">
<button class="primary" onclick="startDrest()">START</button><button id="dRestEndBtn" onclick="endDrest()">STOP</button>
</div>
<p class="sub" id="dRestStatus" style="font-size:12px;margin-top:10px">Ready</p>
</div>
</section>
<section class="grid">
<div class="card"><div class="label">Hydrometer</div><div class="value" id="hydroTitle">No hydrometer</div></div>
<div class="card"><div class="label">Gravity</div><div class="value" id="hydroGravity">--.--</div></div>
<div class="card"><div class="label">Temp</div><div class="value" id="hydroTemp">--.-</div></div>
<div class="card"><div class="label">ABV</div><div class="value" id="hydroAbv">--.-%</div></div>
</section>
<div class="footer">Fault: <span id="fault">NONE</span></div>
</div></main>
<script>
let last=null;
let spark=[];
const deg=String.fromCharCode(176);
function qs(data){return new URLSearchParams(data).toString()}
async function post(data){
 await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:qs(data)});
 await tick();
}
function applyTarget(){const v=parseFloat(targetInput.value);if(!isNaN(v))post({target:v.toFixed(1)})}
function nudge(delta){const v=parseFloat(targetInput.value||'68')+delta;targetInput.value=(Math.round(v*10)/10).toFixed(1);applyTarget()}
function selectProfile(){post({profile:profileSelect.value})}
function setMode(mode){post({mode})}
function dRestData(action){const d={dRestAction:action,dRestTarget:dRestTargetInput.value,dRestHours:dRestHoursInput.value,dRestReturnProfile:dRestReturnSelect.value};return d}
function startDrest(){post(dRestData('start'))}
function endDrest(){post({dRestAction:'end'})}
function toggleMenu(e){e.stopPropagation();document.getElementById('menu').classList.toggle('open')}
document.addEventListener('click',function(e){const m=document.getElementById('menu');if(m&&m.classList.contains('open')&&!m.contains(e.target)&&!e.target.closest('.menuBtn'))m.classList.remove('open')});
document.querySelectorAll('#menu a').forEach(function(a){a.addEventListener('click',function(){document.getElementById('menu').classList.remove('open')})});
function attr(v){return String(v).replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;')}
function renderProfiles(s){
 profileSelect.innerHTML=s.profiles.map(p=>`<option value="${p.index}">${attr(p.name)}</option>`).join('');
 profileSelect.value=s.activeProfile;
}
function renderDrest(s){
 const r=s.diacetylRest||{};
 if(document.activeElement!==dRestTargetInput)dRestTargetInput.value=(r.target||70).toFixed(1);
 if(document.activeElement!==dRestHoursInput)dRestHoursInput.value=String(Math.round(r.durationHours||48));
 if(document.activeElement!==dRestReturnSelect){
  dRestReturnSelect.innerHTML=s.profiles.map(p=>`<option value="${p.index}">${attr(p.name)}</option>`).join('');
  dRestReturnSelect.value=r.returnProfile||0;
 }
 dRestEndBtn.disabled=!r.active;
 dRestStatus.textContent=r.active?('Active - '+remainingText(r.remainingSeconds)+' remaining, then '+(r.returnProfileName||'profile')):'Ready - holds '+Math.round(r.durationHours||48)+'h at '+(r.target||70).toFixed(1)+deg+s.unit;
}
function remainingText(sec){sec=Math.max(0,sec||0);const h=Math.floor(sec/3600),m=Math.floor((sec%3600)/60);return h>0?h+'h '+m+'m':m+'m'}
async function tick(){
 const r=await fetch('/api/status'); const s=await r.json();
 last=s;
 document.body.dataset.state=s.state.toLowerCase();
 document.title=s.fermenterName+' - FermentDial';
 document.querySelectorAll('.unit').forEach(e=>e.textContent=deg+s.unit);
 temp.textContent=s.tempValid?s.temperature.toFixed(1):'--.-';
 fermenterName.textContent=s.fermenterName; fermenterNameTop.textContent=s.fermenterName; fermenterNameHero.textContent=s.fermenterName;
 const rest=s.diacetylRest||{};
 profileName.textContent=rest.active?'D-Rest':s.profileName; target.textContent=s.target.toFixed(1); targetHero.textContent=(rest.active?'D-rest':'target')+' '+s.target.toFixed(1)+deg+s.unit;
 if(document.activeElement!==profileSelect)renderProfiles(s);
 if(document.activeElement!==targetInput)targetInput.value=s.target.toFixed(1);
 renderDrest(s);
 state.textContent=s.state; mode.textContent=s.mode; wifi.textContent=s.wifiConnected?s.ip:s.wifiStatus; demo.hidden=!s.demo;
 heater.textContent=s.heater?'HEATER ON':'HEATER OFF'; pump.textContent=s.pump?'PUMP ON':'PUMP OFF'; heater.classList.toggle('on',s.heater); pump.classList.toggle('on',s.pump); fault.textContent=s.fault;
 summary.textContent=s.tempValid?(rest.active?'D-rest '+remainingText(rest.remainingSeconds)+' remaining':s.mode+' mode'):'Sensor fault - outputs forced off';
 const h=s.hydrometer||{};
 hydroTitle.textContent=h.valid?(h.label||'Hydrometer'):(h.selected?'Waiting':'No hydrometer');
 hydroGravity.textContent=h.valid?('SG '+h.gravity.toFixed(3)):'--.--';
 hydroTemp.textContent=h.valid?(h.temperature.toFixed(1)+deg+(s.unit||'')):'--.-';
 hydroAbv.textContent=h.valid&&h.abv!=null?h.abv.toFixed(1)+'%':'--.-%';
 for(const id of ['OFF','AUTO','HEAT_ONLY','COOL_ONLY'])document.getElementById('btn'+id).classList.toggle('active',s.mode===id);
}
function drawSpark(){
 const c=document.getElementById('spark'); if(!c)return;
 const w=c.clientWidth,h=c.clientHeight; if(!w||!h)return;
 const dpr=window.devicePixelRatio||1;
 if(c.width!==Math.round(w*dpr)){c.width=Math.round(w*dpr);} if(c.height!==Math.round(h*dpr)){c.height=Math.round(h*dpr);}
 const g=c.getContext('2d'); g.setTransform(dpr,0,0,dpr,0,0); g.clearRect(0,0,w,h);
 const d=spark; if(!d||d.length<2)return;
 let lo=Math.min.apply(null,d),hi=Math.max.apply(null,d);
 if(hi-lo<0.5){const m=(hi+lo)/2; lo=m-0.5; hi=m+0.5;}
 const pad=8, top=h*0.42; // keep the trace in the lower part, behind the text
 const X=i=>i/(d.length-1)*w;
 const Y=v=>(h-pad)-(v-lo)/(hi-lo)*((h-pad)-top);
 g.beginPath(); d.forEach((v,i)=>{const px=X(i),py=Y(v); i?g.lineTo(px,py):g.moveTo(px,py);});
 const area=new Path2D(); d.forEach((v,i)=>{const px=X(i),py=Y(v); i?area.lineTo(px,py):area.moveTo(px,py);}); area.lineTo(w,h); area.lineTo(0,h); area.closePath();
 const fg=g.createLinearGradient(0,top,0,h); fg.addColorStop(0,'rgba(46,213,160,0.16)'); fg.addColorStop(1,'rgba(46,213,160,0)');
 g.fillStyle=fg; g.fill(area);
 const lg=g.createLinearGradient(0,0,w,0); lg.addColorStop(0,'rgba(46,213,160,0.28)'); lg.addColorStop(1,'rgba(127,230,200,0.28)');
 g.strokeStyle=lg; g.lineWidth=1.5; g.lineJoin='round'; g.lineCap='round'; g.stroke();
}
async function loadHistory(){try{const r=await fetch('/api/history'); const j=await r.json(); spark=j.tempsC||[]; drawSpark();}catch(e){}}
addEventListener('resize',drawSpark);
tick(); setInterval(tick,2000);
loadHistory(); setInterval(loadHistory,30000);
</script></body></html>)HTML";
}

String NetworkManager::settingsHtml() const {
  const String apSsid = setupApSsid();
  const bool f = _webStatus.unitsFahrenheit;
  const String unit = unitLabel(f);
  String html = R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="/favicon.svg" type="image/svg+xml">
<title>FermentDial Settings</title>
<style>
:root{--bg:#0d1b1e;--panel:#132428;--line:#1e3840;--muted:#6a9aaa;--text:#d0e8f0;--accent:#b0d8f8;--blue:#356f89;--gold:#ffd178}
html{background:var(--bg)}*{box-sizing:border-box}body{margin:0;font-family:"Trebuchet MS","Avenir Next",Verdana,sans-serif;background:var(--bg);color:var(--text);min-height:100vh}
main{max-width:920px;margin:auto;padding:16px}.top{display:flex;justify-content:space-between;gap:10px;align-items:center;margin-bottom:12px;flex-wrap:wrap;position:relative}
.menuBtn{width:auto;margin:0;padding:8px 12px;font-size:18px;line-height:1;background:#102126;color:var(--accent);border:1px solid var(--line);border-radius:8px;cursor:pointer}.menu{display:none;position:absolute;right:0;top:calc(100% + 6px);z-index:9;min-width:170px;background:var(--panel);border:1px solid var(--line);border-radius:8px;overflow:hidden;box-shadow:0 10px 26px rgba(0,0,0,.45)}.menu.open{display:block}.menu a{display:block;padding:12px 14px;margin:0;color:var(--text);text-decoration:none;border-bottom:1px solid var(--line)}.menu a:last-child{border-bottom:0}.menu a:hover{background:#102126;color:var(--accent)}.menu a.current{color:var(--accent);font-weight:900}.menu a.sub{padding-left:30px;font-size:13px;color:var(--muted)}.menu a.sub.current{color:var(--accent)}
h1{font-size:22px;margin:0;color:var(--accent)}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px}.panel{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:14px}
h2{font-size:16px;margin:0 0 12px;color:#d9e8f4}.hint{color:var(--muted);font-size:13px;line-height:1.35}.warn{color:var(--gold)}
label{display:block;color:var(--muted);font-size:13px;margin-top:8px}input,select,button{font:inherit;width:100%;border:1px solid var(--line);border-radius:8px;padding:12px;margin-top:5px}
input,select{background:#102126;color:var(--text)}input[type=checkbox]{width:auto;margin-right:7px}button{background:var(--blue);color:white;font-weight:900;cursor:pointer}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}.nav a{color:#79d4ff;margin-left:12px}.status{border:1px solid var(--line);border-radius:8px;padding:10px;background:#102126;color:var(--accent);font-weight:900}
.wifiTools{margin:8px 0 12px}.scanStatus{min-height:18px}.networkList{display:grid;gap:6px;margin-top:6px}.networkList button{display:flex;justify-content:space-between;gap:10px;background:#102126;color:var(--text);font-weight:700;text-align:left}.networkMeta{color:var(--muted);font-size:12px;white-space:nowrap}
.tabs{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:14px}.tab{width:auto;margin:0;background:#102126;color:var(--muted);border:1px solid var(--line);font-weight:900;padding:10px 16px}.tab.active{background:var(--blue);color:#fff;border-color:#3f819d}
.tabpanel{display:none}.tabpanel.active{display:block}.tabpanel>.panel{margin-bottom:12px}
.profileRows{display:grid;gap:8px}.profileRow{display:grid;grid-template-columns:1fr 110px 46px;gap:6px;align-items:center}.profileRow input{margin-top:0}.reset{margin-top:0;padding:12px 0;background:#1b3540;color:var(--accent);font-size:16px}
.thresholds{display:grid;grid-template-columns:1fr 1fr;gap:8px}.saveStatus{color:var(--accent);font-size:13px;margin-top:8px;min-height:16px}.url{white-space:nowrap}
@media(max-width:640px){main{padding:12px}.row,.thresholds{grid-template-columns:1fr}.nav a{margin-left:0;margin-right:10px}}
</style></head><body><main>
<div class="top"><h1>FermentDial Settings</h1><button class="menuBtn" type="button" onclick="toggleMenu(event)" aria-label="Menu">&#9776;</button>
<nav class="menu" id="menu"><a href="/dashboard">Dashboard</a><a href="/settings" class="current">Settings</a><a href="/settings#profiles" class="sub">Profiles</a><a href="/settings#controller" class="sub">Controller</a><a href="/settings#system" class="sub">System</a><a href="/settings#monitoring" class="sub">Monitoring</a><a href="/metrics">Metrics</a><a href="/logout">Log out</a></nav></div>
<div class="tabs">
<button class="tab" data-tab="profiles">Profiles</button>
<button class="tab" data-tab="controller">Controller</button>
<button class="tab" data-tab="system">System</button>
<button class="tab" data-tab="monitoring">Monitoring</button>
</div>
<div id="tab-profiles" class="tabpanel">
<section class="panel"><h2>Fermentation Profiles</h2>
<p class="hint">Presets you recall from the dashboard or the dial. Editing a preset here doesn't change the live setpoint until you select that profile.</p>
<div class="profileRows" style="margin-top:10px">
)HTML";
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    html += R"HTML(<div class="profileRow"><input id="pName)HTML";
    html += String(i);
    html += R"HTML(" maxlength="15" value=")HTML";
    html += htmlEscape(_webStatus.profiles[i].name);
    html += R"HTML("><input id="pTarget)HTML";
    html += String(i);
    html += R"HTML(" inputmode="decimal" step="0.1" value=")HTML";
    html += String(toDisplayTemp(_webStatus.profiles[i].targetC, f), 1);
    html += R"HTML("><button class="reset" title="Reset to default" data-i=")HTML";
    html += String(i);
    html += R"HTML(" data-default=")HTML";
    html += String(toDisplayTemp(defaultProfileTargetC(i), f), 1);
    html += R"HTML(" onclick="resetProfile(this)">&#8634;</button></div>
)HTML";
  }
  html += R"HTML(</div>
<button class="primary" onclick="saveProfiles()">Save profiles</button>
<div class="saveStatus" id="profilesStatus"></div>
</section>
</div>
<div id="tab-controller" class="tabpanel">
<section class="panel"><h2>Regulation</h2>
<p class="hint">How tightly the controller holds the setpoint. Values are in )HTML";
  html += unit;
  html += R"HTML(.</p>
<div class="thresholds" style="margin-top:10px">
<label>Cool on<input id="coolOnInput" inputmode="decimal" step="0.1" value=")HTML";
  html += String(toDisplayDelta(_webStatus.coolOnDeltaC, f), 1);
  html += R"HTML("></label>
<label>Heat on<input id="heatOnInput" inputmode="decimal" step="0.1" value=")HTML";
  html += String(toDisplayDelta(_webStatus.heatOnDeltaC, f), 1);
  html += R"HTML("></label>
<label>Hold band<input id="holdInput" inputmode="decimal" step="0.1" value=")HTML";
  html += String(toDisplayDelta(_webStatus.holdDeltaC, f), 1);
  html += R"HTML("></label>
<label>Sensor offset<input id="offsetInput" inputmode="decimal" step="0.1" value=")HTML";
  html += String(toDisplayDelta(_webStatus.tempOffsetC, f), 1);
  html += R"HTML("></label>
</div>
<button class="primary" onclick="saveControl()">Save controller settings</button>
<div class="saveStatus" id="controllerStatus"></div>
</section>
<section class="panel"><h2>Diacetyl Rest</h2>
<p class="hint">Defaults used by the dashboard and dial. Typical rests are 24-48 hours at 70-72 )HTML";
  html += unit;
  html += R"HTML(.</p>
<div class="thresholds" style="margin-top:10px">
<label>Rest temp<input id="dRestTargetSettings" inputmode="decimal" step="0.1" value=")HTML";
  html += String(toDisplayTemp(_webStatus.diacetylRestTargetC, f), 1);
  html += R"HTML("></label>
<label>Duration hours<input id="dRestHoursSettings" inputmode="numeric" step="24" min="24" max="96" value=")HTML";
  html += String(_webStatus.diacetylRestDurationSeconds / 3600UL);
  html += R"HTML("></label>
</div>
<label>After rest<select id="dRestReturnSettings">
)HTML";
  const uint8_t dRestReturnProfile =
      _webStatus.diacetylRestReturnProfile < PROFILE_COUNT
          ? _webStatus.diacetylRestReturnProfile
          : static_cast<uint8_t>(ProfileSlot::Ferment);
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    html += R"HTML(<option value=")HTML";
    html += String(i);
    html += "\"";
    html += i == dRestReturnProfile ? " selected" : "";
    html += ">";
    html += htmlEscape(_webStatus.profiles[i].name);
    html += R"HTML(</option>
)HTML";
  }
  html += R"HTML(</select></label>
<button class="primary" onclick="saveDrestDefaults()">Save D-rest defaults</button>
<div class="saveStatus" id="dRestStatusSettings"></div>
</section>
</div>
<div id="tab-system" class="tabpanel">
<div class="grid">
<section class="panel"><h2>Device</h2>
<form method="post" action="/settings/device">
<label>Fermenter name<input name="fermenterName" maxlength="24" value=")HTML";
  html += htmlEscape(_webStatus.fermenterName);
  html += R"HTML("></label>
<label>Screen brightness<input name="brightness" type="range" min="30" max="255" step="5" value=")HTML";
  html += String(_webStatus.brightness);
  html += R"HTML("></label>
<button type="submit">Save device settings</button>
</form>
<p class="hint">Firmware: )HTML";
  html += htmlEscape(FIRMWARE_NAME);
  html += " v";
  html += htmlEscape(FIRMWARE_VERSION);
  html += R"HTML(</p>
<p class="hint">Hostname: <code>)HTML";
  html += htmlEscape(_snapshot.hostname);
  html += R"HTML(</code></p>
</section>
<section class="panel"><h2>Security</h2>
<form method="post" action="/settings/security">
<label>New admin password<input name="newPassword" type="password" autocomplete="new-password" placeholder="leave blank to disable the lock"></label>
<label>Confirm password<input name="confirmPassword" type="password" autocomplete="new-password"></label>
<button type="submit">Update password</button>
</form>
<p class="hint">)HTML";
  html += _adminPassword.length() > 0
              ? R"HTML(Config pages are <b>locked</b>. The live dashboard and metrics stay public.)HTML"
              : R"HTML(Config pages are <span class="warn">unlocked</span> &mdash; anyone on the network can change settings.)HTML";
  html += R"HTML(</p>
</section>
<section class="panel"><h2>Wi-Fi</h2>
<form method="post" action="/wifi">
<label>Wi-Fi name<input name="ssid" autocomplete="off" required value=")HTML";
  html += htmlEscape(_wifiSsid);
  html += R"HTML("></label>
<div class="wifiTools">
<button type="button" onclick="scanWifi()">Scan for networks</button>
<div class="hint scanStatus" id="wifiScanStatus">Select a scanned network or enter the name manually.</div>
<div class="networkList" id="wifiNetworks"></div>
</div>
<label>Password<input name="pass" type="password" placeholder="leave blank to keep saved"></label>
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
<p class="hint">Connected IP: )HTML";
  html += htmlEscape(_snapshot.ipAddress.length() > 0 ? _snapshot.ipAddress
                                                       : "not connected");
  html += R"HTML(</p>
<p class="hint">Setup AP: )HTML";
  html += htmlEscape(apSsid);
  html += R"HTML( (open network)</p></section>
<section class="panel"><h2>Firmware Update</h2>
<p class="warn">Outputs turn off before upload starts. The controller reboots after a successful update.</p>
)HTML";
#if FERM_ENABLE_OTA
  html += R"HTML(<form method="post" action="/firmware" enctype="multipart/form-data">
<label>Firmware .bin<input type="file" name="firmware" accept=".bin" required></label>
<button type="submit">Upload and reboot</button>
</form>)HTML";
#else
  html += R"HTML(<p class="hint">Firmware upload is disabled in this build.</p>)HTML";
#endif
  html += R"HTML(<p class="hint">Build with <code>uv run platformio run -e m5stack_dial_demo</code>, then upload <code>.pio/build/m5stack_dial_demo/firmware.bin</code>.</p>
</section>
</div></div>
<div id="tab-monitoring" class="tabpanel"><div class="grid">
<section class="panel"><h2>Prometheus</h2>
<div class="status">/metrics</div>
<p class="hint">Scrape <code class="url">http://)HTML";
  html += htmlEscape(_snapshot.ipAddress.length() > 0 ? _snapshot.ipAddress
                                                       : apSsid);
  html += R"HTML(/metrics</code>. Values are emitted in Celsius and output states are 0/1 gauges. The measurement / metric name set under Influx Export is also used as the Prometheus metric prefix.</p>
</section>
<section class="panel"><h2>Influx Export</h2>
<form method="post" action="/settings/influx">
<label><input type="checkbox" name="influxEnabled")HTML";
  html += checked(_influx.enabled);
  html += R"HTML(>Enable push export</label>
<label>Target<select name="influxTarget">
<option value="1")HTML";
  html += selected(_influx.target, InfluxExportTarget::V1);
  html += R"HTML(>InfluxDB 1.x</option>
<option value="2")HTML";
  html += selected(_influx.target, InfluxExportTarget::V2);
  html += R"HTML(>InfluxDB 2.x</option>
<option value="3")HTML";
  html += selected(_influx.target, InfluxExportTarget::V3);
  html += R"HTML(>InfluxDB 3.x</option>
<option value="vm")HTML";
  html += selected(_influx.target, InfluxExportTarget::VictoriaMetrics);
  html += R"HTML(>VictoriaMetrics</option>
</select></label>
<label>Measurement / metric name<input name="influxMeasurement" value=")HTML";
  html += htmlEscape(_influx.measurement);
  html += R"HTML("></label>
<label>Base URL<input name="influxUrl" placeholder="http://host:8086" value=")HTML";
  html += htmlEscape(_influx.url);
  html += R"HTML("></label>
<div id="influxV1">
<div class="row">
<label>Database / DB<input name="influxDatabase" value=")HTML";
  html += htmlEscape(_influx.database);
  html += R"HTML("></label>
<label>Retention policy<input name="influxRetentionPolicy" value=")HTML";
  html += htmlEscape(_influx.retentionPolicy);
  html += R"HTML("></label>
</div><div class="row">
<label>Username<input name="influxUsername" value=")HTML";
  html += htmlEscape(_influx.username);
  html += R"HTML("></label>
<label>Password / v1 token<input name="influxPassword" type="password" placeholder="leave blank to keep saved"></label>
</div>
<label><input type="checkbox" name="clearInfluxPassword">Clear saved password</label>
</div>
<div id="influxV2">
<div class="row">
<label>Org<input name="influxOrg" value=")HTML";
  html += htmlEscape(_influx.org);
  html += R"HTML("></label>
<label>Bucket<input name="influxBucket" value=")HTML";
  html += htmlEscape(_influx.bucket);
  html += R"HTML("></label>
</div>
<label>V2/V3 token<input name="influxToken" type="password" placeholder="leave blank to keep saved"></label>
<label><input type="checkbox" name="clearInfluxToken">Clear saved token</label>
</div>
<label>Interval seconds<input name="influxInterval" inputmode="numeric" value=")HTML";
  html += String(_influx.intervalSeconds);
  html += R"HTML("></label>
<button type="submit">Save export settings</button>
</form>
<script>(function(){var s=document.querySelector('select[name=influxTarget]'),a=document.getElementById('influxV1'),b=document.getElementById('influxV2');function u(){var v2=(s.value==='2'||s.value==='3');b.style.display=v2?'':'none';a.style.display=v2?'none':'';}s.addEventListener('change',u);u();})();</script>
<p class="hint">Last export: )HTML";
  html += htmlEscape(_lastInfluxStatus);
  html += R"HTML(. VictoriaMetrics uses Influx line protocol at <code>/write</code> unless the URL already includes an Influx write path.</p>
</section>
<section class="panel"><h2>MQTT / Home Assistant</h2>
<form method="post" action="/settings/mqtt">
<label><input type="checkbox" name="mqttEnabled")HTML";
  html += checked(_mqttConfig.enabled);
  html += R"HTML(>Enable MQTT publishing</label>
<div class="row">
<label>Broker host<input name="mqttHost" placeholder="192.168.1.10" value=")HTML";
  html += htmlEscape(_mqttConfig.host);
  html += R"HTML("></label>
<label>Port<input name="mqttPort" inputmode="numeric" value=")HTML";
  html += String(_mqttConfig.port);
  html += R"HTML("></label>
</div>
<div class="row">
<label>Username<input name="mqttUsername" value=")HTML";
  html += htmlEscape(_mqttConfig.username);
  html += R"HTML("></label>
<label>Password<input name="mqttPassword" type="password" placeholder="leave blank to keep saved"></label>
</div>
<label><input type="checkbox" name="clearMqttPassword">Clear saved password</label>
<label>Base topic<input name="mqttTopic" value=")HTML";
  html += htmlEscape(_mqttConfig.baseTopic);
  html += R"HTML("></label>
<label>Interval seconds<input name="mqttInterval" inputmode="numeric" value=")HTML";
  html += String(_mqttConfig.intervalSeconds);
  html += R"HTML("></label>
<button type="submit">Save MQTT settings</button>
</form>
<p class="hint">Publishes the status JSON to <code>)HTML";
  html += htmlEscape(mqttBaseTopic(_mqttConfig));
  html += R"HTML(/state</code> (retained), with availability on <code>)HTML";
  html += htmlEscape(mqttBaseTopic(_mqttConfig));
  html += R"HTML(/availability</code>. Status: )HTML";
  html += htmlEscape(_lastMqttStatus);
  html += R"HTML(</p>
</section>
<section class="panel"><h2>Hydrometer</h2>
<form method="post" action="/api/settings" id="hydroForm" onsubmit="saveHydroSettings();return false;">
<label><input type="checkbox" name="hydrometerBleEnabled" value="1" )HTML";
  html += checked(_webStatus.hydrometerBleEnabled);
  html += R"HTML(>Enable BLE scanning</label>
<div class="row" style="margin-top:10px">
<button type="button" id="hydroTiltBtn" onclick="setHydrometerScanType('TILT')">Add Tilt</button>
<button type="button" id="hydroRaptBtn" onclick="setHydrometerScanType('RAPT')">Add RAPT</button>
</div>
<div class="hint" id="hydroScanHint">Choose Tilt or RAPT to start scanning. Only that type will be listed here.</div>
<div class="hint">The selected hydrometer is used for display and fermentation metrics only.</div>
<button type="submit">Save hydrometer settings</button>
<div class="row" style="margin-top:10px">
<button type="button" onclick="resetHydrometerOg()">Reset OG</button>
<button type="button" onclick="clearHydrometer()">Clear selection</button>
</div>
</form>
<div class="saveStatus" id="hydroStatusSettings"></div>
<div class="networkList" id="hydroList"></div>
</section>
</div></div>
<script>
function toggleMenu(e){e.stopPropagation();document.getElementById('menu').classList.toggle('open')}
document.addEventListener('click',function(e){const m=document.getElementById('menu');if(m&&m.classList.contains('open')&&!m.contains(e.target)&&!e.target.closest('.menuBtn'))m.classList.remove('open')});
document.querySelectorAll('#menu a').forEach(function(a){a.addEventListener('click',function(){document.getElementById('menu').classList.remove('open')})});
function showTab(id){document.querySelectorAll('.tab').forEach(function(b){b.classList.toggle('active',b.dataset.tab===id)});document.querySelectorAll('.tabpanel').forEach(function(p){p.classList.toggle('active',p.id==='tab-'+id)});document.querySelectorAll('.menu a.sub').forEach(function(a){a.classList.toggle('current',a.getAttribute('href')==='/settings#'+id)});if(location.hash!=='#'+id)history.replaceState(null,'','#'+id);}
addEventListener('hashchange',function(){showTab((location.hash||'#profiles').slice(1))});
document.querySelectorAll('.tab').forEach(function(b){b.addEventListener('click',function(){showTab(b.dataset.tab)})});
showTab((location.hash||'#profiles').slice(1));
async function postSettings(d){await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(d).toString()});}
function hydroAge(sec){sec=Math.max(0,sec||0);if(sec<60)return sec+'s';const m=Math.floor(sec/60),h=Math.floor(m/60);return h>0?h+'h '+(m%60)+'m':m+'m'}
function renderHydro(s){
 const h=s.hydrometer||{},list=document.getElementById('hydroList'),st=document.getElementById('hydroStatusSettings');
 const devices=s.hydrometerDevices||[];
 const scanType=(h.scanType||'UNKNOWN').toUpperCase();
 const tiltBtn=document.getElementById('hydroTiltBtn'),raptBtn=document.getElementById('hydroRaptBtn'),scanHint=document.getElementById('hydroScanHint');
 if(tiltBtn)tiltBtn.classList.toggle('active',scanType==='TILT');
 if(raptBtn)raptBtn.classList.toggle('active',scanType==='RAPT');
 if(scanHint){
  if(scanType==='TILT'){scanHint.textContent='Scanning for Tilt devices only.';}
  else if(scanType==='RAPT'){scanHint.textContent='Scanning for RAPT devices only.';}
  else{scanHint.textContent='Choose Tilt or RAPT to start scanning. Only that type will be listed here.';}
 }
 if(st)st.textContent=h.selected?(h.valid?(h.stale?'Selected device is stale':'Selected device is active'):'Selected device waiting for a reading'):'No hydrometer selected';
 if(!list)return;
 list.innerHTML='';
 if(!devices.length){const d=document.createElement('div');d.className='hint';d.textContent=scanType==='UNKNOWN'?'Choose a device type above to begin scanning.':('No '+scanType+' devices have been decoded yet.');list.appendChild(d);return;}
 devices.forEach(function(dev){
  const b=document.createElement('button');b.type='button';
  const left=document.createElement('span');
  left.textContent=(dev.label||dev.key||'Device')+(dev.selected?' (Selected)':'');
  const right=document.createElement('span');right.className='networkMeta';
  const parts=[];
  if(dev.gravity!=null)parts.push('SG '+Number(dev.gravity).toFixed(3));
  if(dev.temperature!=null)parts.push(Number(dev.temperature).toFixed(1)+deg+(s.unit||''));
  parts.push((dev.rssi||0)+' dBm');
  parts.push(dev.lastSeenSeconds==null?'unknown':hydroAge(dev.lastSeenSeconds));
  right.textContent=parts.join(' · ');
  b.appendChild(left);b.appendChild(right);
  b.onclick=function(){selectHydrometer(dev.key);};
  list.appendChild(b);
 });
}
async function setHydrometerScanType(type){await postSettings({hydrometerScanType:type});await refresh();}
async function saveHydroSettings(){const f=document.getElementById('hydroForm');if(!f)return;await postSettings({hydrometerBleEnabled:f.querySelector('input[name=hydrometerBleEnabled]').checked?1:0});await refresh();}
async function selectHydrometer(key){await postSettings({hydrometerSelectKey:key});await refresh();}
async function clearHydrometer(){await postSettings({hydrometerClearSelection:1});await refresh();}
async function resetHydrometerOg(){await postSettings({hydrometerResetOg:1});await refresh();}
async function refresh(){try{const s=await(await fetch('/api/status')).json();(s.profiles||[]).forEach(function(p){const n=document.getElementById('pName'+p.index),t=document.getElementById('pTarget'+p.index),rb=document.querySelector('.reset[data-i="'+p.index+'"]');if(n&&document.activeElement!==n)n.value=p.name;if(t&&document.activeElement!==t)t.value=p.target.toFixed(1);if(rb)rb.dataset.default=p.default.toFixed(1);});const m={coolOnInput:'coolOn',heatOnInput:'heatOn',holdInput:'hold',offsetInput:'tempOffset'};for(const id in m){const el=document.getElementById(id);if(el&&document.activeElement!==el)el.value=s[m[id]].toFixed(1);}const r=s.diacetylRest||{};if(dRestTargetSettings&&document.activeElement!==dRestTargetSettings)dRestTargetSettings.value=(r.target||70).toFixed(1);if(dRestHoursSettings&&document.activeElement!==dRestHoursSettings)dRestHoursSettings.value=String(Math.round(r.durationHours||48));if(dRestReturnSettings&&document.activeElement!==dRestReturnSettings)dRestReturnSettings.value=r.returnProfile||0;const hf=document.querySelector('#hydroForm input[name=hydrometerBleEnabled]');if(hf&&document.activeElement!==hf)hf.checked=!!(s.hydrometer&&s.hydrometer.enabled);renderHydro(s);}catch(e){}}
function resetProfile(btn){const t=document.getElementById('pTarget'+btn.dataset.i);if(t)t.value=btn.dataset.default;}
async function saveProfiles(){const d={};for(let i=0;i<)HTML";
  html += String(PROFILE_COUNT);
  html += R"HTML(;i++){const n=document.getElementById('pName'+i),t=document.getElementById('pTarget'+i);if(!n||!t)continue;d['profile'+i+'Name']=n.value;d['profile'+i+'Target']=t.value;}const st=document.getElementById('profilesStatus');st.textContent='Saving...';await postSettings(d);await refresh();st.textContent='Saved.';}
async function saveControl(){const d={coolOn:coolOnInput.value,heatOn:heatOnInput.value,hold:holdInput.value,tempOffset:offsetInput.value};const st=document.getElementById('controllerStatus');st.textContent='Saving...';await postSettings(d);await refresh();st.textContent='Saved.';}
async function saveDrestDefaults(){const d={dRestTarget:dRestTargetSettings.value,dRestHours:dRestHoursSettings.value,dRestReturnProfile:dRestReturnSettings.value};const st=document.getElementById('dRestStatusSettings');st.textContent='Saving...';await postSettings(d);await refresh();st.textContent='Saved.';}
refresh();
</script>
</main></body></html>)HTML";
  return html;
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
