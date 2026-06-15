#pragma once

#include <Arduino.h>
#include <math.h>

#ifndef FERM_ENABLE_NETWORK
#define FERM_ENABLE_NETWORK 0
#endif

#ifndef FERM_ENABLE_OTA
#define FERM_ENABLE_OTA 0
#endif

#ifndef FERM_DEMO_SENSOR
#define FERM_DEMO_SENSOR 0
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef FERM_WIFI_SSID
#define FERM_WIFI_SSID ""
#endif

#ifndef FERM_WIFI_PASSWORD
#define FERM_WIFI_PASSWORD ""
#endif

#ifndef FERM_WIFI_HOSTNAME
#define FERM_WIFI_HOSTNAME "ferment-dial"
#endif

namespace ferm {

constexpr const char *FIRMWARE_NAME = "Dial Ferment";
constexpr const char *FIRMWARE_VERSION = "0.1.0";
constexpr uint16_t SETTINGS_VERSION = 1;

// DS18B20 VCC must be 3.3V because its pull-up resistor connects DATA to VCC.
constexpr uint8_t PIN_DS18B20_DATA = 13;

// MOSFET triggers are active HIGH.
constexpr uint8_t PIN_HEATER_TRIGGER = 2;
constexpr uint8_t PIN_PUMP_TRIGGER = 1;
constexpr bool MOSFET_ACTIVE_HIGH = true;

constexpr uint32_t TEMP_READ_INTERVAL_MS = 1500;
constexpr uint32_t SETTINGS_SAVE_DEBOUNCE_MS = 5000;
constexpr uint32_t UI_REDRAW_INTERVAL_MS = 250;
constexpr uint32_t UI_TIMEOUT_MS = 30000;
constexpr uint32_t DISPLAY_DIM_MS = 60000;
constexpr uint32_t OUTPUT_TEST_MS = 5000;

constexpr float DEFAULT_TARGET_F = 68.0f;
constexpr float DEFAULT_HYSTERESIS_F = 0.5f;
constexpr uint32_t DEFAULT_PUMP_MIN_OFF_SECONDS = 120;
constexpr uint32_t DEFAULT_PUMP_MIN_RUN_SECONDS = 30;
constexpr float DEFAULT_TEMP_OFFSET_F = 0.0f;

constexpr float MIN_VALID_TEMP_F = 20.0f;
constexpr float MAX_VALID_TEMP_F = 120.0f;
constexpr float MIN_TARGET_F = 35.0f;
constexpr float MAX_TARGET_F = 95.0f;
constexpr float MIN_HYSTERESIS_F = 0.1f;
constexpr float MAX_HYSTERESIS_F = 5.0f;
constexpr float MIN_OFFSET_F = -10.0f;
constexpr float MAX_OFFSET_F = 10.0f;

enum class UserMode : uint8_t {
  Off = 0,
  Auto = 1,
  HeatOnly = 2,
  CoolOnly = 3,
};

enum class RuntimeState : uint8_t {
  Boot = 0,
  Off = 1,
  Idle = 2,
  Heating = 3,
  Cooling = 4,
  Fault = 5,
};

enum class FaultCode : uint8_t {
  None = 0,
  Sensor = 1,
  Interlock = 2,
};

enum class OutputTestKind : uint8_t {
  None = 0,
  Heater = 1,
  Pump = 2,
};

struct Settings {
  uint16_t version = SETTINGS_VERSION;
  float targetF = DEFAULT_TARGET_F;
  float hysteresisF = DEFAULT_HYSTERESIS_F;
  UserMode mode = UserMode::Off;
  uint32_t pumpMinOffSeconds = DEFAULT_PUMP_MIN_OFF_SECONDS;
  uint32_t pumpMinRunSeconds = DEFAULT_PUMP_MIN_RUN_SECONDS;
  float tempOffsetF = DEFAULT_TEMP_OFFSET_F;
  bool unitsFahrenheit = true;
};

inline float cToF(float tempC) {
  return (tempC * 9.0f / 5.0f) + 32.0f;
}

inline float fToC(float tempF) {
  return (tempF - 32.0f) * 5.0f / 9.0f;
}

inline float clampFloat(float value, float minimum, float maximum) {
  if (isnan(value)) {
    return minimum;
  }
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

inline uint32_t clampU32(uint32_t value, uint32_t minimum, uint32_t maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

inline const char *modeText(UserMode mode) {
  switch (mode) {
    case UserMode::Off:
      return "OFF";
    case UserMode::Auto:
      return "AUTO";
    case UserMode::HeatOnly:
      return "HEAT";
    case UserMode::CoolOnly:
      return "COOL";
    default:
      return "OFF";
  }
}

inline const char *modeTopicText(UserMode mode) {
  switch (mode) {
    case UserMode::Off:
      return "OFF";
    case UserMode::Auto:
      return "AUTO";
    case UserMode::HeatOnly:
      return "HEAT_ONLY";
    case UserMode::CoolOnly:
      return "COOL_ONLY";
    default:
      return "OFF";
  }
}

inline const char *stateText(RuntimeState state) {
  switch (state) {
    case RuntimeState::Boot:
      return "BOOT";
    case RuntimeState::Off:
      return "OFF";
    case RuntimeState::Idle:
      return "IDLE";
    case RuntimeState::Heating:
      return "HEATING";
    case RuntimeState::Cooling:
      return "COOLING";
    case RuntimeState::Fault:
      return "FAULT";
    default:
      return "FAULT";
  }
}

inline const char *faultText(FaultCode fault) {
  switch (fault) {
    case FaultCode::None:
      return "NONE";
    case FaultCode::Sensor:
      return "SENSOR FAULT";
    case FaultCode::Interlock:
      return "INTERLOCK FAULT";
    default:
      return "FAULT";
  }
}

inline UserMode nextMode(UserMode mode) {
  switch (mode) {
    case UserMode::Off:
      return UserMode::Auto;
    case UserMode::Auto:
      return UserMode::HeatOnly;
    case UserMode::HeatOnly:
      return UserMode::CoolOnly;
    case UserMode::CoolOnly:
    default:
      return UserMode::Off;
  }
}

inline void sanitizeSettings(Settings &settings) {
  settings.version = SETTINGS_VERSION;
  settings.targetF = clampFloat(settings.targetF, MIN_TARGET_F, MAX_TARGET_F);
  settings.hysteresisF = clampFloat(settings.hysteresisF, MIN_HYSTERESIS_F, MAX_HYSTERESIS_F);
  settings.pumpMinOffSeconds = clampU32(settings.pumpMinOffSeconds, 0, 1800);
  settings.pumpMinRunSeconds = clampU32(settings.pumpMinRunSeconds, 0, 600);
  settings.tempOffsetF = clampFloat(settings.tempOffsetF, MIN_OFFSET_F, MAX_OFFSET_F);

  uint8_t modeValue = static_cast<uint8_t>(settings.mode);
  if (modeValue > static_cast<uint8_t>(UserMode::CoolOnly)) {
    settings.mode = UserMode::Off;
  }
}

}  // namespace ferm
