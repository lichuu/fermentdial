#include <Arduino.h>

#include "config.h"
#include "control.h"
#include "network.h"
#include "storage.h"
#include "ui.h"

using namespace ferm;

Settings settings;
SettingsStorage storage;
TemperatureSensor temperatureSensor;
FermentationController controller;
NetworkManager network;
DisplayUI ui;

uint32_t lastSerialLogMs = 0;

void logStatus(uint32_t nowMs) {
  if (nowMs - lastSerialLogMs < 2000) {
    return;
  }
  lastSerialLogMs = nowMs;

  Serial.print(F("tempF="));
  if (temperatureSensor.isValid()) {
    Serial.print(temperatureSensor.temperatureF(), 1);
  } else {
    Serial.print(F("invalid"));
  }
  Serial.print(F(" targetF="));
  Serial.print(settings.targetF, 1);
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
  Serial.println(F("DEMO SENSOR MODE ENABLED - do not use for real fermentation control"));
#endif

  ui.begin();
  storage.begin();
  bool loaded = storage.load(settings);
  sanitizeSettings(settings);
  Serial.println(loaded ? F("Loaded saved settings") : F("Using safe defaults"));

  temperatureSensor.begin(millis());
  network.begin(settings);
}

void loop() {
  uint32_t nowMs = millis();

  temperatureSensor.update(nowMs, settings);
  network.update(nowMs, settings);

  controller.update(nowMs, settings, temperatureSensor.isValid(), temperatureSensor.temperatureF());

  UiModel model;
  model.tempValid = temperatureSensor.isValid();
  model.tempF = temperatureSensor.temperatureF();
  model.runtimeState = controller.runtimeState();
  model.faultCode = controller.faultCode();
  model.heaterOn = controller.heaterOn();
  model.pumpOn = controller.pumpOn();
  model.outputTestActive = controller.outputTestActive();
  model.outputTestKind = controller.outputTestKind();
  model.demoSensor = temperatureSensor.demoMode();
  model.network = network.snapshot();

  ui.update(nowMs, settings, model);

  if (ui.consumeSaveRequested() || network.consumeSettingsChanged()) {
    sanitizeSettings(settings);
    storage.scheduleSave(nowMs);
    controller.update(nowMs, settings, temperatureSensor.isValid(), temperatureSensor.temperatureF());
  }

  if (ui.consumeWifiSetupRequested()) {
    network.requestSetupPortal();
  }

  OutputTestKind testRequest = ui.consumeOutputTestRequest();
  if (testRequest != OutputTestKind::None) {
    bool accepted = controller.requestOutputTest(testRequest, nowMs, settings, temperatureSensor.isValid());
    if (!accepted) {
      ui.notifyOutputTestRejected();
    }
  }

  network.publishState(nowMs, settings, temperatureSensor, controller);
  storage.loop(nowMs, settings);
  logStatus(nowMs);
}
