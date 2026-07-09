#include "network.h"

#include "history.h"
#include "network_detail.h"
#include "status_hint.h"
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

void NetworkManager::clearLiveHistory() {
  _historyCount = 0;
  _historyHead = 0;
  _lastSampleMs = 0;
}

void NetworkManager::recordHistory(uint32_t nowMs, bool valid, float tempC,
                                   const HydrometerReading &hydro) {
  if (!valid || isnan(tempC)) {
    return;  // only store real readings; gaps just leave the trace shorter
  }
  if (_historyCount > 0 && (nowMs - _lastSampleMs) < HISTORY_INTERVAL_MS) {
    return;
  }
  _lastSampleMs = nowMs;
  _historyTempC[_historyHead] = static_cast<int16_t>(lroundf(tempC * 10.0f));
  const bool hydroFresh = hydro.valid && !hydro.stale;
  _historyGravityValid[_historyHead] =
      hydroFresh && gravityIsValid(hydro.gravity);
  _historyHydroTempValid[_historyHead] =
      hydroFresh && !isnan(hydro.temperatureC);
  _historyGravity[_historyHead] = _historyGravityValid[_historyHead]
                                      ? static_cast<uint16_t>(
                                            lroundf(hydro.gravity * 10000.0f))
                                      : 0;
  _historyHydroTempC[_historyHead] =
      _historyHydroTempValid[_historyHead]
          ? static_cast<int16_t>(lroundf(hydro.temperatureC * 10.0f))
          : 0;
  _historyHead = (_historyHead + 1) % HISTORY_LEN;
  if (_historyCount < HISTORY_LEN) {
    _historyCount++;
  }
}

String NetworkManager::historyJson() const {
  // Oldest sample first so the client can plot left-to-right.
  const uint16_t start =
      _historyCount < HISTORY_LEN ? 0 : _historyHead;
  String out = "{\"intervalMs\":" + String(HISTORY_INTERVAL_MS) +
               ",\"capacity\":" + String(HISTORY_LEN) + ",\"count\":" +
               String(_historyCount) + ",\"tempsC\":[";
  for (uint16_t i = 0; i < _historyCount; ++i) {
    if (i > 0) {
      out += ",";
    }
    out += String(_historyTempC[(start + i) % HISTORY_LEN] / 10.0f, 1);
  }
  out += "],\"gravity\":[";
  for (uint16_t i = 0; i < _historyCount; ++i) {
    if (i > 0) {
      out += ",";
    }
    const uint16_t idx = (start + i) % HISTORY_LEN;
    out += _historyGravityValid[idx]
               ? String(_historyGravity[idx] / 10000.0f, 4)
               : String("null");
  }
  out += "],\"hydroTempsC\":[";
  for (uint16_t i = 0; i < _historyCount; ++i) {
    if (i > 0) {
      out += ",";
    }
    const uint16_t idx = (start + i) % HISTORY_LEN;
    out += _historyHydroTempValid[idx]
               ? String(_historyHydroTempC[idx] / 10.0f, 1)
               : String("null");
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

String NetworkManager::selfCheckJson(uint32_t nowMs) const {
  const bool f = _webStatus.unitsFahrenheit;
  String json = "[";
  bool first = true;
  auto add = [&](const String &id, const String &label, const char *status,
                 const String &detail) {
    if (!first) {
      json += ",";
    }
    first = false;
    json += "{\"id\":" + jsonString(id) + ",\"label\":" + jsonString(label) +
            ",\"status\":\"" + String(status) +
            "\",\"detail\":" + jsonString(detail) + "}";
  };

  if (_webStatus.demoSensor) {
    add("sensor", "Temperature sensor", "warn",
        "Demo sensor - simulated readings, not for real fermentation.");
  } else if (_webStatus.tempValid) {
    add("sensor", "Temperature sensor", "ok",
        "Reading " + String(toDisplayTemp(_webStatus.tempC, f), 1) +
            unitLabel(f));
  } else {
    add("sensor", "Temperature sensor", "fail",
        "No valid reading - check the DS18B20 wiring (GPIO13, 3.3V).");
  }

  if (_webStatus.demoSensor) {
    add("outputs", "Heater & pump outputs", "warn",
        "Demo build - physical outputs are forced OFF.");
  } else {
    add("outputs", "Heater & pump outputs", "ok",
        "Interlock active (never both on). Bench-test from Dial settings.");
  }

  if (_settings == nullptr || !_settings->hydrometerBleEnabled) {
    add("hydrometer", "Hydrometer", "warn", "BLE scanning is disabled.");
  } else if (_settings->hydrometerSelectionKey.length() == 0) {
    add("hydrometer", "Hydrometer", "warn",
        "Scanning, but no device is selected.");
  } else {
    HydrometerReading sel;
    if (_hydrometer != nullptr) {
      sel = _hydrometer->selectedReading(*_settings, nowMs);
    }
    if (!sel.valid || sel.stale) {
      add("hydrometer", "Hydrometer", "fail",
          "Selected device is not reporting (stale).");
    } else {
      add("hydrometer", "Hydrometer", "ok",
          "SG " + String(sel.gravity, 3) + " from " +
              (sel.label.length() ? sel.label : String("device")));
    }
  }

  if (_snapshot.wifiConnected) {
    add("wifi", "Wi-Fi", "ok", _snapshot.ipAddress);
  } else {
    add("wifi", "Wi-Fi", "warn",
        "Not connected - local control still runs without it.");
  }

  auto integrationStatus = [](const String &s) -> const char * {
    if (s.startsWith("OK") || s == "Saved" || s == "Connected" ||
        s == "Waiting") {
      return "ok";
    }
    return "warn";
  };
  if (_mqttConfig.enabled) {
    add("mqtt", "MQTT / Home Assistant", integrationStatus(_lastMqttStatus),
        _lastMqttStatus);
  }
  if (_brewfather.enabled) {
    add("brewfather", "Brewfather", integrationStatus(_lastBrewfatherStatus),
        _lastBrewfatherStatus);
  }
  if (_influx.enabled) {
    add("influx", "InfluxDB", integrationStatus(_lastInfluxStatus),
        _lastInfluxStatus);
  }

  json += "]";
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
  json += "\"uptimeSeconds\":" + String(nowMs / 1000U) + ",";
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
            ",\"default\":" + jsonFloat(profileDefault) +
            ",\"editable\":" +
            String(profileSlotEditable(i) ? "true" : "false") + "}";
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
  json += "\"historyLogging\":" +
#if FERM_ENABLE_NETWORK
          String("true") +
#else
          String((_settings != nullptr && _settings->historyLoggingEnabled)
                     ? "true"
                     : "false") +
#endif
          ",";
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
  {
    ControlHintInput hintIn;
    hintIn.settings = _settings;
    hintIn.runtimeState = _webStatus.runtimeState;
    hintIn.faultCode = _webStatus.faultCode;
    hintIn.tempValid = _webStatus.tempValid;
    hintIn.tempC = _webStatus.tempC;
    hintIn.pumpOn = _webStatus.pumpOn;
    hintIn.pumpOffElapsedMs = _webStatus.pumpOffElapsedMs;
    hintIn.hydroSelected =
        selected.selected;
    hintIn.hydroStale = selected.selected && selected.stale;
    hintIn.notReaching = _webStatus.notReaching;
    hintIn.longOutput = _webStatus.longOutput;
    const ControlHint hint = buildControlHint(hintIn);
    json += "\"hint\":" + jsonString(String(hint.primary)) + ",";
    json += "\"hintDetail\":" + jsonString(String(hint.detail)) + ",";
    json += "\"attention\":[";
    bool firstAttn = true;
    if (hint.attention & ATTN_FAULT) {
      json += jsonString(String("fault"));
      firstAttn = false;
    }
    if (hint.attention & ATTN_HYDRO_STALE) {
      if (!firstAttn) {
        json += ",";
      }
      json += jsonString(String("hydro-stale"));
      firstAttn = false;
    }
    if (hint.attention & ATTN_NOT_REACHING) {
      if (!firstAttn) {
        json += ",";
      }
      json += jsonString(String("not-reaching-target"));
      firstAttn = false;
    }
    if (hint.attention & ATTN_LONG_OUTPUT) {
      if (!firstAttn) {
        json += ",";
      }
      json += jsonString(String("long-runtime"));
    }
    json += "],";
    // Human-readable first reason (matches dial attentionReasonText).
    json += "\"attentionText\":" +
            jsonString(String(attentionReasonText(hint.attention))) + ",";
  }
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

} // namespace ferm
