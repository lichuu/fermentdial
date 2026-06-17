#include "storage.h"

namespace ferm {

namespace {

String profileNameKey(uint8_t index) {
  return String("profName") + String(index);
}

String profileTargetKey(uint8_t index) {
  return String("profTgt") + String(index);
}

bool targetNear(float actualC, float expectedC) {
  return fabsf(actualC - expectedC) < 0.12f;
}

bool migrateDefaultProfileTarget(Settings &settings, ProfileSlot slot,
                                 float staleTargetC, float newTargetC) {
  const uint8_t index = static_cast<uint8_t>(slot);
  if (!settings.profiles[index].name.equals(defaultProfileName(index)) ||
      !targetNear(settings.profiles[index].targetC, staleTargetC)) {
    return false;
  }
  settings.profiles[index].targetC = newTargetC;
  return true;
}

bool migrateProfileDefaults(Settings &settings) {
  bool changed = false;
  changed |= migrateDefaultProfileTarget(settings, ProfileSlot::Crash,
                                         fToC(38.0f), DEFAULT_CRASH_TARGET_C);
  changed |= migrateDefaultProfileTarget(settings, ProfileSlot::Crash,
                                         fToC(39.9f), DEFAULT_CRASH_TARGET_C);
  changed |= migrateDefaultProfileTarget(settings, ProfileSlot::Lager,
                                         fToC(50.0f), DEFAULT_LAGER_TARGET_C);
  return changed;
}

} // namespace

void SettingsStorage::begin() { _prefs.begin("fermctl", false); }

bool SettingsStorage::load(Settings &settings) {
  Settings defaults;
  settings = defaults;

  uint16_t version = _prefs.getUShort("version", 0);
  if (version == SETTINGS_VERSION_FAHRENHEIT_STORAGE) {
    settings.version = SETTINGS_VERSION;
    settings.profiles[static_cast<uint8_t>(ProfileSlot::Ferment)].targetC =
        fToC(_prefs.getFloat("targetF", DEFAULT_TARGET_F));
    settings.coolOnDeltaC =
        deltaFToC(_prefs.getFloat("hystF", DEFAULT_COOL_ON_DELTA_F));
    settings.heatOnDeltaC = DEFAULT_HEAT_ON_DELTA_C;
    settings.holdDeltaC = DEFAULT_HOLD_DELTA_C;
    settings.mode = static_cast<UserMode>(
        _prefs.getUChar("mode", static_cast<uint8_t>(UserMode::Off)));
    settings.pumpMinOffSeconds =
        _prefs.getUInt("pumpOff", DEFAULT_PUMP_MIN_OFF_SECONDS);
    settings.pumpMinRunSeconds =
        _prefs.getUInt("pumpRun", DEFAULT_PUMP_MIN_RUN_SECONDS);
    settings.tempOffsetC =
        deltaFToC(_prefs.getFloat("offsetF", DEFAULT_TEMP_OFFSET_F));
    settings.unitsFahrenheit = _prefs.getBool("unitsF", true);
    settings.liveTargetC = activeTargetC(settings);
    sanitizeSettings(settings);
    saveNow(settings);
    return true;
  }

  if (version == SETTINGS_VERSION_SINGLE_TARGET_STORAGE) {
    settings.version = SETTINGS_VERSION;
    settings.profiles[static_cast<uint8_t>(ProfileSlot::Ferment)].targetC =
        _prefs.getFloat("targetC", DEFAULT_TARGET_C);
    settings.coolOnDeltaC = _prefs.getFloat("hystC", DEFAULT_COOL_ON_DELTA_C);
    settings.heatOnDeltaC = DEFAULT_HEAT_ON_DELTA_C;
    settings.holdDeltaC = DEFAULT_HOLD_DELTA_C;
    settings.mode = static_cast<UserMode>(
        _prefs.getUChar("mode", static_cast<uint8_t>(UserMode::Off)));
    settings.pumpMinOffSeconds =
        _prefs.getUInt("pumpOff", DEFAULT_PUMP_MIN_OFF_SECONDS);
    settings.pumpMinRunSeconds =
        _prefs.getUInt("pumpRun", DEFAULT_PUMP_MIN_RUN_SECONDS);
    settings.tempOffsetC = _prefs.getFloat("offsetC", DEFAULT_TEMP_OFFSET_C);
    settings.unitsFahrenheit = _prefs.getBool("unitsF", true);
    settings.liveTargetC = activeTargetC(settings);
    sanitizeSettings(settings);
    saveNow(settings);
    return true;
  }

  if (version != SETTINGS_VERSION &&
      version != 8 &&
      version != SETTINGS_VERSION_PROFILE_DEFAULTS_STORAGE &&
      version != SETTINGS_VERSION_FERMENTER_NAME_STORAGE &&
      version != SETTINGS_VERSION_PRELIVE_TARGET_STORAGE &&
      version != SETTINGS_VERSION_DIACETYL_REST_STORAGE &&
      version != SETTINGS_VERSION_HYDROMETER_STORAGE) {
    sanitizeSettings(settings);
    return false;
  }

  settings.version = version;
  settings.fermenterName =
      _prefs.getString("fermName", DEFAULT_FERMENTER_NAME);
  settings.activeProfile =
      _prefs.getUChar("profile", static_cast<uint8_t>(ProfileSlot::Ferment));
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    settings.profiles[i].name =
        _prefs.getString(profileNameKey(i).c_str(), defaultProfileName(i));
    settings.profiles[i].targetC =
        _prefs.getFloat(profileTargetKey(i).c_str(), defaultProfileTargetC(i));
  }
  // Live setpoint (v7+). Older stores fall back to the active profile preset.
  settings.liveTargetC = _prefs.getFloat("liveTgt", activeTargetC(settings));
  settings.diacetylRestActive = _prefs.getBool("dRestAct", false);
  settings.diacetylRestTargetC =
      _prefs.getFloat("dRestTgt", DEFAULT_DIACETYL_REST_TARGET_C);
  settings.diacetylRestDurationSeconds = _prefs.getUInt(
      "dRestDur", DEFAULT_DIACETYL_REST_DURATION_SECONDS);
  settings.diacetylRestRemainingSeconds = _prefs.getUInt("dRestRem", 0);
  settings.diacetylRestReturnProfile = _prefs.getUChar(
      "dRestRet", static_cast<uint8_t>(ProfileSlot::Ferment));
  settings.coolOnDeltaC = _prefs.getFloat("coolOn", DEFAULT_COOL_ON_DELTA_C);
  settings.heatOnDeltaC = _prefs.getFloat("heatOn", DEFAULT_HEAT_ON_DELTA_C);
  settings.holdDeltaC = _prefs.getFloat("hold", DEFAULT_HOLD_DELTA_C);
  settings.mode = static_cast<UserMode>(
      _prefs.getUChar("mode", static_cast<uint8_t>(UserMode::Off)));
  settings.pumpMinOffSeconds =
      _prefs.getUInt("pumpOff", DEFAULT_PUMP_MIN_OFF_SECONDS);
  settings.pumpMinRunSeconds =
      _prefs.getUInt("pumpRun", DEFAULT_PUMP_MIN_RUN_SECONDS);
  settings.tempOffsetC = _prefs.getFloat("offsetC", DEFAULT_TEMP_OFFSET_C);
  settings.unitsFahrenheit = _prefs.getBool("unitsF", true);
  settings.brightness = _prefs.getUChar("bright", DEFAULT_BRIGHTNESS);
  settings.hydrometerBleEnabled = _prefs.getBool("hydroBle", true);
  settings.hydrometerScanType = static_cast<HydrometerScanType>(
      _prefs.getUChar("hydroScan",
                      static_cast<uint8_t>(HydrometerScanType::Unknown)));
  settings.hydrometerSelectionKey =
      _prefs.getString("hydroSel", DEFAULT_HYDROMETER_SELECTION_KEY);
  settings.hydrometerOriginalGravity =
      _prefs.getFloat("hydroOg", NAN);
  settings.hydrometerStableGravity =
      _prefs.getFloat("hydroStableG", NAN);
  settings.hydrometerStableSeconds =
      _prefs.getUInt("hydroStableS", 0);
  if (settings.diacetylRestActive &&
      settings.diacetylRestRemainingSeconds == 0) {
    settings.diacetylRestRemainingSeconds =
        settings.diacetylRestDurationSeconds;
  }
  sanitizeSettings(settings);
  if (version == SETTINGS_VERSION_PROFILE_DEFAULTS_STORAGE) {
    if (migrateProfileDefaults(settings)) {
      sanitizeSettings(settings);
    }
  }
  if (version != SETTINGS_VERSION) {
    saveNow(settings);
  }
  return true;
}

void SettingsStorage::scheduleSave(uint32_t nowMs) {
  _pending = true;
  _saveAtMs = nowMs + SETTINGS_SAVE_DEBOUNCE_MS;
}

void SettingsStorage::loop(uint32_t nowMs, const Settings &settings) {
  if (!_pending || nowMs < _saveAtMs) {
    return;
  }
  saveNow(settings);
}

void SettingsStorage::saveNow(const Settings &settings) {
  Settings copy = settings;
  sanitizeSettings(copy);

  _prefs.putUShort("version", SETTINGS_VERSION);
  _prefs.putString("fermName", copy.fermenterName);
  _prefs.putUChar("profile", copy.activeProfile);
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _prefs.putString(profileNameKey(i).c_str(), copy.profiles[i].name);
    _prefs.putFloat(profileTargetKey(i).c_str(), copy.profiles[i].targetC);
  }
  _prefs.putFloat("liveTgt", copy.liveTargetC);
  _prefs.putBool("dRestAct", copy.diacetylRestActive);
  _prefs.putFloat("dRestTgt", copy.diacetylRestTargetC);
  _prefs.putUInt("dRestDur", copy.diacetylRestDurationSeconds);
  _prefs.putUInt("dRestRem", copy.diacetylRestRemainingSeconds);
  _prefs.putUChar("dRestRet", copy.diacetylRestReturnProfile);
  _prefs.putFloat("coolOn", copy.coolOnDeltaC);
  _prefs.putFloat("heatOn", copy.heatOnDeltaC);
  _prefs.putFloat("hold", copy.holdDeltaC);
  _prefs.putUChar("mode", static_cast<uint8_t>(copy.mode));
  _prefs.putUInt("pumpOff", copy.pumpMinOffSeconds);
  _prefs.putUInt("pumpRun", copy.pumpMinRunSeconds);
  _prefs.putFloat("offsetC", copy.tempOffsetC);
  _prefs.putBool("unitsF", copy.unitsFahrenheit);
  _prefs.putUChar("bright", copy.brightness);
  _prefs.putBool("hydroBle", copy.hydrometerBleEnabled);
  _prefs.putUChar("hydroScan",
                  static_cast<uint8_t>(copy.hydrometerScanType));
  _prefs.putString("hydroSel", copy.hydrometerSelectionKey);
  _prefs.putFloat("hydroOg", copy.hydrometerOriginalGravity);
  _prefs.putFloat("hydroStableG", copy.hydrometerStableGravity);
  _prefs.putUInt("hydroStableS", copy.hydrometerStableSeconds);
  _pending = false;
}

} // namespace ferm
