#include "storage.h"

namespace ferm {

namespace {

String profileNameKey(uint8_t index) {
  return String("profName") + String(index);
}

String profileTargetKey(uint8_t index) {
  return String("profTgt") + String(index);
}

// Legacy POD blob keys (prog0/prog1). Read for one-time migration only.
String programBlobKey(uint8_t index) {
  return String("prog") + String(index);
}

// Field keys stay within the ESP32 NVS 15-character limit (p0cnt, p0s3t, ...).
String programCountKey(uint8_t index) {
  return String("p") + String(index) + "cnt";
}

String programStepKey(uint8_t progIndex, uint8_t stepIndex, char field) {
  return String("p") + String(progIndex) + "s" + String(stepIndex) + field;
}

void loadProgram(Preferences &prefs, uint8_t index, ProgramSettings &program,
                 bool &needsRewrite) {
  const String cntKey = programCountKey(index);
  if (prefs.isKey(cntKey.c_str())) {
    program.stepCount = prefs.getUChar(cntKey.c_str(), 0);
    for (uint8_t s = 0; s < MAX_PROGRAM_STEPS; ++s) {
      ProfileStep &step = program.steps[s];
      step.type = static_cast<StepType>(prefs.getUChar(
          programStepKey(index, s, 't').c_str(),
          static_cast<uint8_t>(StepType::Hold)));
      step.exit = static_cast<StepExit>(prefs.getUChar(
          programStepKey(index, s, 'e').c_str(),
          static_cast<uint8_t>(StepExit::Time)));
      step.targetC =
          prefs.getFloat(programStepKey(index, s, 'g').c_str(), DEFAULT_TARGET_C);
      step.durationSeconds =
          prefs.getUInt(programStepKey(index, s, 'd').c_str(), 0);
      step.gravityThreshold =
          prefs.getFloat(programStepKey(index, s, 'b').c_str(), 1.010f);
      step.stableHours =
          prefs.getUShort(programStepKey(index, s, 'h').c_str(), 24);
    }
    return;
  }

  const String blobKey = programBlobKey(index);
  if (prefs.getBytesLength(blobKey.c_str()) == sizeof(ProgramSettings)) {
    prefs.getBytes(blobKey.c_str(), &program, sizeof(ProgramSettings));
    needsRewrite = true;
  }
}

void saveProgram(Preferences &prefs, uint8_t index,
                 const ProgramSettings &program) {
  prefs.putUChar(programCountKey(index).c_str(), program.stepCount);
  for (uint8_t s = 0; s < MAX_PROGRAM_STEPS; ++s) {
    const ProfileStep &step = program.steps[s];
    prefs.putUChar(programStepKey(index, s, 't').c_str(),
                   static_cast<uint8_t>(step.type));
    prefs.putUChar(programStepKey(index, s, 'e').c_str(),
                   static_cast<uint8_t>(step.exit));
    prefs.putFloat(programStepKey(index, s, 'g').c_str(), step.targetC);
    prefs.putUInt(programStepKey(index, s, 'd').c_str(), step.durationSeconds);
    prefs.putFloat(programStepKey(index, s, 'b').c_str(), step.gravityThreshold);
    prefs.putUShort(programStepKey(index, s, 'h').c_str(), step.stableHours);
  }
  prefs.remove(programBlobKey(index).c_str());
}

} // namespace

void SettingsStorage::begin() { _prefs.begin("fermctl", false); }

bool SettingsStorage::load(Settings &settings) {
  Settings defaults;
  settings = defaults;

  const bool hadFermName = _prefs.isKey("fermName");
  const bool hadLegacyVersion = _prefs.isKey("version");
  if (!hadFermName && !hadLegacyVersion) {
    sanitizeSettings(settings);
    return false;
  }

  bool needsRewrite = hadLegacyVersion;
  if (hadLegacyVersion) {
    _prefs.remove("version");
  }

  // Single additive load path. Every field is read by name with an explicit
  // default. Missing keys (older stores) simply receive their default; no
  // schema version or per-key migration is used or needed. Add new settings by
  // adding a getX(...) read here and the matching putX in saveNow. Never
  // reinterpret the meaning or encoding of an existing key.
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
  // Live setpoint. If absent, fall back to the active profile's preset.
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
    loadProgram(_prefs, i, settings.programs[i], needsRewrite);
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

  // One-shot rewrite after legacy version removal or program blob migration.
  if (needsRewrite) {
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
    saveProgram(_prefs, i, copy.programs[i]);
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