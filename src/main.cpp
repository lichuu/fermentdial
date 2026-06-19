#include <Arduino.h>
#include <LittleFS.h>

#include "config.h"
#include "control.h"
#include "events.h"
#include "history.h"
#include "hydrometer.h"
#include "network.h"
#include "storage.h"
#include "time_sync.h"
#include "ui.h"

using namespace ferm;

// Alert thresholds.
constexpr uint32_t LONG_RUNTIME_MS = 4UL * 60UL * 60UL * 1000UL;  // 4 hours
constexpr uint32_t NOT_REACHING_MS = 30UL * 60UL * 1000UL;        // 30 minutes
constexpr float NOT_REACHING_DELTA_C = deltaFToC(5.0f);           // 5 F off target
constexpr uint32_t EVENT_FLUSH_INTERVAL_MS = 15000;

Settings settings;
SettingsStorage storage;
TemperatureSensor temperatureSensor;
FermentationController controller;
HydrometerManager hydrometer;
NetworkManager network;
DisplayUI ui;

uint32_t lastSerialLogMs = 0;
uint32_t lastDiacetylRestTickMs = 0;
uint32_t lastDiacetylRestSaveMs = 0;
uint32_t lastHydrometerSaveMs = 0;
uint32_t lastGradualCrashStepMs = 0;
uint32_t lastProgramTickMs = 0;
uint32_t lastProgramSaveMs = 0;
bool previousDiacetylRestActive = false;
bool previousGradualCrashActive = false;
bool previousProgramActive = false;

EventLog eventLog;
HistoryLog historyLog;
// Alert edge-detection state.
FaultCode prevFaultCode = FaultCode::None;
bool prevHydroStale = false;
bool prevProgramActiveAlert = false;
uint8_t prevProgramStepIndexAlert = 0;
uint32_t heaterOnSinceMs = 0;  // 0 when off
uint32_t pumpOnSinceMs = 0;
bool heaterLongAlerted = false;
bool pumpLongAlerted = false;
uint32_t deviationSinceMs = 0;  // 0 when within band
bool notReachingAlerted = false;
uint32_t lastEventFlushMs = 0;

void prepareForFirmwareUpdate() {
  // Force the relays off for the flash itself, but keep the saved mode/profile
  // so the controller resumes what was running once it reboots (resilient to
  // both OTA updates and power loss). Flush current settings first.
  Serial.println(F("Firmware update requested; forcing outputs OFF"));
  controller.forceOutputsOff(millis());
  storage.saveNow(settings);
  eventLog.flush();
}

void logStatus(uint32_t nowMs) {
  if (nowMs - lastSerialLogMs < 2000) {
    return;
  }
  lastSerialLogMs = nowMs;

  Serial.print(F("tempC="));
  if (temperatureSensor.isValid()) {
    Serial.print(temperatureSensor.temperatureC(), 1);
  } else {
    Serial.print(F("invalid"));
  }
  Serial.print(F(" targetC="));
  Serial.print(currentTargetC(settings), 1);
  Serial.print(F(" profile="));
  Serial.print(activeProfile(settings).name);
  if (settings.diacetylRestActive) {
    Serial.print(F(" dRestRemainingS="));
    Serial.print(settings.diacetylRestRemainingSeconds);
  }
  Serial.print(F(" mode="));
  Serial.print(modeTopicText(settings.mode));
  Serial.print(F(" state="));
  Serial.print(stateText(controller.runtimeState()));
  Serial.print(F(" fault="));
  Serial.print(faultText(controller.faultCode()));
  Serial.print(F(" heater="));
  Serial.print(controller.heaterOn() ? F("ON") : F("OFF"));
  Serial.print(F(" pump="));
  Serial.println(controller.pumpOn() ? F("ON") : F("OFF"));
}

bool updateDiacetylRest(uint32_t nowMs) {
  if (!settings.diacetylRestActive) {
    previousDiacetylRestActive = false;
    lastDiacetylRestTickMs = nowMs;
    return false;
  }

  if (!previousDiacetylRestActive) {
    previousDiacetylRestActive = true;
    lastDiacetylRestTickMs = nowMs;
    lastDiacetylRestSaveMs = nowMs;
    return false;
  }

  const uint32_t elapsedSeconds = (nowMs - lastDiacetylRestTickMs) / 1000UL;
  if (elapsedSeconds == 0) {
    return false;
  }
  lastDiacetylRestTickMs += elapsedSeconds * 1000UL;

  if (elapsedSeconds >= settings.diacetylRestRemainingSeconds) {
    completeDiacetylRest(settings);
    previousDiacetylRestActive = false;
    return true;
  }

  settings.diacetylRestRemainingSeconds -= elapsedSeconds;
  return false;
}

bool updateGradualCrash(uint32_t nowMs) {
  const bool active =
      settings.gradualCrashEnabled && !settings.diacetylRestActive &&
      activeProfileIndex(settings) == static_cast<uint8_t>(ProfileSlot::Crash);
  if (!active) {
    previousGradualCrashActive = false;
    lastGradualCrashStepMs = nowMs;
    return false;
  }

  const float targetC = activeProfile(settings).targetC;
  if (settings.liveTargetC <= targetC) {
    if (settings.liveTargetC < targetC) {
      settings.liveTargetC = targetC;
      return true;
    }
    previousGradualCrashActive = true;
    return false;
  }

  const uint32_t stepIntervalMs =
      settings.gradualCrashStepIntervalHours * 60UL * 60UL * 1000UL;
  uint32_t steps = 0;
  if (!previousGradualCrashActive) {
    previousGradualCrashActive = true;
    steps = 1;
  } else if (nowMs - lastGradualCrashStepMs >= stepIntervalMs) {
    steps = (nowMs - lastGradualCrashStepMs) / stepIntervalMs;
  }

  if (steps == 0) {
    return false;
  }

  lastGradualCrashStepMs = nowMs;
  float nextTargetC = settings.liveTargetC - (settings.gradualCrashStepC * steps);
  if (nextTargetC < targetC) {
    nextTargetC = targetC;
  }
  settings.liveTargetC =
      snapTempC(clampFloat(nextTargetC, MIN_TARGET_C, MAX_TARGET_C),
                settings.unitsFahrenheit);
  if (settings.liveTargetC < targetC) {
    settings.liveTargetC = targetC;
  }
  return true;
}

// Walks the active program: drives the live setpoint for the current step and
// advances when the step's exit condition is met. Returns true when something
// changed that warrants a settings checkpoint. Mirrors updateDiacetylRest's
// millis-based tick so elapsed time survives reboot (outage time is not added).
bool updateProgramRunner(uint32_t nowMs, const HydrometerReading &hydro) {
  if (!settings.programActive) {
    previousProgramActive = false;
    lastProgramTickMs = nowMs;
    return false;
  }

  // Guard against a corrupt run index or an empty/exhausted program.
  if (settings.programRunIndex >= PROGRAM_SLOT_COUNT) {
    stopProgram(settings);
    previousProgramActive = false;
    return true;
  }
  ProgramSettings &program = settings.programs[settings.programRunIndex];
  if (program.stepCount == 0 ||
      settings.programStepIndex >= program.stepCount) {
    stopProgram(settings);
    previousProgramActive = false;
    return true;
  }

  if (!previousProgramActive) {
    previousProgramActive = true;
    lastProgramTickMs = nowMs;
    lastProgramSaveMs = nowMs;
  }

  const uint32_t elapsedSeconds = (nowMs - lastProgramTickMs) / 1000UL;
  if (elapsedSeconds > 0) {
    lastProgramTickMs += elapsedSeconds * 1000UL;
    settings.programStepElapsedSeconds += elapsedSeconds;
  }

  const ProfileStep &step = program.steps[settings.programStepIndex];

  // Drive the live setpoint to this step's effective target.
  const float stepTargetC = snapTempC(
      clampFloat(computeStepTargetC(step, settings.programStepElapsedSeconds,
                                    settings.programStepStartTargetC),
                 MIN_TARGET_C, MAX_TARGET_C),
      settings.unitsFahrenheit);
  bool changed = false;
  if (fabsf(stepTargetC - settings.liveTargetC) > 0.01f) {
    settings.liveTargetC = stepTargetC;
    changed = true;
  }

  // Evaluate the step's exit condition. Gravity-based exits never fire on a
  // stale or invalid hydrometer reading.
  bool advance = false;
  const bool hydroUsable = hydro.valid && !hydro.stale;
  const uint32_t stableTargetSeconds =
      static_cast<uint32_t>(step.stableHours) * 3600UL;
  switch (effectiveStepExit(step)) {
  case StepExit::Time:
    advance = settings.programStepElapsedSeconds >= step.durationSeconds;
    break;
  case StepExit::GravityBelow:
    advance = hydroUsable && gravityIsValid(hydro.gravity) &&
              hydro.gravity <= step.gravityThreshold;
    break;
  case StepExit::GravityStable:
    advance = hydroUsable && hydro.stableSeconds >= stableTargetSeconds;
    break;
  case StepExit::VelocityBelow:
    if (hydroUsable && hydro.gravityVelocityValid) {
      advance = fabsf(hydro.gravityVelocity) <= step.gravityThreshold;
    } else {
      // Tilt has no native velocity; fall back to the stability window.
      advance = hydroUsable && hydro.stableSeconds >= stableTargetSeconds;
    }
    break;
  case StepExit::Manual:
  default:
    break;
  }
  // A manual advance request always pushes the program forward, regardless of
  // the step's own exit condition (skip button, or a stuck/stale hydrometer).
  if (settings.programManualAdvance) {
    advance = true;
  }
  settings.programManualAdvance = false;

  if (advance) {
    settings.programStepIndex++;
    settings.programStepElapsedSeconds = 0;
    if (settings.programStepIndex >= program.stepCount) {
      stopProgram(settings);
      previousProgramActive = false;
    } else {
      // Next ramp starts from wherever the setpoint is now.
      settings.programStepStartTargetC = settings.liveTargetC;
    }
    return true;
  }

  return changed;
}

// Detects notable state transitions and appends them to the event log. Edge- and
// latch-based so each condition logs once per occurrence, not every loop.
void evaluateAlerts(uint32_t nowMs, const HydrometerReading &hydro) {
  const Timestamp ts = nowEpochOrUptime(nowMs);

  const FaultCode fault = controller.faultCode();
  if (fault != prevFaultCode) {
    if (fault == FaultCode::Sensor) {
      eventLog.add(EventType::SensorFault,
                   F("Sensor fault - outputs forced OFF"), ts);
    } else if (fault == FaultCode::Interlock) {
      eventLog.add(EventType::InterlockFault,
                   F("Output interlock fault - outputs forced OFF"), ts);
    }
    prevFaultCode = fault;
  }

  const bool stale = hydro.selected && hydro.stale;
  if (stale && !prevHydroStale) {
    const String label = hydro.label.length() ? hydro.label : String("Hydrometer");
    eventLog.add(EventType::HydrometerStale, label + F(" stopped reporting"), ts);
  }
  prevHydroStale = stale;

  if (settings.programActive) {
    if (!prevProgramActiveAlert ||
        settings.programStepIndex != prevProgramStepIndexAlert) {
      const uint8_t idx = settings.programRunIndex < PROGRAM_SLOT_COUNT
                              ? settings.programRunIndex
                              : 0;
      const String msg = String(F("Program step ")) +
                         String(settings.programStepIndex + 1) + "/" +
                         String(settings.programs[idx].stepCount);
      eventLog.add(EventType::ProgramStep, msg, ts);
    }
  } else if (prevProgramActiveAlert) {
    eventLog.add(EventType::ProgramDone, F("Program finished"), ts);
  }
  prevProgramActiveAlert = settings.programActive;
  prevProgramStepIndexAlert = settings.programStepIndex;

  if (controller.heaterOn()) {
    if (heaterOnSinceMs == 0) {
      heaterOnSinceMs = nowMs;
    }
    if (!heaterLongAlerted && nowMs - heaterOnSinceMs >= LONG_RUNTIME_MS) {
      eventLog.add(EventType::LongRuntime,
                   F("Heater on over 4h continuously"), ts);
      heaterLongAlerted = true;
    }
  } else {
    heaterOnSinceMs = 0;
    heaterLongAlerted = false;
  }
  if (controller.pumpOn()) {
    if (pumpOnSinceMs == 0) {
      pumpOnSinceMs = nowMs;
    }
    if (!pumpLongAlerted && nowMs - pumpOnSinceMs >= LONG_RUNTIME_MS) {
      eventLog.add(EventType::LongRuntime,
                   F("Cooling pump on over 4h continuously"), ts);
      pumpLongAlerted = true;
    }
  } else {
    pumpOnSinceMs = 0;
    pumpLongAlerted = false;
  }

  const bool regulating =
      settings.mode != UserMode::Off && temperatureSensor.isValid();
  if (regulating &&
      fabsf(temperatureSensor.temperatureC() - currentTargetC(settings)) >
          NOT_REACHING_DELTA_C) {
    if (deviationSinceMs == 0) {
      deviationSinceMs = nowMs;
    }
    if (!notReachingAlerted && nowMs - deviationSinceMs >= NOT_REACHING_MS) {
      eventLog.add(EventType::NotReachingTarget,
                   F("Temperature off target >5F for 30 min"), ts);
      notReachingAlerted = true;
    }
  } else {
    deviationSinceMs = 0;
    notReachingAlerted = false;
  }
}

// Builds one CSV history row from current controller/sensor/program state.
// Temperatures are Celsius (storage unit); empty fields mean "no reading".
String buildHistoryRow(uint32_t nowMs, const HydrometerReading &hydro) {
  const Timestamp ts = nowEpochOrUptime(nowMs);
  String row;
  row += String(ts.seconds);
  row += ',';
  row += ts.wallClock ? '1' : '0';
  row += ',';
  if (temperatureSensor.isValid()) {
    row += String(temperatureSensor.temperatureC(), 2);
  }
  row += ',';
  row += String(currentTargetC(settings), 2);
  row += ',';
  if (hydro.valid && !isnan(hydro.gravity)) {
    row += String(hydro.gravity, 4);
  }
  row += ',';
  if (!isnan(hydro.abv)) {
    row += String(hydro.abv, 2);
  }
  row += ',';
  row += controller.heaterOn() ? '1' : '0';
  row += ',';
  row += controller.pumpOn() ? '1' : '0';
  row += ',';
  row += stateText(controller.runtimeState());
  row += ',';
  if (settings.programActive) {
    row += String(settings.programStepIndex + 1);
  }
  row += '\n';
  return row;
}

void setup() {
  const uint32_t nowMs = millis();

  // Boot defaults force both outputs OFF immediately.
  controller.begin(nowMs);

  Serial.begin(115200);
  Serial.println();
  Serial.print(FIRMWARE_NAME);
  Serial.print(F(" v"));
  Serial.println(FIRMWARE_VERSION);
#if FERM_DEMO_SENSOR
  Serial.println(
      F("DEMO SENSOR MODE ENABLED - do not use for real fermentation control"));
#endif

  ui.begin();
  delay(BOOT_SPLASH_MS);

  storage.begin();
  bool loaded = storage.load(settings);
  sanitizeSettings(settings);
  Serial.println(loaded ? F("Loaded saved settings")
                        : F("Using safe defaults"));

  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS mount failed; event log not persisted"));
  }
  eventLog.begin();
  historyLog.begin();

  temperatureSensor.begin(millis());
  hydrometer.begin();
  network.setFirmwareUpdateSafetyCallback(prepareForFirmwareUpdate);
  network.begin(settings, hydrometer);
  network.setEventLog(&eventLog);
  eventLog.add(EventType::Info, String(F("Booted ")) + FIRMWARE_VERSION,
               nowEpochOrUptime(millis()));
}

void loop() {
  uint32_t nowMs = millis();

  temperatureSensor.update(nowMs, settings);
  const bool hydrometerChanged = hydrometer.update(nowMs, settings);
  const HydrometerReading selectedHydrometer =
      hydrometer.selectedReading(settings, nowMs);
  network.update(nowMs, settings);
  const bool diacetylRestChanged = updateDiacetylRest(nowMs);
  const bool gradualCrashChanged = updateGradualCrash(nowMs);
  const bool programChanged = updateProgramRunner(nowMs, selectedHydrometer);

  controller.update(nowMs, settings, temperatureSensor.isValid(),
                    temperatureSensor.temperatureC());

  evaluateAlerts(nowMs, selectedHydrometer);
  if (eventLog.dirty() && nowMs - lastEventFlushMs >= EVENT_FLUSH_INTERVAL_MS) {
    lastEventFlushMs = nowMs;
    eventLog.flush();
  }
  if (settings.historyLoggingEnabled && historyLog.due(nowMs)) {
    historyLog.markSampled(nowMs);
    historyLog.append(buildHistoryRow(nowMs, selectedHydrometer));
  }

  UiModel model;
  model.tempValid = temperatureSensor.isValid();
  model.tempC = temperatureSensor.temperatureC();
  model.runtimeState = controller.runtimeState();
  model.faultCode = controller.faultCode();
  model.heaterOn = controller.heaterOn();
  model.pumpOn = controller.pumpOn();
  model.outputTestActive = controller.outputTestActive();
  model.outputTestKind = controller.outputTestKind();
  model.demoSensor = temperatureSensor.demoMode();
  model.hydrometer = selectedHydrometer;
  model.hydrometerDeviceCount = hydrometer.deviceCount();
  for (uint8_t i = 0; i < model.hydrometerDeviceCount; i++) {
    model.hydrometerDevices[i] = hydrometer.device(i);
  }
  model.network = network.snapshot();

  ui.update(nowMs, settings, model);

  bool checkpointDiacetylRest = false;
  if (settings.diacetylRestActive &&
      nowMs - lastDiacetylRestSaveMs >= DIACETYL_REST_SAVE_INTERVAL_MS) {
    lastDiacetylRestSaveMs = nowMs;
    checkpointDiacetylRest = true;
  }

  bool checkpointProgram = false;
  if (settings.programActive &&
      nowMs - lastProgramSaveMs >= PROGRAM_SAVE_INTERVAL_MS) {
    lastProgramSaveMs = nowMs;
    checkpointProgram = true;
  }

  bool checkpointHydrometer = false;
  if (selectedHydrometer.valid && !selectedHydrometer.stale &&
      nowMs - lastHydrometerSaveMs >= HYDROMETER_SAVE_INTERVAL_MS) {
    lastHydrometerSaveMs = nowMs;
    settings.hydrometerStableSeconds = selectedHydrometer.stableSeconds;
    settings.hydrometerStableGravity = selectedHydrometer.gravity;
    if (!gravityIsValid(settings.hydrometerOriginalGravity)) {
      settings.hydrometerOriginalGravity = selectedHydrometer.gravity;
    }
    hydrometer.markStableCheckpoint(nowMs);
    checkpointHydrometer = true;
  }

  if (ui.consumeSaveRequested() || network.consumeSettingsChanged() ||
      diacetylRestChanged || checkpointDiacetylRest || hydrometerChanged ||
      checkpointHydrometer || gradualCrashChanged || programChanged ||
      checkpointProgram) {
    sanitizeSettings(settings);
    storage.scheduleSave(nowMs);
    controller.update(nowMs, settings, temperatureSensor.isValid(),
                      temperatureSensor.temperatureC());
  }

  if (ui.consumeWifiSetupRequested()) {
    network.requestSetupPortal();
  }

  OutputTestKind testRequest = ui.consumeOutputTestRequest();
  if (testRequest != OutputTestKind::None) {
    bool accepted = controller.requestOutputTest(testRequest, nowMs, settings,
                                                 temperatureSensor.isValid());
    if (!accepted) {
      ui.notifyOutputTestRejected();
    }
  }

  network.publishState(nowMs, settings, temperatureSensor, controller,
                       hydrometer);
  storage.loop(nowMs, settings);
  logStatus(nowMs);
}
