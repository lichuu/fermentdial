#include "ui.h"

#include "fonts/dejavu_sans_bold_44_vlw.h"
#include "fonts/help_glyph.h"

#include "ui_internal.h"

namespace ferm {

DisplayUI::DisplayUI() : _canvas(&M5Dial.Display) {}

void DisplayUI::begin() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
  M5Dial.Display.setRotation(0);
  M5Dial.Display.setBrightness(255);

  _canvas.setColorDepth(16);
  _canvas.createSprite(M5Dial.Display.width(), M5Dial.Display.height());
  _canvas.setTextDatum(middle_center);
  _lastEncoder = M5Dial.Encoder.read();
  _lastActivityMs = millis();

  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  _canvas.fillScreen(COLOR_BG);
  drawArcSeg(GAUGE_R, GA_START, GA_START + GA_SWEEP, COLOR_TRACK, 6);
  drawArcSeg(GAUGE_R, GA_START, GA_START + 60.0f, COLOR_COOL, 6);
  drawArcSeg(GAUGE_R, GA_START + GA_SWEEP - 60.0f, GA_START + GA_SWEEP,
             COLOR_HEAT, 6);
  _canvas.fillSmoothCircle(cx, cy - 54, 5, COLOR_GOLD);

  _canvas.fillSmoothRoundRect(cx - 82, cy - 25, 164, 50, 14, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 82, cy - 25, 164, 50, 14, COLOR_BLUE);

  _canvas.setTextColor(COLOR_ACCENT, COLOR_PANEL);
  _canvas.drawString(FIRMWARE_NAME, cx, cy - 5, &fonts::DejaVu18);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(String("v") + FIRMWARE_VERSION, cx, cy + 42,
                     &fonts::DejaVu12);
  _canvas.fillSmoothCircle(cx - 24, cy + 66, 3, COLOR_COOL);
  _canvas.fillSmoothCircle(cx, cy + 66, 3, COLOR_OK);
  _canvas.fillSmoothCircle(cx + 24, cy + 66, 3, COLOR_HEAT);
  _canvas.pushSprite(0, 0);
}
void DisplayUI::update(uint32_t nowMs, Settings &settings,
                       const UiModel &model) {
  M5Dial.update();
  // Snapshot the discovered hydrometers before processing input so the
  // Hydrometer edit screen always cycles through the latest scan results.
  _hydroDeviceCount = model.hydrometerDeviceCount;
  for (uint8_t i = 0; i < _hydroDeviceCount; i++) {
    _hydroDevices[i] = model.hydrometerDevices[i];
  }
  _lastNetwork = model.network;
  processInput(nowMs, settings);

  // An un-confirmed setpoint preview reverts on its own, so a stray bump of the
  // dial never sticks.
  if (_setpointEditing && nowMs >= _setpointFocusUntilMs) {
    cancelPendingSetpoint();
  }

  if (_screen != Screen::Main && nowMs - _lastActivityMs > UI_TIMEOUT_MS) {
    if ((_screen == Screen::Edit || _screen == Screen::ConfirmEdit) &&
        _editSnapshotValid) {
      const bool wasBrightness = _editIndex == MENU_BRIGHTNESS;
      settings = _editSnapshot;
      _editSnapshotValid = false;
      if (wasBrightness) {
        applyLiveBrightness(settings.brightness);
      }
    }
    _quickConfirmKind = QuickConfirmKind::Apply;
    _menuInGroup = false;
    _screen = Screen::Main;
    resetSettingsEncoderFilters();
    _dirty = true;
  }

  const uint8_t activeBrightness = settings.brightness;
  if (nowMs < _brightnessPreviewUntilMs) {
    // Web preview owns backlight until holdoff expires.
  } else if (!_dimmed && nowMs - _lastActivityMs > DISPLAY_DIM_MS) {
    const uint8_t dim =
        DIM_BRIGHTNESS < activeBrightness ? DIM_BRIGHTNESS : activeBrightness;
    M5Dial.Display.setBrightness(dim);
    _dimmed = true;
    _appliedBrightness = dim;
  } else if (!_dimmed && _appliedBrightness != activeBrightness) {
    M5Dial.Display.setBrightness(activeBrightness);
    _appliedBrightness = activeBrightness;
  }

  if (_dirty || nowMs - _lastDrawMs >= UI_REDRAW_INTERVAL_MS) {
    draw(nowMs, settings, model);
    _lastDrawMs = nowMs;
    _dirty = false;
  }
}

bool DisplayUI::consumeSaveRequested() {
  bool requested = _saveRequested;
  _saveRequested = false;
  return requested;
}

OutputTestKind DisplayUI::consumeOutputTestRequest() {
  OutputTestKind requested = _pendingOutputTest;
  _pendingOutputTest = OutputTestKind::None;
  return requested;
}

bool DisplayUI::consumeWifiSetupRequested() {
  bool requested = _wifiSetupRequested;
  _wifiSetupRequested = false;
  return requested;
}

void DisplayUI::notifyOutputTestRejected() {
  _toast = "Test blocked";
  _toastUntilMs = millis() + 2500;
  _dirty = true;
}
void DisplayUI::previewBrightness(uint8_t brightness) {
  brightness = clampBrightness(brightness);
  M5Dial.Display.setBrightness(brightness);
  _appliedBrightness = brightness;
  _dimmed = false;
  _lastActivityMs = millis();
  _brightnessPreviewUntilMs = millis() + 400;
}

} // namespace ferm
