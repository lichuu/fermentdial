#include "control.h"

namespace ferm {

TemperatureSensor::TemperatureSensor(uint8_t dataPin)
    : _oneWire(dataPin), _sensors(&_oneWire) {}

void TemperatureSensor::begin(uint32_t nowMs) {
#if FERM_DEMO_SENSOR
  _valid = true;
  _temperatureC = DEFAULT_TARGET_C;
  _rawTemperatureC = _temperatureC;
  _lastRequestMs = nowMs;
#else
  // DS18B20 VCC must be 3.3V because its pull-up resistor connects DATA to VCC.
  _sensors.begin();
  _sensors.setResolution(12);
  _sensors.setWaitForConversion(false);
  _sensors.requestTemperatures();
  _lastRequestMs = nowMs;
#endif
}

void TemperatureSensor::update(uint32_t nowMs, const Settings &settings) {
  if (nowMs - _lastRequestMs < TEMP_READ_INTERVAL_MS) {
    return;
  }

#if FERM_DEMO_SENSOR
  // Demo sensor mode is compile-time only. It exists for UI/control bench work
  // without a DS18B20 attached and must be disabled for real fermentation use.
  constexpr float periodMs = 30000.0f;
  float phase =
      static_cast<float>(nowMs % static_cast<uint32_t>(periodMs)) / periodMs;
  _temperatureC = activeTargetC(settings) +
                  (sinf(phase * TWO_PI) * deltaFToC(1.2f)) +
                  settings.tempOffsetC;
  _rawTemperatureC = _temperatureC;
  _valid = true;
  _lastRequestMs = nowMs;
  return;
#else
  _rawTemperatureC = _sensors.getTempCByIndex(0);
  _lastRequestMs = nowMs;
  _sensors.requestTemperatures();

  if (isnan(_rawTemperatureC) || _rawTemperatureC == DEVICE_DISCONNECTED_C) {
    if (++_badReadCount >= SENSOR_FAULT_READ_COUNT) {
      _valid = false;
      _temperatureC = NAN;
    }
    return;
  }

  if (_rawTemperatureC < MIN_VALID_TEMP_C ||
      _rawTemperatureC > MAX_VALID_TEMP_C) {
    if (++_badReadCount >= SENSOR_FAULT_READ_COUNT) {
      _valid = false;
      _temperatureC = NAN;
    }
    return;
  }

  if (_haveAcceptedReading &&
      fabsf(_rawTemperatureC - _lastAcceptedRawC) > MAX_SENSOR_JUMP_C) {
    if (++_badReadCount >= SENSOR_FAULT_READ_COUNT) {
      _valid = false;
      _temperatureC = NAN;
    }
    return;
  }

  float calibratedC = _rawTemperatureC + settings.tempOffsetC;
  calibratedC = clampFloat(calibratedC, MIN_VALID_TEMP_C, MAX_VALID_TEMP_C);
  _badReadCount = 0;
  _haveAcceptedReading = true;
  _lastAcceptedRawC = _rawTemperatureC;
  _valid = true;
  _temperatureC = calibratedC;
#endif
}

void FermentationController::begin(uint32_t nowMs) {
  pinMode(PIN_HEATER_TRIGGER, OUTPUT);
  pinMode(PIN_PUMP_TRIGGER, OUTPUT);

  _lastPumpOffMs = nowMs;
  _lastPumpOnMs = nowMs;

  Settings bootSettings;
  bootSettings.mode = UserMode::Off;

  // Boot defaults force both outputs OFF before display, storage, or sensor
  // setup runs.
  applyOutputs(nowMs, bootSettings, false, false, false, FaultCode::None,
               RuntimeState::Boot);
}

void FermentationController::update(uint32_t nowMs, const Settings &settings,
                                    bool sensorValid, float tempC) {
  if (_outputTest != OutputTestKind::None) {
    if (!sensorValid) {
      _outputTest = OutputTestKind::None;
      applyOutputs(nowMs, settings, false, false, true, FaultCode::Sensor,
                   RuntimeState::Fault);
      return;
    }
    if (settings.mode == UserMode::Off || nowMs >= _outputTestEndsMs) {
      _outputTest = OutputTestKind::None;
      applyOutputs(nowMs, settings, false, false, false, FaultCode::None,
                   RuntimeState::Idle);
      return;
    }

    bool heaterTest = _outputTest == OutputTestKind::Heater;
    bool pumpTest = _outputTest == OutputTestKind::Pump;
    applyOutputs(nowMs, settings, heaterTest, pumpTest, false, FaultCode::None,
                 heaterTest ? RuntimeState::Heating : RuntimeState::Cooling);
    return;
  }

  if (!sensorValid) {
    // Sensor fault forces both outputs OFF.
    applyOutputs(nowMs, settings, false, false, true, FaultCode::Sensor,
                 RuntimeState::Fault);
    return;
  }

  if (settings.mode == UserMode::Off) {
    applyOutputs(nowMs, settings, false, false, false, FaultCode::None,
                 RuntimeState::Off);
    return;
  }

  bool heatingAllowed =
      settings.mode == UserMode::Auto || settings.mode == UserMode::HeatOnly;
  bool coolingAllowed =
      settings.mode == UserMode::Auto || settings.mode == UserMode::CoolOnly;
  bool heaterRequested = false;
  bool pumpRequested = false;

  const float targetC = activeTargetC(settings);
  const float heatStartThreshold = targetC - settings.heatOnDeltaC;
  const float heatStopThreshold = targetC - settings.holdDeltaC;
  const float coolStartThreshold = targetC + settings.coolOnDeltaC;
  const float coolStopThreshold = targetC + settings.holdDeltaC;

  if (coolingAllowed && pumpMinRunActive(nowMs, settings)) {
    pumpRequested = true;
  } else if (_pumpOn && coolingAllowed && tempC > coolStopThreshold) {
    pumpRequested = true;
  } else if (_heaterOn && heatingAllowed && tempC < heatStopThreshold) {
    heaterRequested = true;
  } else if (tempC > coolStartThreshold && coolingAllowed) {
    pumpRequested = _pumpOn || pumpMinOffSatisfied(nowMs, settings);
  } else if (tempC < heatStartThreshold && heatingAllowed) {
    heaterRequested = true;
  }

  RuntimeState requestedState = RuntimeState::Idle;
  if (heaterRequested) {
    requestedState = RuntimeState::Heating;
  } else if (pumpRequested) {
    requestedState = RuntimeState::Cooling;
  }

  applyOutputs(nowMs, settings, heaterRequested, pumpRequested, false,
               FaultCode::None, requestedState);
}

bool FermentationController::requestOutputTest(OutputTestKind kind,
                                               uint32_t nowMs,
                                               const Settings &settings,
                                               bool sensorValid) {
  if (kind == OutputTestKind::None || _outputTest != OutputTestKind::None) {
    return false;
  }
  if (!sensorValid || settings.mode == UserMode::Off) {
    return false;
  }

  _outputTest = kind;
  _outputTestEndsMs = nowMs + OUTPUT_TEST_MS;
  return true;
}

void FermentationController::cancelOutputTest(uint32_t nowMs,
                                              const Settings &settings) {
  _outputTest = OutputTestKind::None;
  applyOutputs(nowMs, settings, false, false, false, FaultCode::None,
               RuntimeState::Idle);
}

void FermentationController::forceOutputsOff(uint32_t nowMs) {
  _outputTest = OutputTestKind::None;
  Settings safeSettings;
  safeSettings.mode = UserMode::Off;
  applyOutputs(nowMs, safeSettings, false, false, false, FaultCode::None,
               RuntimeState::Off);
}

void FermentationController::applyOutputs(uint32_t nowMs,
                                          const Settings &settings,
                                          bool heaterRequested,
                                          bool pumpRequested, bool safetyFault,
                                          FaultCode safetyFaultCode,
                                          RuntimeState requestedState) {
  bool nextHeater = heaterRequested;
  bool nextPump = pumpRequested;
  RuntimeState nextState = requestedState;
  FaultCode nextFault = FaultCode::None;

  if (safetyFault) {
    nextHeater = false;
    nextPump = false;
    nextState = RuntimeState::Fault;
    nextFault = safetyFaultCode;
  } else if (settings.mode == UserMode::Off) {
    // OFF mode overrides everything.
    nextHeater = false;
    nextPump = false;
    nextState = RuntimeState::Off;
  } else if (heaterRequested && pumpRequested) {
    // Heat and pump must never run simultaneously.
    nextHeater = false;
    nextPump = false;
    nextState = RuntimeState::Fault;
    nextFault = FaultCode::Interlock;
  }

#if FERM_DEMO_SENSOR
  // Demo sensor mode is for UI work without probes or loads attached. Keep
  // physical outputs off so simulated temperatures cannot energize hardware.
  nextHeater = false;
  nextPump = false;
#endif

  if (_pumpOn != nextPump) {
    if (nextPump) {
      _lastPumpOnMs = nowMs;
    } else {
      _lastPumpOffMs = nowMs;
    }
  }

  _heaterOn = nextHeater;
  _pumpOn = nextPump;
  _runtimeState = nextState;
  _faultCode = nextFault;

  // MOSFET triggers are active HIGH.
  digitalWrite(PIN_HEATER_TRIGGER,
               _heaterOn == MOSFET_ACTIVE_HIGH ? HIGH : LOW);
  digitalWrite(PIN_PUMP_TRIGGER, _pumpOn == MOSFET_ACTIVE_HIGH ? HIGH : LOW);
}

bool FermentationController::pumpMinRunActive(uint32_t nowMs,
                                              const Settings &settings) const {
#if FERM_DEMO_SENSOR
  (void)nowMs;
  (void)settings;
  return false;
#else
  return _pumpOn &&
         (nowMs - _lastPumpOnMs) < (settings.pumpMinRunSeconds * 1000UL);
#endif
}

bool FermentationController::pumpMinOffSatisfied(
    uint32_t nowMs, const Settings &settings) const {
#if FERM_DEMO_SENSOR
  (void)nowMs;
  (void)settings;
  return true;
#else
  return (nowMs - _lastPumpOffMs) >= (settings.pumpMinOffSeconds * 1000UL);
#endif
}

} // namespace ferm
