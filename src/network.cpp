#include "network.h"

#if FERM_ENABLE_NETWORK
#include <HTTPClient.h>
#include <WiFi.h>
#endif

#if FERM_ENABLE_NETWORK && FERM_ENABLE_OTA
#include <Update.h>
#endif

namespace ferm {

namespace {
constexpr const char *SETUP_AP_SSID_PREFIX = "FermentDial-Setup";
constexpr const char *SETUP_AP_PASSWORD = "fermentdial";
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

} // namespace

void NetworkManager::begin(const Settings &settings) {
  _snapshot = NetworkSnapshot{};
  _snapshot.wifiEnabled = FERM_ENABLE_NETWORK;
  _snapshot.otaEnabled = FERM_ENABLE_OTA;
  _hostname = networkHostname(settings.fermenterName);
  _snapshot.hostname = _hostname;
  _webStatus.fermenterName = settings.fermenterName;
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _webStatus.profiles[i] = settings.profiles[i];
  }
  _webStatus.activeProfile = activeProfileIndex(settings);
  _webStatus.coolOnDeltaC = settings.coolOnDeltaC;
  _webStatus.heatOnDeltaC = settings.heatOnDeltaC;
  _webStatus.holdDeltaC = settings.holdDeltaC;
  _webStatus.tempOffsetC = settings.tempOffsetC;
  _webStatus.unitsFahrenheit = settings.unitsFahrenheit;
  _webStatus.brightness = settings.brightness;
  _webStatus.mode = settings.mode;

#if FERM_ENABLE_NETWORK
  _prefs.begin("net", false);
  loadInfluxConfig();
  loadMqttConfig();
  _wifiSsid = _prefs.getString("ssid", FERM_WIFI_SSID);
  _wifiPassword = _prefs.getString("pass", FERM_WIFI_PASSWORD);
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
                                  const FermentationController &controller) {
  _webStatus.tempValid = sensor.isValid();
  _webStatus.tempC = sensor.temperatureC();
  _webStatus.fermenterName = settings.fermenterName;
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _webStatus.profiles[i] = settings.profiles[i];
  }
  _webStatus.activeProfile = activeProfileIndex(settings);
  _webStatus.coolOnDeltaC = settings.coolOnDeltaC;
  _webStatus.heatOnDeltaC = settings.heatOnDeltaC;
  _webStatus.holdDeltaC = settings.holdDeltaC;
  _webStatus.tempOffsetC = settings.tempOffsetC;
  _webStatus.unitsFahrenheit = settings.unitsFahrenheit;
  _webStatus.brightness = settings.brightness;
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
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(SETUP_IP, SETUP_GATEWAY, SETUP_MASK);
  WiFi.softAP(ssid.c_str(), SETUP_AP_PASSWORD);
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
  _server.on("/settings", HTTP_GET,
             [this]() { _server.send(200, "text/html", settingsHtml()); });
  _server.on("/settings/device", HTTP_POST,
             [this]() { handleDeviceSettingsPost(); });
  _server.on("/settings/influx", HTTP_POST,
             [this]() { handleInfluxSettingsPost(); });
  _server.on("/settings/mqtt", HTTP_POST,
             [this]() { handleMqttSettingsPost(); });
  _server.on("/metrics", HTTP_GET, [this]() {
    _server.send(200, "text/plain; version=0.0.4; charset=utf-8",
                 metricsText());
  });
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
  _server.on("/firmware", HTTP_GET,
             [this]() { _server.send(200, "text/html", firmwareHtml()); });
  _server.on(
      "/firmware", HTTP_POST,
      [this]() {
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

void NetworkManager::handleFirmwareUpload() {
#if FERM_ENABLE_NETWORK && FERM_ENABLE_OTA
  HTTPUpload &upload = _server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    _firmwareUpdateInProgress = true;
    _firmwareUpdateHadError = false;
    _firmwareUpdateOk = false;
    _firmwareUpdateError = "";

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

String NetworkManager::influxLineProtocol() const {
  const uint8_t profileIndex =
      _webStatus.activeProfile < PROFILE_COUNT ? _webStatus.activeProfile : 0;
  const ProfileSettings &profile = _webStatus.profiles[profileIndex];

  String fields = "";
  if (_webStatus.tempValid && !isnan(_webStatus.tempC)) {
    appendField(fields, "temp_c=" + String(_webStatus.tempC, 3));
  }
  appendField(fields, "target_c=" + String(profile.targetC, 3));
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

  const int code = http.POST(influxLineProtocol());
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
  const String payload = statusJson();
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
    _webStatus.fermenterName = _settings->fermenterName;
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
  json += "\"hostname\":" + jsonString(_snapshot.hostname) + ",";
  json += "\"otaEnabled\":" + String(FERM_ENABLE_OTA ? "true" : "false") + ",";
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
  json += "\"brightness\":" + String(_webStatus.brightness) + ",";
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

String NetworkManager::metricsText() const {
  const uint8_t profileIndex =
      _webStatus.activeProfile < PROFILE_COUNT ? _webStatus.activeProfile : 0;
  const ProfileSettings &profile = _webStatus.profiles[profileIndex];
  const String labels =
      "fermenter=\"" + prometheusLabelEscape(_webStatus.fermenterName) +
      "\",profile=\"" + prometheusLabelEscape(profile.name) + "\"";

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
             String(profile.targetC, 3) + "\n";
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
  return metrics;
}

String NetworkManager::pageHtml() const {
  return R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FermentDial</title>
<style>
:root{--bg:#0d1b1e;--face:#091418;--panel:#132428;--panel2:#1b3540;--line:#1e3840;--muted:#6a9aaa;--text:#d0e8f0;--accent:#b0d8f8;--blue:#356f89;--cool:#b0d8f8;--heat:#e36018;--ok:#36c87a;--gold:#ffd178;--fault:#e44840}
*{box-sizing:border-box}body{margin:0;font-family:"Trebuchet MS","Avenir Next",Verdana,sans-serif;background:var(--bg);color:var(--text)}
main{max-width:1040px;margin:auto;padding:16px}.shell{padding:0}
.top{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;margin-bottom:12px}.brand{font-weight:900;font-size:19px;color:var(--accent)}.brand span{color:var(--text)}.deviceName{color:var(--muted);font-weight:900;margin-top:2px}
.statusbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.pill{border:1px solid var(--line);border-radius:999px;padding:7px 10px;background:#102126;color:var(--accent);font-size:13px}.demo{border-color:#5c4118;color:var(--gold);background:#1c160b}
.hero{border:1px solid var(--line);border-top:4px solid var(--ok);border-radius:8px;padding:18px;background:var(--face);box-shadow:inset 0 0 0 1px #071015}
body[data-state=heating] .hero{border-top-color:var(--heat)}body[data-state=cooling] .hero,body[data-state=crashing] .hero{border-top-color:var(--cool)}body[data-state=fault] .hero{border-top-color:var(--fault)}
.heroTop{display:flex;justify-content:space-between;gap:12px;color:var(--muted);font-size:13px;text-transform:uppercase}.readout{display:flex;align-items:flex-end;justify-content:space-between;gap:14px;margin-top:12px}
.tempBlock{min-width:0}.tempLine{display:flex;align-items:flex-start}.temp{font-size:78px;line-height:.9;font-weight:900;letter-spacing:0}.unit{font-size:28px;color:var(--accent);margin-left:7px;margin-top:8px}.state{font-size:25px;font-weight:900;margin-top:8px;color:var(--ok)}body[data-state=heating] .state{color:var(--heat)}body[data-state=cooling] .state,body[data-state=crashing] .state{color:var(--cool)}body[data-state=fault] .state{color:var(--fault)}
.sub{margin-top:6px;color:var(--muted)}.outputs{display:grid;gap:8px;min-width:116px}.out{border:1px solid var(--line);border-radius:8px;padding:10px;background:#102126;color:var(--muted);font-weight:900;text-align:center}.out.on.heat{background:#3b1807;color:#ffd8bf;border-color:var(--heat)}.out.on.cool{background:#123040;color:var(--accent);border-color:var(--cool)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-top:12px}.card,.panel{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:13px}.label{color:var(--muted);font-size:12px;text-transform:uppercase}.value{font-size:23px;font-weight:900;margin-top:4px}
.controls{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px}.panel h2{font-size:15px;margin:0 0 11px;color:#d9e8f4}.targetCtl{display:grid;grid-template-columns:50px 1fr 50px;gap:8px;align-items:center}
input,button,select{font:inherit;border:1px solid var(--line);border-radius:8px;padding:12px}input,select{width:100%;background:#102126;color:var(--text)}input{text-align:center;font-size:22px;font-weight:900}.nameInput{text-align:left;font-size:20px}.fieldLabel{display:block;color:var(--muted);font-size:13px}button{background:#234858;color:var(--text);font-weight:900;cursor:pointer}button.primary{background:var(--blue);border-color:#3f819d;color:#fff}.modes{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.active{background:var(--accent)!important;border-color:var(--accent)!important;color:#081317!important}.danger{background:#321418}.heat{background:#3b1807}.cool{background:#123040}.profileRows{display:grid;gap:8px}.profileRow{display:grid;grid-template-columns:1fr 110px;gap:8px}.thresholds{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}.thresholds label{color:var(--muted);font-size:13px}
a{color:#79d4ff}.footer{margin-top:12px;color:#8da2b0;font-size:13px}@media(max-width:760px){main{padding:12px}.controls{grid-template-columns:1fr}.readout{display:block}.outputs{grid-template-columns:1fr 1fr;margin-top:14px}.temp{font-size:58px}.unit{font-size:22px}.thresholds{grid-template-columns:1fr}}
</style></head>
<body><main><div class="shell">
<div class="top"><div><div class="brand">Ferment<span>Dial</span></div><div class="deviceName" id="fermenterNameTop">Fermenter</div></div><div class="statusbar"><span class="pill" id="wifi">Wi-Fi</span><span class="pill demo" id="demo" hidden>DEMO SENSOR</span></div></div>
<section class="hero" id="hero">
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
<div class="footer"><span id="fault">NONE</span> - <a href="/settings">Settings</a> - <a href="/metrics">Metrics</a></div>
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
 document.title=s.fermenterName+' - FermentDial';
 document.querySelectorAll('.unit').forEach(e=>e.textContent=deg+s.unit);
 temp.textContent=s.tempValid?s.temperature.toFixed(1):'--.-';
 fermenterName.textContent=s.fermenterName; fermenterNameTop.textContent=s.fermenterName; fermenterNameHero.textContent=s.fermenterName;
 profileName.textContent=s.profileName; target.textContent=s.target.toFixed(1); targetHero.textContent=s.profileName+' target '+s.target.toFixed(1)+deg+s.unit; if(document.activeElement!==targetInput)targetInput.value=s.target.toFixed(1);
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

String NetworkManager::settingsHtml() const {
  const String apSsid = setupApSsid();
  String html = R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FermentDial Settings</title>
<style>
:root{--bg:#0d1b1e;--panel:#132428;--line:#1e3840;--muted:#6a9aaa;--text:#d0e8f0;--accent:#b0d8f8;--blue:#356f89;--gold:#ffd178}
*{box-sizing:border-box}body{margin:0;font-family:"Trebuchet MS","Avenir Next",Verdana,sans-serif;background:var(--bg);color:var(--text)}
main{max-width:920px;margin:auto;padding:16px}.top{display:flex;justify-content:space-between;gap:10px;align-items:center;margin-bottom:12px;flex-wrap:wrap}
h1{font-size:22px;margin:0;color:var(--accent)}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px}.panel{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:14px}
h2{font-size:16px;margin:0 0 12px;color:#d9e8f4}.hint{color:var(--muted);font-size:13px;line-height:1.35}.warn{color:var(--gold)}
label{display:block;color:var(--muted);font-size:13px;margin-top:8px}input,select,button{font:inherit;width:100%;border:1px solid var(--line);border-radius:8px;padding:12px;margin-top:5px}
input,select{background:#102126;color:var(--text)}input[type=checkbox]{width:auto;margin-right:7px}button{background:var(--blue);color:white;font-weight:900;cursor:pointer}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}.nav a{color:#79d4ff;margin-left:12px}.status{border:1px solid var(--line);border-radius:8px;padding:10px;background:#102126;color:var(--accent);font-weight:900}
@media(max-width:640px){main{padding:12px}.row{grid-template-columns:1fr}.nav a{margin-left:0;margin-right:10px}}
</style></head><body><main>
<div class="top"><h1>FermentDial Settings</h1><div class="nav"><a href="/dashboard">Dashboard</a><a href="/metrics">Metrics</a></div></div>
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
<section class="panel"><h2>Wi-Fi</h2>
<form method="post" action="/wifi">
<label>Wi-Fi name<input name="ssid" autocomplete="off" required value=")HTML";
  html += htmlEscape(_wifiSsid);
  html += R"HTML("></label>
<label>Password<input name="pass" type="password" placeholder="leave blank to keep saved"></label>
<button type="submit">Save and reboot</button>
</form>
<p class="hint">Connected IP: )HTML";
  html += htmlEscape(_snapshot.ipAddress.length() > 0 ? _snapshot.ipAddress
                                                       : "not connected");
  html += R"HTML(</p>
<p class="hint">Setup AP: )HTML";
  html += htmlEscape(apSsid);
  html += R"HTML( / password: fermentdial</p></section>
<section class="panel"><h2>Prometheus</h2>
<div class="status">/metrics</div>
<p class="hint">Scrape <code>http://)HTML";
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
</div></main></body></html>)HTML";
  return html;
}

String NetworkManager::setupHtml() const {
  String html = R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FermentDial Wi-Fi</title>
<style>
body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0}
main{max-width:480px;margin:auto;padding:24px}.card{border-radius:8px;background:#132428;border:1px solid #1e3840;padding:22px}
input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0 16px;padding:14px;border-radius:8px;border:1px solid #1e3840}
input{background:#102126;color:#d0e8f0}button{background:#356f89;color:white;font-weight:900}.hint{color:#a9bac8}a{color:#79d4ff}
</style></head><body><main><div class="card">
<h1>FermentDial Wi-Fi</h1>
<p class="hint">Join your fermentation controller to your home Wi-Fi.</p>
<p><a href="/dashboard">Open live dashboard</a></p>
<p><a href="/settings">Device settings</a></p>
<form method="post" action="/wifi">
<label>Wi-Fi name</label><input name="ssid" autocomplete="off" required>
<label>Password</label><input name="pass" type="password">
<button type="submit">Save and reboot</button>
</form>
<p class="hint">Setup AP: )HTML";
  html += htmlEscape(setupApSsid());
  html += R"HTML( / fermentdial</p>
</div></main></body></html>)HTML";
  return html;
}

String NetworkManager::firmwareHtml() const {
  return R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FermentDial Firmware</title>
<style>
body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0}
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
