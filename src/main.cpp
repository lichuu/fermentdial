#include <Arduino.h>

#include "config.h"
#include "control.h"
#include "hydrometer.h"
#include "network.h"
#include "storage.h"
#include "ui.h"

using namespace ferm;

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
bool previousDiacetylRestActive = false;
bool previousGradualCrashActive = false;

void prepareForFirmwareUpdate() {
  // Force the relays off for the flash itself, but keep the saved mode/profile
  // so the controller resumes what was running once it reboots (resilient to
  // both OTA updates and power loss). Flush current settings first.
  Serial.println(F("Firmware update requested; forcing outputs OFF"));
  controller.forceOutputsOff(millis());
  storage.saveNow(settings);
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

  temperatureSensor.begin(millis());
  hydrometer.begin();
  network.setFirmwareUpdateSafetyCallback(prepareForFirmwareUpdate);
  network.begin(settings, hydrometer);
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

  controller.update(nowMs, settings, temperatureSensor.isValid(),
                    temperatureSensor.temperatureC());

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
      checkpointHydrometer || gradualCrashChanged) {
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
