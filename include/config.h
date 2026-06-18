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

#ifndef FERM_ENABLE_HYDROMETER_BLE
#define FERM_ENABLE_HYDROMETER_BLE 0
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

#ifndef FERM_WIFI_HOSTNAME_BASE
#ifdef FERM_WIFI_HOSTNAME
#define FERM_WIFI_HOSTNAME_BASE FERM_WIFI_HOSTNAME
#else
#define FERM_WIFI_HOSTNAME_BASE "fermentdial"
#endif
#endif

#ifndef FERM_GIT_SHA
#define FERM_GIT_SHA "unknown"
#endif

namespace ferm {

constexpr const char *FIRMWARE_NAME = "FermentDial";
constexpr const char *FIRMWARE_VERSION = "0.1.0";
constexpr const char *FIRMWARE_GIT_SHA = FERM_GIT_SHA;
constexpr uint16_t SETTINGS_VERSION = 14;
constexpr uint16_t SETTINGS_VERSION_FAHRENHEIT_STORAGE = 1;
constexpr uint16_t SETTINGS_VERSION_SINGLE_TARGET_STORAGE = 2;
constexpr uint16_t SETTINGS_VERSION_PROFILE_DEFAULTS_STORAGE = 4;
constexpr uint16_t SETTINGS_VERSION_FERMENTER_NAME_STORAGE = 5;
constexpr uint16_t SETTINGS_VERSION_PRELIVE_TARGET_STORAGE = 6;
constexpr uint16_t SETTINGS_VERSION_DIACETYL_REST_STORAGE = 7;
constexpr uint16_t SETTINGS_VERSION_HYDROMETER_STORAGE = 9;
constexpr uint16_t SETTINGS_VERSION_PRE_GRADUAL_CRASH_STORAGE = 10;
constexpr uint16_t SETTINGS_VERSION_PRE_PROFILE_WORKFLOW_STORAGE = 11;
constexpr uint16_t SETTINGS_VERSION_PRE_CONFIGURABLE_GRADUAL_CRASH_STORAGE = 12;

// DS18B20 VCC must be 3.3V because its pull-up resistor connects DATA to VCC.
constexpr uint8_t PIN_DS18B20_DATA = 13;

// MOSFET triggers are active HIGH.
constexpr uint8_t PIN_HEATER_TRIGGER = 2;
constexpr uint8_t PIN_PUMP_TRIGGER = 1;
constexpr bool MOSFET_ACTIVE_HIGH = true;

constexpr uint32_t TEMP_READ_INTERVAL_MS = 1500;
constexpr uint32_t BOOT_SPLASH_MS = 1600;
constexpr uint32_t SETTINGS_SAVE_DEBOUNCE_MS = 5000;
constexpr uint32_t UI_REDRAW_INTERVAL_MS = 100;
constexpr uint32_t UI_TIMEOUT_MS = 30000;
constexpr uint32_t DISPLAY_DIM_MS = 60000;
constexpr uint32_t OUTPUT_TEST_MS = 5000;
constexpr uint32_t DIACETYL_REST_SAVE_INTERVAL_MS = 60000;
constexpr uint32_t HYDROMETER_SAVE_INTERVAL_MS = 60000;
constexpr uint32_t DEFAULT_GRADUAL_CRASH_STEP_INTERVAL_HOURS = 12;
constexpr uint32_t MIN_GRADUAL_CRASH_STEP_INTERVAL_HOURS = 1;
constexpr uint32_t MAX_GRADUAL_CRASH_STEP_INTERVAL_HOURS = 72;
// How long the main screen stays in gold "SET TARGET" focus after the last
// encoder turn before reverting to the live temperature readout.
constexpr uint32_t SETPOINT_FOCUS_MS = 2500;
// While adjusting the setpoint on the main screen the new value is only a
// pending preview; if it isn't confirmed within this window it auto-cancels,
// so an accidental bump of the dial reverts on its own.
constexpr uint32_t SETPOINT_EDIT_TIMEOUT_MS = 8000;
constexpr uint8_t SENSOR_FAULT_READ_COUNT = 3;

constexpr float cToF(float tempC) { return (tempC * 9.0f / 5.0f) + 32.0f; }

constexpr float fToC(float tempF) { return (tempF - 32.0f) * 5.0f / 9.0f; }

constexpr float deltaFToC(float deltaF) { return deltaF * 5.0f / 9.0f; }

constexpr float deltaCToF(float deltaC) { return deltaC * 9.0f / 5.0f; }

// --- Display-unit helpers: single source of truth for C/F handling ----------
// Storage and control are always Celsius; the user merely views and edits in
// their chosen unit. These wrap the raw conversions so call sites read as
// intent ("to display", "from display") instead of repeating the
// `unitsFahrenheit ? cToF(x) : x` ternary everywhere.
inline float toDisplayTemp(float tempC, bool fahrenheit) {
  return fahrenheit ? cToF(tempC) : tempC;
}

inline float fromDisplayTemp(float tempDisplay, bool fahrenheit) {
  return fahrenheit ? fToC(tempDisplay) : tempDisplay;
}

inline float toDisplayDelta(float deltaC, bool fahrenheit) {
  return fahrenheit ? deltaCToF(deltaC) : deltaC;
}

inline float fromDisplayDelta(float deltaDisplay, bool fahrenheit) {
  return fahrenheit ? deltaFToC(deltaDisplay) : deltaDisplay;
}

inline const char *unitLabel(bool fahrenheit) { return fahrenheit ? "F" : "C"; }

// Resolution shown to and edited by the user (0.1 in the active unit).
constexpr float DISPLAY_TEMP_STEP = 0.1f;

// One on-screen tick (0.1 in the active unit) expressed in Celsius, so the dial
// nudges by the same visible step in either unit.
inline float displayStepC(bool fahrenheit) {
  return fromDisplayDelta(DISPLAY_TEMP_STEP, fahrenheit);
}

// Snap a stored Celsius value onto the display grid so Fahrenheit<->Celsius
// round-trips are stable and persisted setpoints never drift to ugly values
// (e.g. a stale 19.97 C migrates cleanly to 68.0 F == 20.0 C).
inline float snapTempC(float tempC, bool fahrenheit) {
  if (isnan(tempC)) {
    return tempC;
  }
  const float disp = toDisplayTemp(tempC, fahrenheit);
  return fromDisplayTemp(roundf(disp / DISPLAY_TEMP_STEP) * DISPLAY_TEMP_STEP,
                         fahrenheit);
}

inline float snapDeltaC(float deltaC, bool fahrenheit) {
  if (isnan(deltaC)) {
    return deltaC;
  }
  const float disp = toDisplayDelta(deltaC, fahrenheit);
  return fromDisplayDelta(roundf(disp / DISPLAY_TEMP_STEP) * DISPLAY_TEMP_STEP,
                          fahrenheit);
}

constexpr float DEFAULT_TARGET_F = 68.0f;
constexpr float DEFAULT_SOFT_CRASH_TARGET_F = 55.0f;
constexpr float DEFAULT_CRASH_TARGET_F = 37.0f;
constexpr float DEFAULT_LAGER_TARGET_F = 55.0f;
constexpr float DEFAULT_CUSTOM1_TARGET_F = 72.0f;
constexpr float DEFAULT_CUSTOM2_TARGET_F = 64.0f;
constexpr float DEFAULT_DIACETYL_REST_TARGET_F = 70.0f;
constexpr float DEFAULT_GRADUAL_CRASH_STEP_F = 5.0f;
constexpr float MIN_GRADUAL_CRASH_STEP_F = 0.5f;
constexpr float MAX_GRADUAL_CRASH_STEP_F = 20.0f;
constexpr uint32_t DEFAULT_DIACETYL_REST_DURATION_SECONDS = 48UL * 60UL * 60UL;
constexpr uint32_t MIN_DIACETYL_REST_DURATION_SECONDS = 24UL * 60UL * 60UL;
constexpr uint32_t MAX_DIACETYL_REST_DURATION_SECONDS = 96UL * 60UL * 60UL;
constexpr uint32_t DIACETYL_REST_DURATION_STEP_SECONDS = 24UL * 60UL * 60UL;
constexpr float DEFAULT_COOL_ON_DELTA_F = 0.5f;
constexpr float DEFAULT_HEAT_ON_DELTA_F = 5.0f;
constexpr float DEFAULT_HOLD_DELTA_F = 0.4f;
constexpr float DEFAULT_TARGET_C = fToC(DEFAULT_TARGET_F);
constexpr float DEFAULT_SOFT_CRASH_TARGET_C = fToC(DEFAULT_SOFT_CRASH_TARGET_F);
constexpr float DEFAULT_CRASH_TARGET_C = fToC(DEFAULT_CRASH_TARGET_F);
constexpr float DEFAULT_LAGER_TARGET_C = fToC(DEFAULT_LAGER_TARGET_F);
constexpr float DEFAULT_CUSTOM1_TARGET_C = fToC(DEFAULT_CUSTOM1_TARGET_F);
constexpr float DEFAULT_CUSTOM2_TARGET_C = fToC(DEFAULT_CUSTOM2_TARGET_F);
constexpr float DEFAULT_DIACETYL_REST_TARGET_C =
    fToC(DEFAULT_DIACETYL_REST_TARGET_F);
constexpr float DEFAULT_GRADUAL_CRASH_STEP_C = deltaFToC(DEFAULT_GRADUAL_CRASH_STEP_F);
constexpr float MIN_GRADUAL_CRASH_STEP_C = deltaFToC(MIN_GRADUAL_CRASH_STEP_F);
constexpr float MAX_GRADUAL_CRASH_STEP_C = deltaFToC(MAX_GRADUAL_CRASH_STEP_F);
constexpr float DEFAULT_COOL_ON_DELTA_C = deltaFToC(DEFAULT_COOL_ON_DELTA_F);
constexpr float DEFAULT_HEAT_ON_DELTA_C = deltaFToC(DEFAULT_HEAT_ON_DELTA_F);
constexpr float DEFAULT_HOLD_DELTA_C = deltaFToC(DEFAULT_HOLD_DELTA_F);
constexpr uint32_t DEFAULT_PUMP_MIN_OFF_SECONDS = 120;
constexpr uint32_t DEFAULT_PUMP_MIN_RUN_SECONDS = 30;
constexpr float DEFAULT_TEMP_OFFSET_F = 0.0f;
constexpr float DEFAULT_TEMP_OFFSET_C = deltaFToC(DEFAULT_TEMP_OFFSET_F);
constexpr const char *DEFAULT_FERMENTER_NAME = "Fermenter";
constexpr const char *DEFAULT_HYDROMETER_SELECTION_KEY = "";

constexpr uint8_t DEFAULT_BRIGHTNESS = 255;
constexpr uint8_t MIN_BRIGHTNESS = 30;
constexpr uint8_t MAX_BRIGHTNESS = 255;
constexpr uint8_t DIM_BRIGHTNESS = 60;

constexpr float MIN_VALID_TEMP_F = 20.0f;
constexpr float MAX_VALID_TEMP_F = 120.0f;
constexpr float MIN_TARGET_F = 35.0f;
constexpr float MAX_TARGET_F = 95.0f;
constexpr float MIN_DELTA_F = 0.1f;
constexpr float MAX_DELTA_F = 10.0f;
constexpr float MIN_OFFSET_F = -10.0f;
constexpr float MAX_OFFSET_F = 10.0f;
constexpr float MAX_SENSOR_JUMP_F = 50.0f;
constexpr float MIN_VALID_TEMP_C = fToC(MIN_VALID_TEMP_F);
constexpr float MAX_VALID_TEMP_C = fToC(MAX_VALID_TEMP_F);
constexpr float MIN_TARGET_C = fToC(MIN_TARGET_F);
constexpr float MAX_TARGET_C = fToC(MAX_TARGET_F);
constexpr float MIN_DELTA_C = deltaFToC(MIN_DELTA_F);
constexpr float MAX_DELTA_C = deltaFToC(MAX_DELTA_F);
constexpr float MIN_OFFSET_C = deltaFToC(MIN_OFFSET_F);
constexpr float MAX_OFFSET_C = deltaFToC(MAX_OFFSET_F);
constexpr float MAX_SENSOR_JUMP_C = deltaFToC(MAX_SENSOR_JUMP_F);
constexpr float MIN_VALID_GRAVITY = 0.900f;
constexpr float MAX_VALID_GRAVITY = 1.200f;

// Main-screen radial gauge spans a fixed absolute range (an on-dial
// thermometer), not a target-relative window. 0-35 C is round in the control
// unit; 32 F (freezing) anchors the cold-crash end and 95 F (== MAX_TARGET_F)
// the warm end, so a setpoint tick never clips off the arc.
constexpr float GAUGE_MIN_C = 0.0f;
constexpr float GAUGE_MAX_C = 35.0f;

constexpr uint8_t PROFILE_COUNT = 6;
constexpr size_t MAX_FERMENTER_NAME_LENGTH = 24;
constexpr size_t MAX_PROFILE_NAME_LENGTH = 15;

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

enum class HydrometerScanType : uint8_t {
  Unknown = 0,
  Tilt = 1,
  Rapt = 2,
};

enum class ProfileSlot : uint8_t {
  Ferment = 0,
  SoftCrash = 1,
  Crash = 2,
  Lager = 3,
  Custom1 = 4,
  Custom2 = 5,
};

struct ProfileSettings {
  String name = "";
  float targetC = DEFAULT_TARGET_C;
};

struct Settings {
  uint16_t version = SETTINGS_VERSION;
  String fermenterName = DEFAULT_FERMENTER_NAME;
  ProfileSettings profiles[PROFILE_COUNT];
  uint8_t activeProfile = static_cast<uint8_t>(ProfileSlot::Ferment);
  // Live operating setpoint. Profiles hold recallable presets; this is the
  // value the controller actually regulates to and what the dial nudges.
  float liveTargetC = DEFAULT_TARGET_C;
  bool diacetylRestActive = false;
  float diacetylRestTargetC = DEFAULT_DIACETYL_REST_TARGET_C;
  uint32_t diacetylRestDurationSeconds = DEFAULT_DIACETYL_REST_DURATION_SECONDS;
  uint32_t diacetylRestRemainingSeconds = 0;
  uint8_t diacetylRestReturnProfile =
      static_cast<uint8_t>(ProfileSlot::Ferment);
  float coolOnDeltaC = DEFAULT_COOL_ON_DELTA_C;
  float heatOnDeltaC = DEFAULT_HEAT_ON_DELTA_C;
  float holdDeltaC = DEFAULT_HOLD_DELTA_C;
  UserMode mode = UserMode::Off;
  uint32_t pumpMinOffSeconds = DEFAULT_PUMP_MIN_OFF_SECONDS;
  uint32_t pumpMinRunSeconds = DEFAULT_PUMP_MIN_RUN_SECONDS;
  float tempOffsetC = DEFAULT_TEMP_OFFSET_C;
  bool unitsFahrenheit = true;
  uint8_t brightness = DEFAULT_BRIGHTNESS;
  bool hydrometerBleEnabled = true;
  HydrometerScanType hydrometerScanType = HydrometerScanType::Unknown;
  String hydrometerSelectionKey = DEFAULT_HYDROMETER_SELECTION_KEY;
  float hydrometerOriginalGravity = NAN;
  float hydrometerStableGravity = NAN;
  uint32_t hydrometerStableSeconds = 0;
  bool gradualCrashEnabled = false;
  float gradualCrashStepC = DEFAULT_GRADUAL_CRASH_STEP_C;
  uint32_t gradualCrashStepIntervalHours = DEFAULT_GRADUAL_CRASH_STEP_INTERVAL_HOURS;
};

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

inline const char *defaultProfileName(uint8_t index) {
  switch (index) {
  case static_cast<uint8_t>(ProfileSlot::Ferment):
    return "Ferment";
  case static_cast<uint8_t>(ProfileSlot::SoftCrash):
    return "Soft Crash";
  case static_cast<uint8_t>(ProfileSlot::Crash):
    return "Crash";
  case static_cast<uint8_t>(ProfileSlot::Lager):
    return "Lager";
  case static_cast<uint8_t>(ProfileSlot::Custom1):
    return "Custom 1";
  case static_cast<uint8_t>(ProfileSlot::Custom2):
    return "Custom 2";
  default:
    return "Profile";
  }
}

inline float defaultProfileTargetC(uint8_t index) {
  switch (index) {
  case static_cast<uint8_t>(ProfileSlot::SoftCrash):
    return DEFAULT_SOFT_CRASH_TARGET_C;
  case static_cast<uint8_t>(ProfileSlot::Crash):
    return DEFAULT_CRASH_TARGET_C;
  case static_cast<uint8_t>(ProfileSlot::Lager):
    return DEFAULT_LAGER_TARGET_C;
  case static_cast<uint8_t>(ProfileSlot::Custom1):
    return DEFAULT_CUSTOM1_TARGET_C;
  case static_cast<uint8_t>(ProfileSlot::Custom2):
    return DEFAULT_CUSTOM2_TARGET_C;
  case static_cast<uint8_t>(ProfileSlot::Ferment):
  default:
    return DEFAULT_TARGET_C;
  }
}

inline uint8_t activeProfileIndex(const Settings &settings) {
  return settings.activeProfile < PROFILE_COUNT ? settings.activeProfile : 0;
}

inline ProfileSettings &activeProfile(Settings &settings) {
  return settings.profiles[activeProfileIndex(settings)];
}

inline const ProfileSettings &activeProfile(const Settings &settings) {
  return settings.profiles[activeProfileIndex(settings)];
}

inline float activeTargetC(const Settings &settings) {
  return activeProfile(settings).targetC;
}

inline void setActiveTargetC(Settings &settings, float targetC) {
  activeProfile(settings).targetC =
      clampFloat(targetC, MIN_TARGET_C, MAX_TARGET_C);
}

inline uint8_t diacetylRestReturnProfileIndex(const Settings &settings) {
  return settings.diacetylRestReturnProfile < PROFILE_COUNT
             ? settings.diacetylRestReturnProfile
             : static_cast<uint8_t>(ProfileSlot::Ferment);
}

// Live setpoint the controller regulates to (separate from profile presets).
inline float currentTargetC(const Settings &settings) {
  return settings.diacetylRestActive ? settings.diacetylRestTargetC
                                     : settings.liveTargetC;
}

inline void setCurrentTargetC(Settings &settings, float targetC) {
  if (settings.diacetylRestActive) {
    settings.diacetylRestTargetC =
        clampFloat(targetC, MIN_TARGET_C, MAX_TARGET_C);
    return;
  }
  settings.liveTargetC = clampFloat(targetC, MIN_TARGET_C, MAX_TARGET_C);
}

// Load the active profile's preset into the live setpoint (recall a preset).
inline void applyActiveProfileTarget(Settings &settings) {
  const float targetC = activeProfile(settings).targetC;
  if (settings.gradualCrashEnabled && !isnan(settings.liveTargetC) &&
      settings.liveTargetC > targetC) {
    return;
  }
  settings.liveTargetC = targetC;
}

inline void cancelDiacetylRest(Settings &settings) {
  settings.diacetylRestActive = false;
  settings.diacetylRestRemainingSeconds = 0;
}

inline void startDiacetylRest(Settings &settings) {
  settings.diacetylRestActive = true;
  settings.diacetylRestRemainingSeconds =
      settings.diacetylRestDurationSeconds;
  settings.diacetylRestTargetC =
      clampFloat(settings.diacetylRestTargetC, MIN_TARGET_C, MAX_TARGET_C);
}

inline void completeDiacetylRest(Settings &settings) {
  const float previousTargetC = currentTargetC(settings);
  const uint8_t returnProfile = diacetylRestReturnProfileIndex(settings);
  settings.diacetylRestActive = false;
  settings.diacetylRestRemainingSeconds = 0;
  settings.activeProfile = returnProfile;
  if (settings.gradualCrashEnabled &&
      returnProfile == static_cast<uint8_t>(ProfileSlot::Crash) &&
      previousTargetC > activeProfile(settings).targetC) {
    settings.liveTargetC = previousTargetC;
  }
  applyActiveProfileTarget(settings);
}

inline bool gravityIsValid(float gravity) {
  return !isnan(gravity) && gravity >= MIN_VALID_GRAVITY &&
         gravity <= MAX_VALID_GRAVITY;
}

inline void resetHydrometerSession(Settings &settings) {
  settings.hydrometerOriginalGravity = NAN;
  settings.hydrometerStableGravity = NAN;
  settings.hydrometerStableSeconds = 0;
}

inline HydrometerScanType hydrometerScanTypeFromKey(const String &key) {
  if (key.startsWith("tilt:")) {
    return HydrometerScanType::Tilt;
  }
  if (key.startsWith("rapt:")) {
    return HydrometerScanType::Rapt;
  }
  return HydrometerScanType::Unknown;
}

inline void clearHydrometerSelection(Settings &settings) {
  settings.hydrometerSelectionKey = DEFAULT_HYDROMETER_SELECTION_KEY;
  resetHydrometerSession(settings);
}

inline const char *hydrometerScanTypeText(HydrometerScanType type) {
  switch (type) {
  case HydrometerScanType::Tilt:
    return "TILT";
  case HydrometerScanType::Rapt:
    return "RAPT";
  default:
    return "UNKNOWN";
  }
}

inline const char *profileRuntimeText(const Settings &settings,
                                      RuntimeState state) {
  if (settings.diacetylRestActive) {
    return "D-REST";
  }
  if (state == RuntimeState::Cooling &&
      activeProfileIndex(settings) ==
          static_cast<uint8_t>(ProfileSlot::Crash)) {
    return "CRASHING";
  }
  return stateText(state);
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
  settings.fermenterName.trim();
  if (settings.fermenterName.length() == 0) {
    settings.fermenterName = DEFAULT_FERMENTER_NAME;
  }
  if (settings.fermenterName.length() > MAX_FERMENTER_NAME_LENGTH) {
    settings.fermenterName =
        settings.fermenterName.substring(0, MAX_FERMENTER_NAME_LENGTH);
  }
  if (settings.brightness < MIN_BRIGHTNESS) {
    settings.brightness = MIN_BRIGHTNESS;
  }
  if (settings.activeProfile >= PROFILE_COUNT) {
    settings.activeProfile = static_cast<uint8_t>(ProfileSlot::Ferment);
  }
  if (settings.diacetylRestReturnProfile >= PROFILE_COUNT) {
    settings.diacetylRestReturnProfile =
        static_cast<uint8_t>(ProfileSlot::Ferment);
  }
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    settings.profiles[i].name.trim();
    bool nameWasEmpty = settings.profiles[i].name.length() == 0;
    if (nameWasEmpty) {
      settings.profiles[i].name = defaultProfileName(i);
    }
    if (nameWasEmpty && i != static_cast<uint8_t>(ProfileSlot::Ferment) &&
        settings.profiles[i].targetC == DEFAULT_TARGET_C) {
      settings.profiles[i].targetC = defaultProfileTargetC(i);
    }
    if (settings.profiles[i].name.length() > MAX_PROFILE_NAME_LENGTH) {
      settings.profiles[i].name =
          settings.profiles[i].name.substring(0, MAX_PROFILE_NAME_LENGTH);
    }
    settings.profiles[i].targetC = snapTempC(
        clampFloat(settings.profiles[i].targetC, MIN_TARGET_C, MAX_TARGET_C),
        settings.unitsFahrenheit);
  }
  settings.gradualCrashStepC = snapDeltaC(
      clampFloat(settings.gradualCrashStepC, MIN_GRADUAL_CRASH_STEP_C,
                MAX_GRADUAL_CRASH_STEP_C),
      settings.unitsFahrenheit);
  settings.gradualCrashStepIntervalHours =
      clampU32(settings.gradualCrashStepIntervalHours,
               MIN_GRADUAL_CRASH_STEP_INTERVAL_HOURS,
               MAX_GRADUAL_CRASH_STEP_INTERVAL_HOURS);
  settings.coolOnDeltaC = snapDeltaC(
      clampFloat(settings.coolOnDeltaC, MIN_DELTA_C, MAX_DELTA_C),
      settings.unitsFahrenheit);
  settings.heatOnDeltaC = snapDeltaC(
      clampFloat(settings.heatOnDeltaC, MIN_DELTA_C, MAX_DELTA_C),
      settings.unitsFahrenheit);
  settings.holdDeltaC = snapDeltaC(
      clampFloat(settings.holdDeltaC, MIN_DELTA_C, MAX_DELTA_C),
      settings.unitsFahrenheit);
  if (settings.holdDeltaC > settings.coolOnDeltaC) {
    settings.holdDeltaC = settings.coolOnDeltaC;
  }
  if (settings.holdDeltaC > settings.heatOnDeltaC) {
    settings.holdDeltaC = settings.heatOnDeltaC;
  }
  settings.pumpMinOffSeconds = clampU32(settings.pumpMinOffSeconds, 0, 1800);
  settings.pumpMinRunSeconds = clampU32(settings.pumpMinRunSeconds, 0, 600);
  settings.tempOffsetC = snapDeltaC(
      clampFloat(settings.tempOffsetC, MIN_OFFSET_C, MAX_OFFSET_C),
      settings.unitsFahrenheit);
  settings.hydrometerSelectionKey.trim();
  if (settings.hydrometerSelectionKey.length() > 64) {
    settings.hydrometerSelectionKey =
        settings.hydrometerSelectionKey.substring(0, 64);
  }
  if (settings.hydrometerScanType != HydrometerScanType::Unknown &&
      settings.hydrometerScanType != HydrometerScanType::Tilt &&
      settings.hydrometerScanType != HydrometerScanType::Rapt) {
    settings.hydrometerScanType = HydrometerScanType::Unknown;
  }
  if (settings.hydrometerScanType == HydrometerScanType::Unknown) {
    const HydrometerScanType inferred =
        hydrometerScanTypeFromKey(settings.hydrometerSelectionKey);
    if (inferred != HydrometerScanType::Unknown) {
      settings.hydrometerScanType = inferred;
    }
  }
  if (!gravityIsValid(settings.hydrometerOriginalGravity)) {
    settings.hydrometerOriginalGravity = NAN;
  }
  if (!gravityIsValid(settings.hydrometerStableGravity)) {
    settings.hydrometerStableGravity = NAN;
  }
  settings.hydrometerStableSeconds =
      clampU32(settings.hydrometerStableSeconds, 0, 604800);

  if (isnan(settings.liveTargetC)) {
    settings.liveTargetC = activeProfile(settings).targetC;
  }
  settings.liveTargetC = snapTempC(
      clampFloat(settings.liveTargetC, MIN_TARGET_C, MAX_TARGET_C),
      settings.unitsFahrenheit);
  settings.diacetylRestTargetC = snapTempC(
      clampFloat(settings.diacetylRestTargetC, MIN_TARGET_C, MAX_TARGET_C),
      settings.unitsFahrenheit);
  settings.diacetylRestDurationSeconds =
      clampU32(settings.diacetylRestDurationSeconds,
               MIN_DIACETYL_REST_DURATION_SECONDS,
               MAX_DIACETYL_REST_DURATION_SECONDS);
  settings.diacetylRestDurationSeconds =
      ((settings.diacetylRestDurationSeconds +
        DIACETYL_REST_DURATION_STEP_SECONDS / 2) /
       DIACETYL_REST_DURATION_STEP_SECONDS) *
      DIACETYL_REST_DURATION_STEP_SECONDS;
  settings.diacetylRestDurationSeconds =
      clampU32(settings.diacetylRestDurationSeconds,
               MIN_DIACETYL_REST_DURATION_SECONDS,
               MAX_DIACETYL_REST_DURATION_SECONDS);
  if (!settings.diacetylRestActive) {
    settings.diacetylRestRemainingSeconds = 0;
  } else {
    settings.diacetylRestRemainingSeconds =
        clampU32(settings.diacetylRestRemainingSeconds, 1,
                 settings.diacetylRestDurationSeconds);
  }

  uint8_t modeValue = static_cast<uint8_t>(settings.mode);
  if (modeValue > static_cast<uint8_t>(UserMode::CoolOnly)) {
    settings.mode = UserMode::Off;
  }
}

} // namespace ferm
