#pragma once

#include <M5Dial.h>

#include "config.h"
#include "hydrometer.h"
#include "network.h"

namespace ferm {

struct UiModel {
  bool tempValid = false;
  float tempC = NAN;
  RuntimeState runtimeState = RuntimeState::Boot;
  FaultCode faultCode = FaultCode::None;
  bool heaterOn = false;
  bool pumpOn = false;
  uint32_t pumpOffElapsedMs = 0;
  bool outputTestActive = false;
  OutputTestKind outputTestKind = OutputTestKind::None;
  bool demoSensor = false;
  bool notReaching = false;
  bool longOutput = false;
  HydrometerReading hydrometer;
  HydrometerReading hydrometerDevices[HydrometerManager::MAX_DEVICES];
  uint8_t hydrometerDeviceCount = 0;
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
  void previewBrightness(uint8_t brightness);
  void queueRemoteInput(const ScreenInputEvent &event);

  ScreenFrame canvasFrame() const {
    ScreenFrame frame{};
    frame.width = _canvas.width();
    frame.height = _canvas.height();
    frame.len = _canvas.bufferLength();
    if (frame.height > 0) {
      frame.stride = static_cast<uint16_t>(frame.len / frame.height / 2);
    }
    frame.data = static_cast<const uint8_t *>(_canvas.getBuffer());
    return frame;
  }

 private:
  enum class Screen : uint8_t {
    Main,
    Hydrometer,
    QuickMenu,
    QuickProfile,
    QuickMode,
    QuickConfirm,
    Menu,
    Edit,
    EditInfo,
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
    DRest,
  };

  enum class QuickConfirmKind : uint8_t {
    Apply,
    CrashGradual,
  };

  void processInput(uint32_t nowMs, Settings &settings);
  void applyPendingRemoteInput(uint32_t nowMs, Settings &settings);
  void handleTouch(uint32_t nowMs, Settings &settings);
  void handleTouchClick(int16_t x, int16_t y, uint32_t nowMs, Settings &settings);
  void handleSwipe(uint32_t nowMs, Settings &settings, int16_t dx, int16_t dy);
  void scrollMenuByTouch(int16_t deltaY);
  void scrollQuickByTouch(int16_t deltaY, const Settings &settings);
  void handleEncoder(int32_t delta, Settings &settings);
  void handleShortPress(uint32_t nowMs, Settings &settings);
  void handleLongPress(Settings &settings);
  void openSettingsMenu();
  void leaveMenuToParent();
  void enterMenuGroup();
  void moveMenuSelection(int32_t steps);
  void applyLiveBrightness(uint8_t brightness);
  void commitPendingSetpoint(uint32_t nowMs, Settings &settings);
  void cancelPendingSetpoint();
  // Shared Cancel (left) / confirm (right) action-row hit rects used by
  // setpoint confirm, ConfirmEdit, ConfirmTest, and QuickConfirm.
  bool confirmRowLeftHit(int16_t x, int16_t y) const;
  bool confirmRowRightHit(int16_t x, int16_t y) const;
  void openQuickMenu(const Settings &settings);
  void moveQuickMenu(int32_t delta);
  void selectQuickAction(const Settings &settings);
  void moveQuickSelection(int32_t delta, const Settings &settings);
  void requestQuickConfirm(const Settings &settings);
  void requestCrashGradualPrompt(bool defaultGradual);
  void confirmQuickAction(Settings &settings);
  void applyCrashProfile(Settings &settings, bool gradual);
  void cancelQuickFlow();
  void beginEdit(uint8_t index, const Settings &settings);
  void cancelEdit(Settings &settings);
  void saveEdit(Settings &settings);
  void requestEditConfirm(EditConfirmAction action);
  void confirmEditAction(Settings &settings);
  void cancelEditConfirm();
  void confirmOutputTest();
  void editCurrentValue(int32_t delta, Settings &settings);
  void resetCurrentValue(Settings &settings);
  int32_t filteredSettingsDelta(int32_t delta, int32_t &accumulator, int32_t divisor);
  void resetSettingsEncoderFilters();
  void requestSave();
  void markActivity(uint32_t nowMs);

  void draw(uint32_t nowMs, const Settings &settings, const UiModel &model);
  void drawMain(uint32_t nowMs, const Settings &settings, const UiModel &model);
  void drawHydrometer(uint32_t nowMs, const Settings &settings,
                      const UiModel &model);
  void drawQuickMenu(const Settings &settings, const UiModel &model);
  void drawQuickProfile(const Settings &settings, const UiModel &model);
  void drawQuickMode(const Settings &settings, const UiModel &model);
  void drawQuickConfirm(const Settings &settings, const UiModel &model);
  bool quickCancelHit(int16_t x, int16_t y) const;
  void drawMenu(const Settings &settings, const NetworkSnapshot &network);
  void drawEdit(const Settings &settings);
  void drawEditInfo(const Settings &settings);
  void drawConfirmEdit(const Settings &settings);
  void drawConfirmTest();
  void drawAbout();
  void drawHelp();
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
  void drawPageDots(uint8_t activePage, uint8_t pageCount);
  void drawOutputChips(const UiModel &model);
  uint8_t wifiStatus(const NetworkSnapshot &network) const;
  String temperatureNumber(float tempC, bool unitsFahrenheit) const;
  const char *temperatureUnit(bool unitsFahrenheit) const;
  String formatTemperature(float tempC, bool unitsFahrenheit) const;
  String diacetylRestRemainingText(const Settings &settings) const;
  String hydrometerValueText(const Settings &settings) const;
  int32_t hydrometerOptionIndexFromSettings(const Settings &settings) const;
  const char *quickActionLabel(QuickAction action) const;
  String quickActionValue(QuickAction action, const Settings &settings) const;
  String quickPendingValue(const Settings &settings) const;
  String menuValue(uint8_t index, const Settings &settings, const NetworkSnapshot &network = NetworkSnapshot{}) const;
  String defaultMenuValue(uint8_t index, const Settings &settings) const;
  bool editHasReset(const Settings &settings) const;
  bool editCommitsProfile() const;
  const char *editCommitLabel() const;
  String editDefaultLine(const Settings &settings) const;
  String editConfirmLine(const Settings &settings) const;
  uint16_t stateColor(RuntimeState state, FaultCode fault) const;
  uint16_t stateBackground(RuntimeState state, FaultCode fault) const;

  M5Canvas _canvas;
  Screen _screen = Screen::Main;
  int32_t _lastEncoder = 0;
  // Settings navigation: group list first, then items within the group.
  // When _menuInGroup, _menuIndex is a MenuIndex value for the focused item.
  bool _menuInGroup = false;
  uint8_t _menuGroup = 0;
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
  QuickConfirmKind _quickConfirmKind = QuickConfirmKind::Apply;
  bool _pendingGradualCrash = false;
  uint32_t _lastDrawMs = 0;
  uint32_t _lastActivityMs = 0;
  uint32_t _pressStartedMs = 0;
  bool _longPressHandled = false;
  bool _touchPressActive = false;
  bool _hasPendingRemoteInput = false;
  ScreenInputEvent _pendingRemoteInput{};
  int16_t _pressX = -1;
  int16_t _pressY = -1;
  bool _dimmed = false;
  uint8_t _appliedBrightness = 255;
  uint32_t _brightnessPreviewUntilMs = 0;
  uint32_t _setpointFocusUntilMs = 0;
  // Main-screen setpoint adjustments are previewed, not committed, until the
  // user confirms — guards against an accidental bump of the dial.
  bool _setpointEditing = false;
  float _pendingTargetC = 0.0f;
  bool _largeFontLoaded = false;
  String _toast = "";
  uint32_t _toastUntilMs = 0;
  // Snapshot of the discovered hydrometers, refreshed each update() so the
  // Hydrometer edit screen can list/cycle through them while editing.
  HydrometerReading _hydroDevices[HydrometerManager::MAX_DEVICES];
  uint8_t _hydroDeviceCount = 0;
  // 0 = off, 1 = scanning for Tilt, 2 = scanning for RAPT, 3+ = device index
  // (index - 3) within _hydroDevices.
  int32_t _hydroOptionIndex = 0;
  // Last main-screen attention flags (for badge tap → toast).
  uint8_t _lastAttention = 0;
};

}  // namespace ferm
