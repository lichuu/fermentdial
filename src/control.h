#pragma once

#include <DallasTemperature.h>
#include <OneWire.h>

#include "config.h"

namespace ferm {

class TemperatureSensor {
public:
  explicit TemperatureSensor(uint8_t dataPin = PIN_DS18B20_DATA);

  void begin(uint32_t nowMs);
  void update(uint32_t nowMs, const Settings &settings);

  bool isValid() const { return _valid; }
  float temperatureC() const { return _temperatureC; }
  float rawTemperatureC() const { return _rawTemperatureC; }
  bool demoMode() const { return FERM_DEMO_SENSOR; }

private:
  OneWire _oneWire;
  DallasTemperature _sensors;
  uint32_t _lastRequestMs = 0;
  uint8_t _badReadCount = 0;
  bool _haveAcceptedReading = false;
  bool _valid = false;
  float _temperatureC = NAN;
  float _rawTemperatureC = NAN;
  float _lastAcceptedRawC = NAN;
};

class FermentationController {
public:
  void begin(uint32_t nowMs);
  void update(uint32_t nowMs, const Settings &settings, bool sensorValid,
              float tempC);

  bool requestOutputTest(OutputTestKind kind, uint32_t nowMs,
                         const Settings &settings, bool sensorValid);
  void cancelOutputTest(uint32_t nowMs, const Settings &settings);
  void forceOutputsOff(uint32_t nowMs);

  RuntimeState runtimeState() const { return _runtimeState; }
  FaultCode faultCode() const { return _faultCode; }
  bool heaterOn() const { return _heaterOn; }
  bool pumpOn() const { return _pumpOn; }
  bool outputTestActive() const { return _outputTest != OutputTestKind::None; }
  OutputTestKind outputTestKind() const { return _outputTest; }
  uint32_t pumpOffElapsedMs(uint32_t nowMs) const {
    return nowMs - _lastPumpOffMs;
  }
  uint32_t pumpRunElapsedMs(uint32_t nowMs) const {
    return nowMs - _lastPumpOnMs;
  }

private:
  void applyOutputs(uint32_t nowMs, const Settings &settings,
                    bool heaterRequested, bool pumpRequested, bool safetyFault,
                    FaultCode safetyFaultCode, RuntimeState requestedState);

  bool pumpMinRunActive(uint32_t nowMs, const Settings &settings) const;
  bool pumpMinOffSatisfied(uint32_t nowMs, const Settings &settings) const;

  RuntimeState _runtimeState = RuntimeState::Boot;
  FaultCode _faultCode = FaultCode::None;
  bool _heaterOn = false;
  bool _pumpOn = false;
  uint32_t _lastPumpOnMs = 0;
  uint32_t _lastPumpOffMs = 0;
  OutputTestKind _outputTest = OutputTestKind::None;
  uint32_t _outputTestEndsMs = 0;
};

} // namespace ferm
