#include "ui.h"

#include "fonts/dejavu_sans_bold_44_vlw.h"
#include "fonts/help_glyph.h"

namespace ferm {

namespace {

enum MenuIndex : uint8_t {
  MENU_PROFILE = 0,
  MENU_TARGET = 1,
  MENU_DREST = 2,
  MENU_MODE = 3,
  MENU_COOL_ON = 4,
  MENU_HEAT_ON = 5,
  MENU_HOLD_BAND = 6,
  MENU_COOLING_OFF = 7,
  MENU_COOLING_RUN = 8,
  MENU_OFFSET = 9,
  MENU_UNITS = 10,
  MENU_WIFI = 11,
  MENU_MQTT = 12,
  MENU_HYDROMETER = 13,
  MENU_HEATER_TEST = 14,
  MENU_PUMP_TEST = 15,
  MENU_ABOUT = 16,
};

constexpr uint8_t MENU_COUNT = MENU_ABOUT + 1;
constexpr uint8_t QUICK_ACTION_COUNT = 2;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Scale an RGB565 colour toward black, for pressed-button feedback.
constexpr uint16_t dim565(uint16_t c, uint8_t num = 9, uint8_t den = 16) {
  return static_cast<uint16_t>(
      (((((c >> 11) & 0x1F) * num / den) & 0x1F) << 11) |
      (((((c >> 5) & 0x3F) * num / den) & 0x3F) << 5) |
      ((((c & 0x1F) * num / den) & 0x1F)));
}

// Alpha-blend foreground over background (a = 0..255), for AA glyph blitting.
constexpr uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
  return static_cast<uint16_t>(
      (((((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (255 - a)) / 255)
       << 11) |
      (((((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * (255 - a)) / 255) << 5) |
      ((((fg & 0x1F) * a + (bg & 0x1F) * (255 - a)) / 255)));
}

// Appliance-style palette adapted for the round M5Stack Dial.
constexpr uint16_t COLOR_BG = rgb565(13, 27, 30);
constexpr uint16_t COLOR_PANEL = rgb565(19, 36, 40);
constexpr uint16_t COLOR_PANEL_DARK = rgb565(9, 20, 24);
constexpr uint16_t COLOR_TEXT = rgb565(208, 232, 240);
constexpr uint16_t COLOR_TEXT_MUTED = rgb565(106, 154, 170);
constexpr uint16_t COLOR_ACCENT = rgb565(176, 216, 248);
constexpr uint16_t COLOR_BLUE = rgb565(53, 111, 137);
constexpr uint16_t COLOR_HEAT = rgb565(227, 96, 24);     // warm amber
constexpr uint16_t COLOR_COOL = rgb565(176, 216, 248);   // pale blue
constexpr uint16_t COLOR_CRASH = rgb565(176, 216, 248);
constexpr uint16_t COLOR_OK = rgb565(54, 200, 122);
constexpr uint16_t COLOR_WARN = rgb565(255, 196, 64);
constexpr uint16_t COLOR_FAULT = rgb565(228, 72, 64);
constexpr int32_t MENU_ENCODER_DIVISOR = 2;
constexpr int32_t EDIT_ENCODER_DIVISOR = 2;

// Radial-gauge palette + geometry (main screen).
constexpr uint16_t COLOR_TRACK = rgb565(30, 56, 64);     // unlit ring
constexpr uint16_t COLOR_TICK = rgb565(53, 111, 137);    // 5 C scale ticks
constexpr uint16_t COLOR_GOLD = rgb565(255, 209, 120);   // setpoint tick
constexpr uint16_t COLOR_WIFI_OFF = rgb565(96, 122, 130);
constexpr int32_t TOUCH_MENU_ITEM_PX = 28;

constexpr int16_t GAUGE_R = 108;        // gauge radius (hugs the bezel)
constexpr int16_t GAUGE_W = 7;          // ring thickness
constexpr float GA_START = 135.0f;      // lower-left  (cold)
constexpr float GA_SWEEP = 270.0f;      // clockwise to lower-right (hot)

// Output indicators: true = snowflake / heat-steam glyphs, false = "H"/"P".
constexpr bool USE_OUTPUT_ICONS = true;

constexpr const char *MENU_LABELS[MENU_COUNT] = {
    "Profile",   "Target",    "D-Rest",      "Mode",        "Cool on",
    "Heat on",   "Hold band", "Cooling off", "Cooling run", "Offset",
    "Units",     "Wi-Fi",     "MQTT",        "Hydrometer",  "Heater test",
    "Pump test", "About",
};

} // namespace
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
  processInput(nowMs, settings);

  // An un-confirmed setpoint preview reverts on its own, so a stray bump of the
  // dial never sticks.
  if (_setpointEditing && nowMs >= _setpointFocusUntilMs) {
    cancelPendingSetpoint();
  }

  if (_screen != Screen::Main && nowMs - _lastActivityMs > UI_TIMEOUT_MS) {
    if ((_screen == Screen::Edit || _screen == Screen::ConfirmEdit) &&
        _editSnapshotValid) {
      settings = _editSnapshot;
      _editSnapshotValid = false;
    }
    _quickConfirmKind = QuickConfirmKind::Apply;
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
