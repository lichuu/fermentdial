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

} // namespace ferm
