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
    QuickMenu,
    QuickProfile,
    QuickMode,
    QuickConfirm,
    Menu,
    Edit,
    ConfirmEdit,
    ConfirmTest,
    About,
    Help,
  };

  enum class EditConfirmAction : uint8_t {
    None,
    Save,
    Reset,
  };

  enum class QuickAction : uint8_t {
    Profile,
    Mode,
  };

  void processInput(uint32_t nowMs, Settings &settings);
  void handleTouch(uint32_t nowMs, Settings &settings);
  void handleSwipe(uint32_t nowMs, Settings &settings, int16_t dx, int16_t dy);
  void scrollMenuByTouch(int16_t deltaY);
  void scrollQuickByTouch(int16_t deltaY, const Settings &settings);
  void handleEncoder(int32_t delta, Settings &settings);
  void handleShortPress(uint32_t nowMs, Settings &settings);
  void handleLongPress(Settings &settings);
  void commitPendingSetpoint(uint32_t nowMs, Settings &settings);
  void cancelPendingSetpoint();
  // Hit rects for the main-screen setpoint confirm/cancel buttons.
  bool setpointConfirmHit(int16_t x, int16_t y) const;
  bool setpointCancelHit(int16_t x, int16_t y) const;
  void openQuickMenu(const Settings &settings);
  void moveQuickMenu(int32_t delta);
  void selectQuickAction(const Settings &settings);
  void moveQuickSelection(int32_t delta, const Settings &settings);
  void requestQuickConfirm();
  void confirmQuickAction(Settings &settings);
  void cancelQuickFlow();
  void beginEdit(uint8_t index, const Settings &settings);
  void cancelEdit(Settings &settings);
  void saveEdit(Settings &settings);
  void requestEditConfirm(EditConfirmAction action);
  void confirmEditAction(Settings &settings);
  void cancelEditConfirm();
  void editCurrentValue(int32_t delta, Settings &settings);
  void resetCurrentValue(Settings &settings);
  int32_t filteredSettingsDelta(int32_t delta, int32_t &accumulator, int32_t divisor);
  void resetSettingsEncoderFilters();
  void requestSave();
  void markActivity(uint32_t nowMs);

  void draw(uint32_t nowMs, const Settings &settings, const UiModel &model);
  void drawMain(uint32_t nowMs, const Settings &settings, const UiModel &model);
  void drawQuickMenu(const Settings &settings, const UiModel &model);
  void drawQuickProfile(const Settings &settings, const UiModel &model);
  void drawQuickMode(const Settings &settings, const UiModel &model);
  void drawQuickConfirm(const Settings &settings, const UiModel &model);
  bool quickCancelHit(int16_t x, int16_t y) const;
  void drawMenu(const Settings &settings, const NetworkSnapshot &network);
  void drawEdit(const Settings &settings);
  void drawConfirmEdit(const Settings &settings);
  void drawConfirmTest();
  void drawAbout();
  void drawHelp();
  void drawPill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fill, uint16_t outline,
                const String &text, uint16_t textColor, uint8_t textSize = 1);
  void drawGhostButton(int16_t cx, int16_t cy, int16_t w, int16_t h,
                       const String &text, uint16_t outline,
                       const lgfx::IFont *font);
  void drawSolidButton(int16_t cx, int16_t cy, int16_t w, int16_t h,
                       const String &text, uint16_t fill, uint16_t textColor,
                       const lgfx::IFont *font, uint16_t outline = 0);
  // True when a finger is currently held within the given rect (top-left
  // origin), used to render pressed-button feedback.
  bool pressInRect(int16_t x, int16_t y, int16_t w, int16_t h) const;
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
  void drawHelpIcon(int16_t cx, int16_t cy, uint16_t color, uint16_t bg);
  void drawOutputChips(const UiModel &model);
  uint8_t wifiStatus(const NetworkSnapshot &network) const;
  String temperatureNumber(float tempC, bool unitsFahrenheit) const;
  const char *temperatureUnit(bool unitsFahrenheit) const;
  String formatTemperature(float tempC, bool unitsFahrenheit) const;
  const char *quickActionLabel(QuickAction action) const;
  String quickActionValue(QuickAction action, const Settings &settings) const;
  String quickPendingValue(const Settings &settings) const;
  String menuValue(uint8_t index, const Settings &settings, const NetworkSnapshot &network = NetworkSnapshot{}) const;
  String defaultMenuValue(uint8_t index, const Settings &settings) const;
  bool editHasReset() const;
  String editDefaultLine(const Settings &settings) const;
  String editConfirmLine(const Settings &settings) const;
  uint16_t stateColor(RuntimeState state, FaultCode fault) const;
  uint16_t stateBackground(RuntimeState state, FaultCode fault) const;

  M5Canvas _canvas;
  Screen _screen = Screen::Main;
  int32_t _lastEncoder = 0;
  uint8_t _menuIndex = 0;
  uint8_t _editIndex = 0;
  Settings _editSnapshot;
  bool _editSnapshotValid = false;
  int32_t _menuEncoderAccumulator = 0;
  int32_t _editEncoderAccumulator = 0;
  int32_t _quickEncoderAccumulator = 0;
  int32_t _touchMenuScrollAccumulator = 0;
  bool _dirty = true;
  bool _saveRequested = false;
  bool _wifiSetupRequested = false;
  OutputTestKind _pendingOutputTest = OutputTestKind::None;
  EditConfirmAction _pendingEditConfirm = EditConfirmAction::None;
  uint8_t _quickIndex = 0;
  uint8_t _pendingProfile = 0;
  UserMode _pendingMode = UserMode::Off;
  QuickAction _pendingQuickAction = QuickAction::Profile;
  uint32_t _lastDrawMs = 0;
  uint32_t _lastActivityMs = 0;
  uint32_t _pressStartedMs = 0;
  bool _longPressHandled = false;
  bool _touchPressActive = false;
  int16_t _pressX = -1;
  int16_t _pressY = -1;
  bool _dimmed = false;
  uint8_t _appliedBrightness = 255;
  uint32_t _setpointFocusUntilMs = 0;
  // Main-screen setpoint adjustments are previewed, not committed, until the
  // user confirms — guards against an accidental bump of the dial.
  bool _setpointEditing = false;
  float _pendingTargetC = 0.0f;
  bool _largeFontLoaded = false;
  String _toast = "";
  uint32_t _toastUntilMs = 0;
};

}  // namespace ferm
