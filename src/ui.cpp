#include "ui.h"

#include "fonts/dejavu_sans_bold_44_vlw.h"

namespace ferm {

namespace {

enum MenuIndex : uint8_t {
  MENU_PROFILE = 0,
  MENU_TARGET = 1,
  MENU_MODE = 2,
  MENU_COOL_ON = 3,
  MENU_HEAT_ON = 4,
  MENU_HOLD_BAND = 5,
  MENU_COOLING_OFF = 6,
  MENU_COOLING_RUN = 7,
  MENU_OFFSET = 8,
  MENU_UNITS = 9,
  MENU_WIFI = 10,
  MENU_MQTT = 11,
  MENU_HEATER_TEST = 12,
  MENU_PUMP_TEST = 13,
  MENU_ABOUT = 14,
};

constexpr uint8_t MENU_COUNT = MENU_ABOUT + 1;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
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

constexpr int16_t GAUGE_R = 108;        // gauge radius (hugs the bezel)
constexpr int16_t GAUGE_W = 7;          // ring thickness
constexpr float GA_START = 135.0f;      // lower-left  (cold)
constexpr float GA_SWEEP = 270.0f;      // clockwise to lower-right (hot)

// Output indicators: true = snowflake / heat-steam glyphs, false = "H"/"P".
constexpr bool USE_OUTPUT_ICONS = true;

constexpr const char *MENU_LABELS[MENU_COUNT] = {
    "Profile",   "Target",      "Mode",        "Cool on",   "Heat on",
    "Hold band", "Cooling off", "Cooling run", "Offset",    "Units",
    "Wi-Fi",     "MQTT",        "Heater test", "Pump test", "About",
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
  drawArcSeg(104, GA_START, GA_START + GA_SWEEP, COLOR_TRACK, 6);
  drawArcSeg(104, GA_START, GA_START + 60.0f, COLOR_COOL, 6);
  drawArcSeg(104, GA_START + GA_SWEEP - 60.0f, GA_START + GA_SWEEP,
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
  processInput(nowMs, settings);

  if (_screen != Screen::Main && nowMs - _lastActivityMs > UI_TIMEOUT_MS) {
    _screen = Screen::Main;
    _dirty = true;
  }

  const uint8_t activeBrightness = settings.brightness;
  if (!_dimmed && nowMs - _lastActivityMs > DISPLAY_DIM_MS) {
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

void DisplayUI::processInput(uint32_t nowMs, Settings &settings) {
  int32_t encoder = M5Dial.Encoder.read();
  int32_t delta = encoder - _lastEncoder;
  if (delta != 0) {
    _lastEncoder = encoder;
    markActivity(nowMs);
    handleEncoder(delta, settings);
  }

  handleTouch(nowMs, settings);

  if (M5Dial.BtnA.wasPressed()) {
    _pressStartedMs = nowMs;
    _longPressHandled = false;
    markActivity(nowMs);
  }

  if (M5Dial.BtnA.isPressed() && !_longPressHandled &&
      nowMs - _pressStartedMs > 800) {
    _longPressHandled = true;
    markActivity(nowMs);
    handleLongPress(settings);
  }

  if (M5Dial.BtnA.wasReleased() && !_longPressHandled) {
    markActivity(nowMs);
    handleShortPress(nowMs, settings);
  }
}

void DisplayUI::handleTouch(uint32_t nowMs, Settings &settings) {
  auto touch = M5Dial.Touch.getDetail();
  if (!touch.wasClicked()) {
    return;
  }

  markActivity(nowMs);

  const int16_t h = M5Dial.Display.height();
  if (_screen == Screen::Main) {
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
    _dirty = true;
    return;
  }

  if (_screen == Screen::Menu) {
    if (touch.y < h / 3) {
      handleEncoder(-MENU_ENCODER_DIVISOR, settings);
    } else if (touch.y > (h * 2) / 3) {
      handleEncoder(MENU_ENCODER_DIVISOR, settings);
    } else {
      handleShortPress(nowMs, settings);
    }
    return;
  }

  if (_screen == Screen::Edit) {
    if (touch.y > (h * 2) / 3) {
      handleShortPress(nowMs, settings);
    } else {
      handleEncoder(touch.x < M5Dial.Display.width() / 2 ? -EDIT_ENCODER_DIVISOR
                                                         : EDIT_ENCODER_DIVISOR,
                    settings);
    }
    return;
  }

  handleShortPress(nowMs, settings);
}

void DisplayUI::handleEncoder(int32_t delta, Settings &settings) {
  if (_screen == Screen::Main) {
    float stepC = settings.unitsFahrenheit ? deltaFToC(0.1f) : 0.1f;
    setActiveTargetC(settings, activeTargetC(settings) + (delta * stepC));
    _setpointFocusUntilMs = millis() + 3000;
    requestSave();
  } else if (_screen == Screen::Menu) {
    int32_t filteredDelta = filteredSettingsDelta(
        delta, _menuEncoderAccumulator, MENU_ENCODER_DIVISOR);
    if (filteredDelta == 0) {
      return;
    }
    int32_t next = static_cast<int32_t>(_menuIndex) + filteredDelta;
    while (next < 0) {
      next += MENU_COUNT;
    }
    _menuIndex = static_cast<uint8_t>(next % MENU_COUNT);
  } else if (_screen == Screen::Edit) {
    int32_t filteredDelta = filteredSettingsDelta(
        delta, _editEncoderAccumulator, EDIT_ENCODER_DIVISOR);
    if (filteredDelta == 0) {
      return;
    }
    editCurrentValue(filteredDelta, settings);
  }
  _dirty = true;
}

void DisplayUI::handleShortPress(uint32_t nowMs, Settings &settings) {
  if (_screen == Screen::Main) {
    settings.mode = nextMode(settings.mode);
    _toast = String("Mode: ") + modeText(settings.mode);
    _toastUntilMs = nowMs + 1500;
    requestSave();
  } else if (_screen == Screen::Menu) {
    if (_menuIndex <= MENU_OFFSET) {
      _editIndex = _menuIndex;
      _screen = Screen::Edit;
      resetSettingsEncoderFilters();
    } else if (_menuIndex == MENU_UNITS) {
      settings.unitsFahrenheit = !settings.unitsFahrenheit;
      _toast = String("Units: ") + temperatureUnit(settings.unitsFahrenheit);
      _toastUntilMs = nowMs + 1500;
      requestSave();
      resetSettingsEncoderFilters();
      _dirty = true;
      return;
    } else if (_menuIndex == MENU_WIFI) {
      _wifiSetupRequested = true;
      _toast = FERM_ENABLE_NETWORK ? "Setup AP active" : "Wi-Fi disabled";
      _toastUntilMs = nowMs + 3500;
    } else if (_menuIndex == MENU_MQTT) {
      _toast = FERM_ENABLE_NETWORK ? "MQTT not enabled" : "Network disabled";
      _toastUntilMs = nowMs + 2000;
    } else if (_menuIndex == MENU_HEATER_TEST || _menuIndex == MENU_PUMP_TEST) {
      _screen = Screen::ConfirmTest;
      resetSettingsEncoderFilters();
    } else if (_menuIndex == MENU_ABOUT) {
      _screen = Screen::About;
      resetSettingsEncoderFilters();
    }
  } else if (_screen == Screen::Edit) {
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
    requestSave();
  } else if (_screen == Screen::ConfirmTest) {
    _pendingOutputTest =
        _menuIndex == MENU_HEATER_TEST ? OutputTestKind::Heater
                                       : OutputTestKind::Pump;
    _screen = Screen::Main;
    resetSettingsEncoderFilters();
  } else if (_screen == Screen::About) {
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
  }
  _dirty = true;
}

void DisplayUI::handleLongPress(Settings &settings) {
  (void)settings;
  if (_screen == Screen::Main) {
    _screen = Screen::Menu;
  } else {
    _screen = Screen::Main;
  }
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::editCurrentValue(int32_t delta, Settings &settings) {
  switch (_editIndex) {
  case 0: {
    int32_t profile =
        static_cast<int32_t>(settings.activeProfile) + (delta > 0 ? 1 : -1);
    while (profile < 0) {
      profile += PROFILE_COUNT;
    }
    settings.activeProfile = static_cast<uint8_t>(profile % PROFILE_COUNT);
    break;
  }
  case 1:
    setActiveTargetC(
        settings,
        activeTargetC(settings) +
            (delta * (settings.unitsFahrenheit ? deltaFToC(0.1f) : 0.1f)));
    break;
  case 2: {
    int32_t mode = static_cast<int32_t>(settings.mode) + (delta > 0 ? 1 : -1);
    while (mode < 0) {
      mode += 4;
    }
    settings.mode = static_cast<UserMode>(mode % 4);
    break;
  }
  case 3:
    settings.coolOnDeltaC = clampFloat(
        settings.coolOnDeltaC +
            (delta * (settings.unitsFahrenheit ? deltaFToC(0.1f) : 0.1f)),
        MIN_DELTA_C, MAX_DELTA_C);
    break;
  case 4:
    settings.heatOnDeltaC = clampFloat(
        settings.heatOnDeltaC +
            (delta * (settings.unitsFahrenheit ? deltaFToC(0.1f) : 0.1f)),
        MIN_DELTA_C, MAX_DELTA_C);
    break;
  case 5:
    settings.holdDeltaC = clampFloat(
        settings.holdDeltaC +
            (delta * (settings.unitsFahrenheit ? deltaFToC(0.1f) : 0.1f)),
        MIN_DELTA_C, MAX_DELTA_C);
    if (settings.holdDeltaC > settings.coolOnDeltaC) {
      settings.holdDeltaC = settings.coolOnDeltaC;
    }
    if (settings.holdDeltaC > settings.heatOnDeltaC) {
      settings.holdDeltaC = settings.heatOnDeltaC;
    }
    break;
  case 6: {
    int32_t next =
        static_cast<int32_t>(settings.pumpMinOffSeconds) + (delta * 5);
    if (next < 0) {
      next = 0;
    }
    settings.pumpMinOffSeconds = clampU32(static_cast<uint32_t>(next), 0, 1800);
    break;
  }
  case 7: {
    int32_t next =
        static_cast<int32_t>(settings.pumpMinRunSeconds) + (delta * 5);
    if (next < 0) {
      next = 0;
    }
    settings.pumpMinRunSeconds = clampU32(static_cast<uint32_t>(next), 0, 600);
    break;
  }
  case 8:
    settings.tempOffsetC = clampFloat(
        settings.tempOffsetC +
            (delta * (settings.unitsFahrenheit ? deltaFToC(0.1f) : 0.1f)),
        MIN_OFFSET_C, MAX_OFFSET_C);
    break;
  case 9:
    settings.unitsFahrenheit = !settings.unitsFahrenheit;
    break;
  default:
    break;
  }
}

int32_t DisplayUI::filteredSettingsDelta(int32_t delta, int32_t &accumulator,
                                         int32_t divisor) {
  accumulator += delta;
  int32_t filteredDelta = accumulator / divisor;
  if (filteredDelta != 0) {
    accumulator -= filteredDelta * divisor;
  }
  return filteredDelta;
}

void DisplayUI::resetSettingsEncoderFilters() {
  _menuEncoderAccumulator = 0;
  _editEncoderAccumulator = 0;
}

void DisplayUI::requestSave() { _saveRequested = true; }

void DisplayUI::markActivity(uint32_t nowMs) {
  _lastActivityMs = nowMs;
  if (_dimmed) {
    _dimmed = false;
    _appliedBrightness = 0;  // force re-apply at configured brightness next update
  }
}

void DisplayUI::draw(uint32_t nowMs, const Settings &settings,
                     const UiModel &model) {
  if (_screen != Screen::Main) {
    useDefaultFont();
  }
  _canvas.fillScreen(COLOR_BG);
  if (_screen == Screen::Main) {
    drawMain(nowMs, settings, model);
  } else if (_screen == Screen::Menu) {
    drawMenu(settings, model.network);
  } else if (_screen == Screen::Edit) {
    drawEdit(settings);
  } else if (_screen == Screen::ConfirmTest) {
    drawConfirmTest();
  } else {
    drawAbout();
  }

  if (_toast.length() > 0 && nowMs < _toastUntilMs) {
    _canvas.setTextDatum(bottom_center);
    _canvas.setTextSize(1);
    _canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
    _canvas.drawString(_toast, _canvas.width() / 2, _canvas.height() - 10,
                       &fonts::Font0);
  }

  _canvas.pushSprite(0, 0);
}

void DisplayUI::drawMain(uint32_t nowMs, const Settings &settings,
                         const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;
  const uint16_t bg = COLOR_BG;  // flat face; state reads from the arc colour
  const bool editing = nowMs < _setpointFocusUntilMs;
  const bool fault = model.runtimeState == RuntimeState::Fault;
  const bool unitsF = settings.unitsFahrenheit;
  const float targetC = activeTargetC(settings);
  uint16_t accent = stateColor(model.runtimeState, model.faultCode);
  if (!fault && model.runtimeState == RuntimeState::Cooling &&
      activeProfileIndex(settings) == static_cast<uint8_t>(ProfileSlot::Crash)) {
    accent = COLOR_CRASH;  // brighter blue while cold-crashing
  }

  _canvas.fillScreen(bg);

  // 1. unlit gauge track + 5 C scale ticks (absolute thermometer)
  drawArcSeg(GAUGE_R, GA_START, GA_START + GA_SWEEP, COLOR_TRACK, GAUGE_W);
  for (int tc = 5; tc <= 30; tc += 5) {
    drawTick(GAUGE_R - 4, GAUGE_R + 4, gaugeAngle(static_cast<float>(tc)),
             COLOR_TICK, 2);
  }

  // 2. hold band around the target
  drawArcSeg(GAUGE_R, gaugeAngle(targetC - 0.4f), gaugeAngle(targetC + 0.4f),
             COLOR_OK, GAUGE_W);

  // 3. lit thermometer fill from the cold end up to the current temperature
  if (model.tempValid && !fault) {
    float aNow = gaugeAngle(model.tempC);
    drawArcSeg(GAUGE_R, GA_START, aNow, accent, GAUGE_W);
    drawMarker(GAUGE_R, aNow, TFT_WHITE, accent);
  }

  // 4. setpoint tick (gold; emphasised while turning the dial)
  float aTgt = gaugeAngle(targetC);
  drawTick(GAUGE_R - 13, GAUGE_R + 9, aTgt, COLOR_GOLD, editing ? 5 : 3);
  if (editing) {
    drawMarker(GAUGE_R, aTgt, COLOR_GOLD, COLOR_GOLD);
  }

  // 5. Wi-Fi status, then the centre stack
  drawWifiIcon(cx, cy - 82, 11, wifiStatus(model.network), bg);

  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(editing ? COLOR_GOLD : COLOR_TEXT_MUTED, bg);
  _canvas.drawString(activeProfile(settings).name, cx, cy - 52, &fonts::DejaVu18);

  const float bigC = editing ? targetC : model.tempC;
  const String big =
      (editing || model.tempValid) ? temperatureNumber(bigC, unitsF) : "--.-";
  // Centre the number under the profile label; hang the unit off its real
  // right edge (textWidth) so it never shifts the number off-centre.
  const bool largeFontLoaded = ensureLargeFont();
  if (!largeFontLoaded) {
    _canvas.setFont(&fonts::FreeSansBold24pt7b);
  }
  const int16_t numHalf = _canvas.textWidth(big) / 2;
  _canvas.setTextColor(TFT_WHITE, bg);
  _canvas.drawString(big, cx, cy - 6);
  drawTemperatureUnit(cx + numHalf + 6, cy - 16, unitsF, COLOR_TEXT_MUTED, bg);
  _canvas.setTextDatum(middle_center);

  // 6. state line
  String stateLine;
  uint16_t stateCol;
  if (editing) {
    stateLine = "SET TARGET";
    stateCol = COLOR_GOLD;
  } else if (fault) {
    stateLine = faultText(model.faultCode);
    stateCol = COLOR_FAULT;
  } else if (model.outputTestActive) {
    stateLine = model.outputTestKind == OutputTestKind::Heater ? "TEST HEATER"
                                                               : "TEST PUMP";
    stateCol = COLOR_WARN;
  } else if (model.runtimeState == RuntimeState::Idle) {
    stateLine = "IN RANGE";  // friendlier than the canonical "IDLE"
    stateCol = accent;
  } else {
    stateLine = profileRuntimeText(settings, model.runtimeState);
    stateCol = accent;
  }
  _canvas.setTextColor(stateCol, bg);
  _canvas.drawString(stateLine, cx, cy + 34, &fonts::FreeSansBold12pt7b);

  // 7. fermenter name in the bottom gap
  if (!editing) {
    _canvas.setTextColor(COLOR_TEXT_MUTED, bg);
    _canvas.drawString(settings.fermenterName, cx, cy + 56, &fonts::DejaVu12);
  }
  drawOutputChips(model);
}

bool DisplayUI::ensureLargeFont() {
  if (_largeFontLoaded) {
    return true;
  }

  _largeFontLoaded = _canvas.loadFont(fermentdial_dejavu_sans_bold_44_vlw);
  return _largeFontLoaded;
}

void DisplayUI::useDefaultFont() {
  _canvas.setFont(&fonts::Font0);
  _canvas.setTextSize(1);
  _largeFontLoaded = false;
}

void DisplayUI::drawTemperatureUnit(int16_t x, int16_t y, bool unitsFahrenheit,
                                    uint16_t color, uint16_t bg) {
  _canvas.setTextSize(1);
  _canvas.setTextDatum(middle_left);
  _canvas.drawCircle(x + 3, y - 6, 2, color);
  _canvas.setTextColor(color, bg);
  _canvas.drawString(temperatureUnit(unitsFahrenheit), x + 8, y,
                     &fonts::DejaVu12);
}

void DisplayUI::drawTargetTemperature(int16_t cx, int16_t y, float tempC,
                                      bool unitsFahrenheit, uint16_t color,
                                      uint16_t bg) {
  const String label = String("target ") + temperatureNumber(tempC, unitsFahrenheit);
  const int16_t unitWidth =
      8 + _canvas.textWidth(temperatureUnit(unitsFahrenheit), &fonts::DejaVu12);
  const int16_t totalWidth = _canvas.textWidth(label, &fonts::DejaVu12) + unitWidth;
  const int16_t x = cx - totalWidth / 2;

  _canvas.setTextDatum(middle_left);
  _canvas.setTextColor(color, bg);
  _canvas.drawString(label, x, y, &fonts::DejaVu12);
  drawTemperatureUnit(x + _canvas.textWidth(label, &fonts::DejaVu12), y,
                      unitsFahrenheit, color, bg);
  _canvas.setTextDatum(middle_center);
}

float DisplayUI::gaugeAngle(float tempC) const {
  float frac = (tempC - GAUGE_MIN_C) / (GAUGE_MAX_C - GAUGE_MIN_C);
  if (frac < 0.0f) {
    frac = 0.0f;
  }
  if (frac > 1.0f) {
    frac = 1.0f;
  }
  return GA_START + frac * GA_SWEEP;
}

void DisplayUI::polar(int16_t r, float deg, int16_t &x, int16_t &y) const {
  float rad = deg * DEG_TO_RAD;
  x = _canvas.width() / 2 + static_cast<int16_t>(lroundf(r * cosf(rad)));
  y = _canvas.height() / 2 + static_cast<int16_t>(lroundf(r * sinf(rad)));
}

void DisplayUI::drawArcSeg(int16_t r, float a0, float a1, uint16_t color,
                           int16_t width) {
  if (a1 - a0 < 0.4f) {
    return;
  }
  const int16_t rad = width / 2;
  for (float a = a0; a <= a1; a += 1.0f) {
    int16_t x, y;
    polar(r, a, x, y);
    _canvas.fillSmoothCircle(x, y, rad, color);  // anti-aliased edge
  }
}

void DisplayUI::drawMarker(int16_t r, float deg, uint16_t fill,
                           uint16_t outline) {
  int16_t x, y;
  polar(r, deg, x, y);
  _canvas.fillSmoothCircle(x, y, 5, outline);
  _canvas.fillSmoothCircle(x, y, 3, fill);
}

void DisplayUI::drawTick(int16_t r0, int16_t r1, float deg, uint16_t color,
                         int16_t width) {
  int16_t x0, y0, x1, y1;
  polar(r0, deg, x0, y0);
  polar(r1, deg, x1, y1);
  _canvas.drawWideLine(x0, y0, x1, y1, width * 0.5f, color);  // r = half width
}

uint8_t DisplayUI::wifiStatus(const NetworkSnapshot &network) const {
  if (!network.wifiEnabled) {
    return 0;
  }
  if (network.wifiConnected) {
    return 1;
  }
  if (network.status == "Setup AP") {
    return 2;
  }
  return 0;
}

void DisplayUI::drawWifiIcon(int16_t cx, int16_t cy, int16_t size,
                             uint8_t status, uint16_t bg) {
  const uint16_t col = status == 1 ? COLOR_COOL
                       : status == 2 ? COLOR_GOLD
                                     : COLOR_WIFI_OFF;
  const int16_t by = cy + static_cast<int16_t>(lroundf(0.55f * size));
  const int16_t dot = max(2, static_cast<int>(lroundf(0.16f * size)));
  _canvas.fillSmoothCircle(cx, by, dot, col);

  const float radii[3] = {0.5f, 0.85f, 1.2f};
  for (int i = 0; i < 3; i++) {  // each fan band as a smooth AA polyline
    const float rr = radii[i] * size;
    int16_t px = 0, py = 0;
    bool first = true;
    for (float a = 222.0f; a <= 318.0f; a += 8.0f) {  // upward-opening fan
      float rad = a * DEG_TO_RAD;
      int16_t x = cx + static_cast<int16_t>(lroundf(rr * cosf(rad)));
      int16_t y = by + static_cast<int16_t>(lroundf(rr * sinf(rad)));
      if (!first) {
        _canvas.drawWideLine(px, py, x, y, 0.9f, col);
      }
      px = x;
      py = y;
      first = false;
    }
  }

  if (status == 0) {  // Material wifi_off slash, knocked out of the fan
    const int16_t off = static_cast<int16_t>(lroundf(0.78f * size));
    _canvas.drawWideLine(cx - off, cy + off, cx + off, cy - off, 2.6f, bg);
    _canvas.drawWideLine(cx - off, cy + off, cx + off, cy - off, 1.0f, COLOR_FAULT);
  }
}

void DisplayUI::drawHeatIcon(int16_t cx, int16_t cy, int16_t size,
                             uint16_t color) {
  const float amp = 0.26f * size, half = 0.98f * size;
  for (int c = -1; c <= 1; c++) {
    const float x0 = cx + c * 0.52f * size;
    int16_t px = 0, py = 0;
    for (int i = 0; i <= 10; i++) {
      float t = i / 10.0f;
      int16_t x = static_cast<int16_t>(lroundf(x0 + amp * sinf(t * 2.4f * PI)));
      int16_t y = static_cast<int16_t>(lroundf(cy - half + t * 2.0f * half));
      if (i > 0) {
        _canvas.drawWideLine(px, py, x, y, 0.9f, color);
      }
      px = x;
      py = y;
    }
  }
}

void DisplayUI::drawSnowflake(int16_t cx, int16_t cy, int16_t size,
                              uint16_t color) {
  for (int k = 0; k < 6; k++) {
    const float a = k * 60.0f * DEG_TO_RAD;
    const float ca = cosf(a), sa = sinf(a);
    int16_t ex = cx + static_cast<int16_t>(lroundf(size * ca));
    int16_t ey = cy + static_cast<int16_t>(lroundf(size * sa));
    _canvas.drawWideLine(cx, cy, ex, ey, 0.8f, color);
    int16_t bxr = cx + static_cast<int16_t>(lroundf(0.58f * size * ca));
    int16_t byr = cy + static_cast<int16_t>(lroundf(0.58f * size * sa));
    for (int s = -1; s <= 1; s += 2) {
      const float da = s * 38.0f * DEG_TO_RAD;
      int16_t bx = bxr + static_cast<int16_t>(lroundf(0.34f * size * cosf(a + da)));
      int16_t by = byr + static_cast<int16_t>(lroundf(0.34f * size * sinf(a + da)));
      _canvas.drawWideLine(bxr, byr, bx, by, 0.8f, color);
    }
  }
}

void DisplayUI::drawOutputChips(const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t y = _canvas.height() / 2 + 78;
  // Demo mode forces the physical relays off, so fall back to the controller's
  // intent (runtime state) to keep the chips demonstrable. Real mode shows the
  // actual relay state.
  const bool heatOn =
      model.heaterOn ||
      (model.demoSensor && model.runtimeState == RuntimeState::Heating);
  const bool coolOn =
      model.pumpOn ||
      (model.demoSensor && model.runtimeState == RuntimeState::Cooling);
  const int16_t dxs[2] = {-29, 29};
  const bool on[2] = {heatOn, coolOn};
  const uint16_t col[2] = {COLOR_HEAT, COLOR_COOL};
  for (int i = 0; i < 2; i++) {
    int16_t x = cx + dxs[i];
    uint16_t chipCol = on[i] ? col[i] : COLOR_PANEL;
    _canvas.fillSmoothCircle(x, y, 13, chipCol);
    uint16_t glyph = on[i] ? TFT_WHITE : COLOR_TEXT_MUTED;
    if (!USE_OUTPUT_ICONS) {
      _canvas.setTextDatum(middle_center);
      _canvas.setTextColor(glyph, chipCol);
      _canvas.drawString(i == 0 ? "H" : "P", x, y, &fonts::Font2);
    } else if (i == 0) {
      drawHeatIcon(x, y, 7, glyph);
    } else {
      drawSnowflake(x, y, 8, glyph);
    }
  }
  if (model.demoSensor) {
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(COLOR_GOLD, COLOR_BG);
    _canvas.drawString("DEMO", cx, y + 20, &fonts::DejaVu12);
  }
}

void DisplayUI::drawMenu(const Settings &settings,
                         const NetworkSnapshot &network) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  // position dots around the arc — where this item sits in the list
  for (int i = 0; i < MENU_COUNT; i++) {
    const float a =
        GA_START + (MENU_COUNT > 1 ? static_cast<float>(i) / (MENU_COUNT - 1)
                                   : 0.0f) *
                       GA_SWEEP;
    int16_t x, y;
    polar(104, a, x, y);
    const bool cur = i == _menuIndex;
    _canvas.fillSmoothCircle(x, y, cur ? 4 : 2, cur ? COLOR_GOLD : COLOR_TRACK);
  }

  const int prev = _menuIndex == 0 ? MENU_COUNT - 1 : _menuIndex - 1;
  const int next = (_menuIndex + 1) % MENU_COUNT;

  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(COLOR_BLUE, COLOR_BG);
  _canvas.drawString("SETTINGS", cx, cy - 74, &fonts::DejaVu18);

  _canvas.setTextColor(rgb565(86, 106, 118), COLOR_BG);  // dim neighbours
  _canvas.drawString(MENU_LABELS[prev], cx, cy - 44, &fonts::DejaVu12);
  _canvas.drawString(MENU_LABELS[next], cx, cy + 48, &fonts::DejaVu12);

  // focused item card
  _canvas.fillSmoothRoundRect(cx - 94, cy - 26, 188, 52, 14, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 94, cy - 26, 188, 52, 14, COLOR_BLUE);
  _canvas.setTextColor(TFT_WHITE, COLOR_PANEL);
  _canvas.drawString(MENU_LABELS[_menuIndex], cx, cy - 9,
                     &fonts::FreeSansBold12pt7b);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL);
  _canvas.drawString(menuValue(_menuIndex, settings, network), cx, cy + 13,
                     &fonts::DejaVu18);

  const char *hint = "tap to select";
  if (_menuIndex == MENU_UNITS) {
    hint = "tap to toggle";
  } else if (_menuIndex == MENU_WIFI) {
    hint = network.status == "Setup AP" ? "AP is active" : "tap to start AP";
  } else if (_menuIndex == MENU_MQTT) {
    hint = FERM_ENABLE_NETWORK ? "not enabled yet" : "network disabled";
  }
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(hint, cx, cy + 78, &fonts::DejaVu12);
}

void DisplayUI::drawEdit(const Settings &settings) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(COLOR_BLUE, COLOR_BG);
  _canvas.drawString(MENU_LABELS[_editIndex], cx, cy - 58, &fonts::DejaVu18);

  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(menuValue(_editIndex, settings), cx, cy - 6,
                     &fonts::FreeSansBold18pt7b);

  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString((_editIndex == 0 || _editIndex == 2) ? "rotate to choose"
                                                          : "rotate to change",
                     cx, cy + 36, &fonts::DejaVu12);

  _canvas.fillSmoothRoundRect(cx - 48, cy + 56, 96, 30, 15, COLOR_BLUE);
  _canvas.setTextColor(TFT_WHITE, COLOR_BLUE);
  _canvas.drawString("Save", cx, cy + 71, &fonts::DejaVu18);
}

void DisplayUI::drawConfirmTest() {
  _canvas.setTextDatum(middle_center);
  _canvas.setTextSize(2);
  _canvas.setTextColor(COLOR_WARN, COLOR_BG);
  _canvas.drawString("Confirm test", _canvas.width() / 2, 61);
  _canvas.setTextSize(1);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(_menuIndex == MENU_HEATER_TEST ? "Heater ON for 5 sec"
                                                    : "Pump ON for 5 sec",
                     _canvas.width() / 2, 108);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString("Mode cannot be OFF", _canvas.width() / 2, 134);
  drawPill(58, 160, 124, 28, COLOR_WARN, COLOR_WARN, "Press to start",
           TFT_BLACK, 1);
}

void DisplayUI::drawAbout() {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  drawArcSeg(104, GA_START, GA_START + GA_SWEEP, COLOR_TRACK, 5);
  drawArcSeg(104, GA_START, GA_START + 60.0f, COLOR_COOL, 5);
  drawArcSeg(104, GA_START + GA_SWEEP - 60.0f, GA_START + GA_SWEEP,
             COLOR_HEAT, 5);

  _canvas.fillSmoothRoundRect(cx - 90, cy - 43, 180, 86, 14, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 90, cy - 43, 180, 86, 14, COLOR_BLUE);

  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(COLOR_ACCENT, COLOR_PANEL);
  _canvas.drawString(FIRMWARE_NAME, cx, cy - 22, &fonts::DejaVu18);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL);
  _canvas.drawString(String("Version ") + FIRMWARE_VERSION, cx, cy + 6,
                     &fonts::DejaVu12);
  _canvas.setTextColor(COLOR_TEXT, COLOR_PANEL);
  _canvas.drawString("M5Stack Dial / ESP32-S3", cx, cy + 30,
                     &fonts::DejaVu12);
}

void DisplayUI::drawPill(int16_t x, int16_t y, int16_t w, int16_t h,
                         uint16_t fill, uint16_t outline, const String &text,
                         uint16_t textColor, uint8_t textSize) {
  _canvas.fillRoundRect(x, y, w, h, h / 2, fill);
  _canvas.drawRoundRect(x, y, w, h, h / 2, outline);
  _canvas.setTextDatum(middle_center);
  _canvas.setTextSize(textSize);
  _canvas.setTextColor(textColor, fill);
  _canvas.drawString(text, x + w / 2, y + h / 2);
}

String DisplayUI::temperatureNumber(float tempC, bool unitsFahrenheit) const {
  if (isnan(tempC)) {
    return "--.-";
  }
  return String(unitsFahrenheit ? cToF(tempC) : tempC, 1);
}

const char *DisplayUI::temperatureUnit(bool unitsFahrenheit) const {
  return unitsFahrenheit ? "F" : "C";
}

String DisplayUI::formatTemperature(float tempC, bool unitsFahrenheit) const {
  if (isnan(tempC)) {
    return "--.-";
  }
  float displayed = unitsFahrenheit ? cToF(tempC) : tempC;
  return String(displayed, 1) + temperatureUnit(unitsFahrenheit);
}

String DisplayUI::menuValue(uint8_t index, const Settings &settings,
                            const NetworkSnapshot &network) const {
  switch (index) {
  case 0:
    return activeProfile(settings).name;
  case 1:
    return formatTemperature(activeTargetC(settings), settings.unitsFahrenheit);
  case 2:
    return modeText(settings.mode);
  case 3:
    return String(settings.unitsFahrenheit ? deltaCToF(settings.coolOnDeltaC)
                                           : settings.coolOnDeltaC,
                  1) +
           temperatureUnit(settings.unitsFahrenheit);
  case 4:
    return String(settings.unitsFahrenheit ? deltaCToF(settings.heatOnDeltaC)
                                           : settings.heatOnDeltaC,
                  1) +
           temperatureUnit(settings.unitsFahrenheit);
  case 5:
    return String(settings.unitsFahrenheit ? deltaCToF(settings.holdDeltaC)
                                           : settings.holdDeltaC,
                  1) +
           temperatureUnit(settings.unitsFahrenheit);
  case 6:
    return String(settings.pumpMinOffSeconds) + "s";
  case 7:
    return String(settings.pumpMinRunSeconds) + "s";
  case 8:
    return String(settings.unitsFahrenheit ? deltaCToF(settings.tempOffsetC)
                                           : settings.tempOffsetC,
                  1) +
           temperatureUnit(settings.unitsFahrenheit);
  case 9:
    return temperatureUnit(settings.unitsFahrenheit);
  case 10:
    if (!network.wifiEnabled) {
      return "Disabled";
    }
    return network.status;
  case 11:
    return FERM_ENABLE_NETWORK ? "Enabled" : "Offline";
  case 12:
  case 13:
    return "5s";
  case 14:
    return FIRMWARE_VERSION;
  default:
    return "";
  }
}

uint16_t DisplayUI::stateColor(RuntimeState state, FaultCode fault) const {
  if (fault != FaultCode::None || state == RuntimeState::Fault) {
    return COLOR_FAULT;
  }
  switch (state) {
  case RuntimeState::Heating:
    return COLOR_HEAT;
  case RuntimeState::Cooling:
    return COLOR_COOL;
  case RuntimeState::Idle:
    return COLOR_OK;
  case RuntimeState::Off:
    return TFT_DARKGREY;
  default:
    return COLOR_TEXT_MUTED;
  }
}

uint16_t DisplayUI::stateBackground(RuntimeState state, FaultCode fault) const {
  if (fault != FaultCode::None || state == RuntimeState::Fault) {
    return 0x4800;
  }
  switch (state) {
  case RuntimeState::Heating:
    return 0x5002;
  case RuntimeState::Cooling:
    return 0x0230;
  case RuntimeState::Off:
    return 0x0861;
  default:
    return COLOR_BG;
  }
}

} // namespace ferm
