#include "storage.h"

namespace ferm {

namespace {

String profileNameKey(uint8_t index) {
  return String("profName") + String(index);
}

String profileTargetKey(uint8_t index) {
  return String("profTgt") + String(index);
}

String programKey(uint8_t index) {
  return String("prog") + String(index);
}

} // namespace

void SettingsStorage::begin() { _prefs.begin("fermctl", false); }

bool SettingsStorage::load(Settings &settings) {
  Settings defaults;
  settings = defaults;

  uint16_t version = _prefs.getUShort("version", 0);
  if (version == 0) {
    // Nothing stored yet (fresh or erased NVS): keep safe defaults.
    sanitizeSettings(settings);
    return false;
  }

  // Single additive load path. Every field below reads its own key with a
  // default, so any older store upgrades in place: keys that did not exist yet
  // fall back to their default, and the record is re-stamped to SETTINGS_VERSION
  // at the end. Schema changes must stay additive (add new keys, never
  // reinterpret existing ones), which is why no per-version migration code is
  // needed here.
  settings.version = version;
  settings.fermenterName =
      _prefs.getString("fermName", DEFAULT_FERMENTER_NAME);
  settings.activeProfile =
      _prefs.getUChar("profile", static_cast<uint8_t>(ProfileSlot::Ale));
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
      "dRestRet", static_cast<uint8_t>(ProfileSlot::Ale));
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
  settings.gradualCrashEnabled = _prefs.getBool("gradCrash", false);
  settings.gradualCrashStepC =
      _prefs.getFloat("gradStepC", DEFAULT_GRADUAL_CRASH_STEP_C);
  settings.gradualCrashStepIntervalHours = _prefs.getUInt(
      "gradStepHrs", DEFAULT_GRADUAL_CRASH_STEP_INTERVAL_HOURS);
  for (uint8_t i = 0; i < PROGRAM_SLOT_COUNT; ++i) {
    const String key = programKey(i);
    if (_prefs.getBytesLength(key.c_str()) == sizeof(ProgramSettings)) {
      _prefs.getBytes(key.c_str(), &settings.programs[i],
                      sizeof(ProgramSettings));
    }
  }
  settings.programActive = _prefs.getBool("progAct", false);
  settings.programRunIndex = _prefs.getUChar("progRun", 0);
  settings.programStepIndex = _prefs.getUChar("progStep", 0);
  settings.programStepElapsedSeconds = _prefs.getUInt("progElap", 0);
  settings.programStepStartTargetC =
      _prefs.getFloat("progStart", DEFAULT_TARGET_C);
  settings.historyLoggingEnabled = _prefs.getBool("histLog", false);
  if (settings.diacetylRestActive &&
      settings.diacetylRestRemainingSeconds == 0) {
    settings.diacetylRestRemainingSeconds =
        settings.diacetylRestDurationSeconds;
  }
  sanitizeSettings(settings);
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
  _prefs.putBool("gradCrash", copy.gradualCrashEnabled);
  _prefs.putFloat("gradStepC", copy.gradualCrashStepC);
  _prefs.putUInt("gradStepHrs", copy.gradualCrashStepIntervalHours);
  for (uint8_t i = 0; i < PROGRAM_SLOT_COUNT; ++i) {
    _prefs.putBytes(programKey(i).c_str(), &copy.programs[i],
                    sizeof(ProgramSettings));
  }
  _prefs.putBool("progAct", copy.programActive);
  _prefs.putUChar("progRun", copy.programRunIndex);
  _prefs.putUChar("progStep", copy.programStepIndex);
  _prefs.putUInt("progElap", copy.programStepElapsedSeconds);
  _prefs.putFloat("progStart", copy.programStepStartTargetC);
  _prefs.putBool("histLog", copy.historyLoggingEnabled);
  _pending = false;
}

} // namespace ferm
