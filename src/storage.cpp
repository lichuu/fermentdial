#include "storage.h"

namespace ferm {

void SettingsStorage::begin() {
  _prefs.begin("fermctl", false);
}

bool SettingsStorage::load(Settings &settings) {
  Settings defaults;
  settings = defaults;

  uint16_t version = _prefs.getUShort("version", 0);
  if (version != SETTINGS_VERSION) {
    sanitizeSettings(settings);
    return false;
  }

  settings.version = version;
  settings.targetF = _prefs.getFloat("targetF", DEFAULT_TARGET_F);
  settings.hysteresisF = _prefs.getFloat("hystF", DEFAULT_HYSTERESIS_F);
  settings.mode = static_cast<UserMode>(_prefs.getUChar("mode", static_cast<uint8_t>(UserMode::Off)));
  settings.pumpMinOffSeconds = _prefs.getUInt("pumpOff", DEFAULT_PUMP_MIN_OFF_SECONDS);
  settings.pumpMinRunSeconds = _prefs.getUInt("pumpRun", DEFAULT_PUMP_MIN_RUN_SECONDS);
  settings.tempOffsetF = _prefs.getFloat("offsetF", DEFAULT_TEMP_OFFSET_F);
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
  _prefs.putFloat("targetF", copy.targetF);
  _prefs.putFloat("hystF", copy.hysteresisF);
  _prefs.putUChar("mode", static_cast<uint8_t>(copy.mode));
  _prefs.putUInt("pumpOff", copy.pumpMinOffSeconds);
  _prefs.putUInt("pumpRun", copy.pumpMinRunSeconds);
  _prefs.putFloat("offsetF", copy.tempOffsetF);
  _prefs.putBool("unitsF", copy.unitsFahrenheit);
  _pending = false;
}

}  // namespace ferm

