#pragma once

#include <M5Dial.h>

#include "config.h"
#include "network.h"

namespace ferm {

struct UiModel {
  bool tempValid = false;
  float tempC = NAN;
  RuntimeState runtimeState = RuntimeState::Boot;
  FaultCode faultCode = FaultCode::None;
  bool heaterOn = false;
  bool pumpOn = false;
  bool outputTestActive = false;
  OutputTestKind outputTestKind = OutputTestKind::None;
  bool demoSensor = false;
  NetworkSnapshot network;
};

class DisplayUI {
 public:
  DisplayUI();

  void begin();
  void update(uint32_t nowMs, Settings &settings, const UiModel &model);

  bool consumeSaveRequested();
  OutputTestKind consumeOutputTestRequest();
  bool consumeWifiSetupRequested();
  void notifyOutputTestRejected();

 private:
  enum class Screen : uint8_t {
    Main,
    Menu,
    Edit,
    ConfirmTest,
    About,
  };

  void processInput(uint32_t nowMs, Settings &settings);
  void handleTouch(uint32_t nowMs, Settings &settings);
  void handleEncoder(int32_t delta, Settings &settings);
  void handleShortPress(uint32_t nowMs, Settings &settings);
  void handleLongPress(Settings &settings);
  void editCurrentValue(int32_t delta, Settings &settings);
  int32_t filteredSettingsDelta(int32_t delta, int32_t &accumulator, int32_t divisor);
  void resetSettingsEncoderFilters();
  void requestSave();
  void markActivity(uint32_t nowMs);

  void draw(uint32_t nowMs, const Settings &settings, const UiModel &model);
  void drawMain(uint32_t nowMs, const Settings &settings, const UiModel &model);
  void drawMenu(const Settings &settings, const NetworkSnapshot &network);
  void drawEdit(const Settings &settings);
  void drawConfirmTest();
  void drawAbout();
  void drawPill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fill, uint16_t outline,
                const String &text, uint16_t textColor, uint8_t textSize = 1);
  bool ensureLargeFont();
  void useDefaultFont();
  void drawTemperatureUnit(int16_t x, int16_t y, bool unitsFahrenheit,
                           uint16_t color, uint16_t bg);
  void drawTargetTemperature(int16_t cx, int16_t y, float tempC,
                             bool unitsFahrenheit, uint16_t color,
                             uint16_t bg);

  // Radial-gauge helpers (main screen). Angles use a screen-math convention:
  // 0 deg = 3 o'clock, increasing clockwise (matches +y-down sprites).
  float gaugeAngle(float tempC) const;
  void polar(int16_t r, float deg, int16_t &x, int16_t &y) const;
  void drawArcSeg(int16_t r, float a0, float a1, uint16_t color, int16_t width);
  void drawMarker(int16_t r, float deg, uint16_t fill, uint16_t outline);
  void drawTick(int16_t r0, int16_t r1, float deg, uint16_t color, int16_t width);
  void drawWifiIcon(int16_t cx, int16_t cy, int16_t size, uint8_t status, uint16_t bg);
  void drawHeatIcon(int16_t cx, int16_t cy, int16_t size, uint16_t color);
  void drawSnowflake(int16_t cx, int16_t cy, int16_t size, uint16_t color);
  void drawOutputChips(const UiModel &model);
  uint8_t wifiStatus(const NetworkSnapshot &network) const;
  String temperatureNumber(float tempC, bool unitsFahrenheit) const;
  const char *temperatureUnit(bool unitsFahrenheit) const;
  String formatTemperature(float tempC, bool unitsFahrenheit) const;
  String menuValue(uint8_t index, const Settings &settings, const NetworkSnapshot &network = NetworkSnapshot{}) const;
  uint16_t stateColor(RuntimeState state, FaultCode fault) const;
  uint16_t stateBackground(RuntimeState state, FaultCode fault) const;

  M5Canvas _canvas;
  Screen _screen = Screen::Main;
  int32_t _lastEncoder = 0;
  uint8_t _menuIndex = 0;
  uint8_t _editIndex = 0;
  int32_t _menuEncoderAccumulator = 0;
  int32_t _editEncoderAccumulator = 0;
  bool _dirty = true;
  bool _saveRequested = false;
  bool _wifiSetupRequested = false;
  OutputTestKind _pendingOutputTest = OutputTestKind::None;
  uint32_t _lastDrawMs = 0;
  uint32_t _lastActivityMs = 0;
  uint32_t _pressStartedMs = 0;
  bool _longPressHandled = false;
  bool _dimmed = false;
  uint8_t _appliedBrightness = 255;
  uint32_t _setpointFocusUntilMs = 0;
  bool _largeFontLoaded = false;
  String _toast = "";
  uint32_t _toastUntilMs = 0;
};

}  // namespace ferm
