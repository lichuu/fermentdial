#include "storage.h"

namespace ferm {

namespace {

String profileNameKey(uint8_t index) {
  return String("profName") + String(index);
}

String profileTargetKey(uint8_t index) {
  return String("profTgt") + String(index);
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
    sanitizeSettings(settings);
    saveNow(settings);
    return true;
  }

  if (version != SETTINGS_VERSION) {
    sanitizeSettings(settings);
    return false;
  }

  settings.version = version;
  settings.activeProfile =
      _prefs.getUChar("profile", static_cast<uint8_t>(ProfileSlot::Ferment));
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    settings.profiles[i].name =
        _prefs.getString(profileNameKey(i).c_str(), defaultProfileName(i));
    settings.profiles[i].targetC =
        _prefs.getFloat(profileTargetKey(i).c_str(), defaultProfileTargetC(i));
  }
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
  sanitizeSettings(settings);
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
  _prefs.putUChar("profile", copy.activeProfile);
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _prefs.putString(profileNameKey(i).c_str(), copy.profiles[i].name);
    _prefs.putFloat(profileTargetKey(i).c_str(), copy.profiles[i].targetC);
  }
  _prefs.putFloat("coolOn", copy.coolOnDeltaC);
  _prefs.putFloat("heatOn", copy.heatOnDeltaC);
  _prefs.putFloat("hold", copy.holdDeltaC);
  _prefs.putUChar("mode", static_cast<uint8_t>(copy.mode));
  _prefs.putUInt("pumpOff", copy.pumpMinOffSeconds);
  _prefs.putUInt("pumpRun", copy.pumpMinRunSeconds);
  _prefs.putFloat("offsetC", copy.tempOffsetC);
  _prefs.putBool("unitsF", copy.unitsFahrenheit);
  _pending = false;
}

} // namespace ferm
