#include <Arduino.h>
#include <LittleFS.h>

#include "config.h"
#include "control.h"
#include "events.h"
#include "history.h"
#include "hydrometer.h"
#include "network.h"
#include "policy.h"
#include "storage.h"
#include "time_sync.h"
#include "ui.h"

namespace ferm {

constexpr uint32_t EVENT_FLUSH_INTERVAL_MS = 15000;

Settings settings;
SettingsStorage storage;
TemperatureSensor temperatureSensor;
FermentationController controller;
HydrometerManager hydrometer;
NetworkManager network;
DisplayUI ui;

uint32_t lastSerialLogMs = 0;
uint32_t lastDiacetylRestSaveMs = 0;
uint32_t lastHydrometerSaveMs = 0;
uint32_t lastProgramSaveMs = 0;

EventLog eventLog;
HistoryLog historyLog;
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

void prepareFactoryReset() {
  controller.forceOutputsOff(millis());
  storage.factoryReset();
  eventLog.clear();
  historyLog.clear();
}

void prepareFermentReset() {
  const uint32_t nowMs = millis();
#if FERM_DEMO_SENSOR
  hydrometer.resetDemoFerment(nowMs);
#endif
  deactivateSupersededModes(settings);
  resetHydrometerSession(settings);
  historyLog.clear();
  eventLog.clear();
  network.clearLiveHistory();
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

}  // namespace ferm

void setup() {
  using namespace ferm;
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
  network.setFactoryResetCallback(prepareFactoryReset);
  network.setFermentResetCallback(prepareFermentReset);
  network.setBrightnessPreviewCallback(
      [](uint8_t brightness) { ui.previewBrightness(brightness); });
  network.setScreenFrameProvider([]() -> ScreenFrame { return ui.canvasFrame(); });
#if FERM_ENABLE_SCREEN_MIRROR
  network.setScreenInputCallback(
      [](const ScreenInputEvent &event) { ui.queueRemoteInput(event); });
#endif
  network.begin(settings, hydrometer);
  network.setEventLog(&eventLog);
#if FERM_DEMO_SENSOR
  // LittleFS survives USB/OTA flashes; drop any prior demo CSV so the web
  // chart does not replay stacked cycles from earlier runs.
  historyLog.clear();
  eventLog.clear();
  network.clearLiveHistory();
#endif
}

void loop() {
  using namespace ferm;
  uint32_t nowMs = millis();

  temperatureSensor.update(nowMs, settings);
  const bool hydrometerChanged = hydrometer.update(nowMs, settings);
#if FERM_DEMO_SENSOR
  if (hydrometer.consumeDemoCycleComplete()) {
    prepareFermentReset();
  }
#endif
  const HydrometerReading selectedHydrometer =
      hydrometer.selectedReading(settings, nowMs);
  network.update(nowMs, settings);
  const bool diacetylRestChanged = updateDiacetylRest(nowMs);
  const bool gradualCrashChanged = updateGradualCrash(nowMs);
  const bool programChanged = updateProgramRunner(nowMs, selectedHydrometer);

  controller.update(nowMs, settings, temperatureSensor.isValid(),
                    temperatureSensor.temperatureC());

  evaluateAlerts(nowMs, selectedHydrometer);
  eventLog.upgradeUptimeTimestamps(nowMs);
  if (eventLog.dirty() && nowMs - lastEventFlushMs >= EVENT_FLUSH_INTERVAL_MS) {
    lastEventFlushMs = nowMs;
    eventLog.flush();
  }
#if FERM_ENABLE_NETWORK
  if (historyLog.due(nowMs)) {
    historyLog.markSampled(nowMs);
    historyLog.append(buildHistoryRow(nowMs, selectedHydrometer));
  }
#endif

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
  network.serviceWeb();

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
