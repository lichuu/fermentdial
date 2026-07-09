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

  if (_server.hasArg("brightness")) {
    const int raw = _server.arg("brightness").toInt();
    if (_server.hasArg("brightnessPreview") &&
        _server.arg("brightnessPreview").toInt() != 0) {
      previewWebBrightness(raw);
    } else {
      applyWebBrightness(raw);
      changed = true;
    }
  }

  if (_server.hasArg("profile")) {
    int requestedProfile = _server.arg("profile").toInt();
    if (requestedProfile < 0 || requestedProfile >= PROFILE_COUNT) {
      _server.send(400, "application/json",
                   "{\"ok\":false,\"error\":\"invalid profile\"}");
      return;
    }
    activateProfile(*_settings, static_cast<uint8_t>(requestedProfile));
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

  if (applyHydrometerSettingsFromPost(_server, *_settings)) {
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

  if (applyEditableProfileFieldsFromPost(_server, *_settings, unitsF)) {
    changed = true;
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
  if (_server.hasArg("historyLogging")) {
    _settings->historyLoggingEnabled = true;
    changed = true;
  }
  if ((_server.hasArg("hydrometerBleEnabled") ||
       _server.hasArg("hydrometerScanType")) &&
      _settings->hydrometerBleEnabled &&
      _settings->hydrometerScanType != HydrometerScanType::Unknown) {
    _settings->historyLoggingEnabled = true;
    changed = true;
  }
  if (_server.hasArg("fermentReset") && _server.arg("fermentReset").toInt() != 0) {
    if (_fermentResetCallback != nullptr) {
      _fermentResetCallback();
    }
    changed = true;
  }
  if (_server.hasArg("batchAction")) {
    String action = _server.arg("batchAction");
    action.trim();
    action.toLowerCase();
    if (action == "new") {
      if (_server.hasArg("batchName")) {
        _settings->batchName = _server.arg("batchName");
      }
      // Prefer wall clock when available; otherwise leave 0 (unknown).
      const Timestamp ts = nowEpochOrUptime(millis());
      _settings->batchStartedAt = ts.wallClock ? ts.seconds : 0;
      if (_eventLog != nullptr) {
        String msg = "Batch started";
        if (_settings->batchName.length() > 0) {
          msg += ": ";
          msg += _settings->batchName;
        }
        _eventLog->add(EventType::Info, msg, ts);
      }
      changed = true;
    } else if (action == "name" && _server.hasArg("batchName")) {
      _settings->batchName = _server.arg("batchName");
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
    _webStatus.brightness = _settings->brightness;
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

void NetworkManager::handleScreenInputPost() {
#if FERM_ENABLE_NETWORK && FERM_ENABLE_SCREEN_MIRROR
  if (_screenInputCallback == nullptr) {
    _server.send(503, "text/plain", "Screen input unavailable");
    return;
  }
  if (!_server.hasArg("type")) {
    _server.send(400, "text/plain", "Missing type");
    return;
  }

  ScreenInputEvent event{};
  const String type = _server.arg("type");
  if (type == "tap") {
    event.kind = ScreenInputKind::Tap;
    event.x = static_cast<int16_t>(constrain(_server.arg("x").toInt(), 0, 239));
    event.y = static_cast<int16_t>(constrain(_server.arg("y").toInt(), 0, 239));
  } else if (type == "swipe") {
    event.kind = ScreenInputKind::Swipe;
    event.dx =
        static_cast<int16_t>(constrain(_server.arg("dx").toInt(), -240, 240));
    event.dy =
        static_cast<int16_t>(constrain(_server.arg("dy").toInt(), -240, 240));
  } else if (type == "hold") {
    event.kind = ScreenInputKind::Hold;
  } else if (type == "scroll") {
    event.kind = ScreenInputKind::Scroll;
    event.delta =
        static_cast<int16_t>(constrain(_server.arg("delta").toInt(), -64, 64));
  } else {
    _server.send(400, "text/plain", "Unknown type");
    return;
  }

  _screenInputCallback(event);
  _server.send(204, "text/plain", "");
#endif
}

} // namespace ferm
