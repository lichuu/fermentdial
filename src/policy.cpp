#include "policy.h"

#include "control.h"
#include "events.h"
#include "time_sync.h"

namespace ferm {

extern Settings settings;
extern TemperatureSensor temperatureSensor;
extern FermentationController controller;
extern EventLog eventLog;
extern uint32_t lastDiacetylRestSaveMs;
extern uint32_t lastProgramSaveMs;

namespace {

constexpr uint32_t LONG_RUNTIME_MS = 4UL * 60UL * 60UL * 1000UL;
constexpr uint32_t NOT_REACHING_MS = 30UL * 60UL * 1000UL;
constexpr float NOT_REACHING_DELTA_C = deltaFToC(5.0f);

uint32_t lastDiacetylRestTickMs = 0;
uint32_t lastGradualCrashStepMs = 0;
uint32_t lastProgramTickMs = 0;
bool previousDiacetylRestActive = false;
bool previousGradualCrashActive = false;
bool previousProgramActive = false;

FaultCode prevFaultCode = FaultCode::None;
bool prevHydroStale = false;
bool prevProgramActiveAlert = false;
uint8_t prevProgramStepIndexAlert = 0;
uint32_t heaterOnSinceMs = 0;
uint32_t pumpOnSinceMs = 0;
bool heaterLongAlerted = false;
bool pumpLongAlerted = false;
uint32_t deviationSinceMs = 0;
bool notReachingAlerted = false;

}  // namespace

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

bool updateProgramRunner(uint32_t nowMs, const HydrometerReading &hydro) {
  if (!settings.programActive) {
    previousProgramActive = false;
    lastProgramTickMs = nowMs;
    return false;
  }

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
      advance = hydroUsable && hydro.stableSeconds >= stableTargetSeconds;
    }
    break;
  case StepExit::Manual:
  default:
    break;
  }
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
      settings.programStepStartTargetC = settings.liveTargetC;
    }
    return true;
  }

  return changed;
}

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
  row += ',';
  if (hydro.valid && !hydro.stale && !isnan(hydro.temperatureC)) {
    row += String(hydro.temperatureC, 2);
  }
  row += '\n';
  return row;
}

}  // namespace ferm