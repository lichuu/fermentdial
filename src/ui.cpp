#include "ui.h"

namespace ferm {

namespace {

constexpr uint8_t MENU_COUNT = 12;
constexpr uint16_t COLOR_BG = 0x0841;
constexpr uint16_t COLOR_PANEL = 0x10A2;
constexpr uint16_t COLOR_PANEL_DARK = 0x0861;
constexpr uint16_t COLOR_TEXT_MUTED = 0xBDF7;
constexpr uint16_t COLOR_BLUE = 0x059F;
constexpr uint16_t COLOR_HEAT = 0xF9A6;
constexpr uint16_t COLOR_COOL = 0x06BF;
constexpr uint16_t COLOR_OK = 0x45E8;
constexpr uint16_t COLOR_WARN = 0xFDE0;
constexpr uint16_t COLOR_FAULT = 0xD945;
constexpr int32_t MENU_ENCODER_DIVISOR = 2;
constexpr int32_t EDIT_ENCODER_DIVISOR = 2;

constexpr const char *MENU_LABELS[MENU_COUNT] = {
    "Target",
    "Mode",
    "Hysteresis",
    "Pump off",
    "Pump run",
    "Offset",
    "Units",
    "Wi-Fi",
    "MQTT",
    "Heater test",
    "Pump test",
    "About",
};

}  // namespace

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

  _canvas.fillScreen(COLOR_BLUE);
  _canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  _canvas.setTextSize(2);
  _canvas.drawString(FIRMWARE_NAME, _canvas.width() / 2, _canvas.height() / 2 - 18);
  _canvas.setTextSize(1);
  _canvas.drawString(String("v") + FIRMWARE_VERSION, _canvas.width() / 2, _canvas.height() / 2 + 18);
  _canvas.pushSprite(0, 0);
}

void DisplayUI::update(uint32_t nowMs, Settings &settings, const UiModel &model) {
  M5Dial.update();
  processInput(nowMs, settings);

  if (_screen != Screen::Main && nowMs - _lastActivityMs > UI_TIMEOUT_MS) {
    _screen = Screen::Main;
    _dirty = true;
  }

  if (!_dimmed && nowMs - _lastActivityMs > DISPLAY_DIM_MS) {
    M5Dial.Display.setBrightness(60);
    _dimmed = true;
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

  if (M5Dial.BtnA.isPressed() && !_longPressHandled && nowMs - _pressStartedMs > 800) {
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
      handleEncoder(touch.x < M5Dial.Display.width() / 2 ? -EDIT_ENCODER_DIVISOR : EDIT_ENCODER_DIVISOR, settings);
    }
    return;
  }

  handleShortPress(nowMs, settings);
}

void DisplayUI::handleEncoder(int32_t delta, Settings &settings) {
  if (_screen == Screen::Main) {
    float stepF = settings.unitsFahrenheit ? 0.1f : (0.1f * 9.0f / 5.0f);
    settings.targetF = clampFloat(settings.targetF + (delta * stepF), MIN_TARGET_F, MAX_TARGET_F);
    _setpointFocusUntilMs = millis() + 3000;
    requestSave();
  } else if (_screen == Screen::Menu) {
    int32_t filteredDelta = filteredSettingsDelta(delta, _menuEncoderAccumulator, MENU_ENCODER_DIVISOR);
    if (filteredDelta == 0) {
      return;
    }
    int32_t next = static_cast<int32_t>(_menuIndex) + filteredDelta;
    while (next < 0) {
      next += MENU_COUNT;
    }
    _menuIndex = static_cast<uint8_t>(next % MENU_COUNT);
  } else if (_screen == Screen::Edit) {
    int32_t filteredDelta = filteredSettingsDelta(delta, _editEncoderAccumulator, EDIT_ENCODER_DIVISOR);
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
    if (_menuIndex <= 6) {
      if (_menuIndex == 6) {
        settings.unitsFahrenheit = !settings.unitsFahrenheit;
        _toast = String("Units: ") + (settings.unitsFahrenheit ? "F" : "C");
        _toastUntilMs = nowMs + 1500;
        requestSave();
        resetSettingsEncoderFilters();
        _dirty = true;
        return;
      }
      _editIndex = _menuIndex;
      _screen = Screen::Edit;
      resetSettingsEncoderFilters();
    } else if (_menuIndex == 7) {
      _wifiSetupRequested = true;
      _toast = "Starting setup AP";
      _toastUntilMs = nowMs + 2000;
    } else if (_menuIndex == 9 || _menuIndex == 10) {
      _screen = Screen::ConfirmTest;
      resetSettingsEncoderFilters();
    } else if (_menuIndex == 11) {
      _screen = Screen::About;
      resetSettingsEncoderFilters();
    }
  } else if (_screen == Screen::Edit) {
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
    requestSave();
  } else if (_screen == Screen::ConfirmTest) {
    _pendingOutputTest = _menuIndex == 9 ? OutputTestKind::Heater : OutputTestKind::Pump;
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
    case 0:
      settings.targetF = clampFloat(settings.targetF + (delta * 0.1f), MIN_TARGET_F, MAX_TARGET_F);
      break;
    case 1: {
      int32_t mode = static_cast<int32_t>(settings.mode) + (delta > 0 ? 1 : -1);
      while (mode < 0) {
        mode += 4;
      }
      settings.mode = static_cast<UserMode>(mode % 4);
      break;
    }
    case 2:
      settings.hysteresisF = clampFloat(settings.hysteresisF + (delta * 0.1f), MIN_HYSTERESIS_F, MAX_HYSTERESIS_F);
      break;
    case 3:
    {
      int32_t next = static_cast<int32_t>(settings.pumpMinOffSeconds) + (delta * 5);
      if (next < 0) {
        next = 0;
      }
      settings.pumpMinOffSeconds = clampU32(static_cast<uint32_t>(next), 0, 1800);
      break;
    }
    case 4:
    {
      int32_t next = static_cast<int32_t>(settings.pumpMinRunSeconds) + (delta * 5);
      if (next < 0) {
        next = 0;
      }
      settings.pumpMinRunSeconds = clampU32(static_cast<uint32_t>(next), 0, 600);
      break;
    }
    case 5:
      settings.tempOffsetF = clampFloat(settings.tempOffsetF + (delta * 0.1f), MIN_OFFSET_F, MAX_OFFSET_F);
      break;
    case 6:
      settings.unitsFahrenheit = !settings.unitsFahrenheit;
      break;
    default:
      break;
  }
}

int32_t DisplayUI::filteredSettingsDelta(int32_t delta, int32_t &accumulator, int32_t divisor) {
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

void DisplayUI::requestSave() {
  _saveRequested = true;
}

void DisplayUI::markActivity(uint32_t nowMs) {
  _lastActivityMs = nowMs;
  if (_dimmed) {
    M5Dial.Display.setBrightness(255);
    _dimmed = false;
  }
}

void DisplayUI::draw(uint32_t nowMs, const Settings &settings, const UiModel &model) {
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
    _canvas.drawString(_toast, _canvas.width() / 2, _canvas.height() - 10);
  }

  _canvas.pushSprite(0, 0);
}

void DisplayUI::drawMain(uint32_t nowMs, const Settings &settings, const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;
  const uint16_t accent = stateColor(model.runtimeState, model.faultCode);
  const uint16_t bg = stateBackground(model.runtimeState, model.faultCode);

  _canvas.fillScreen(bg);
  _canvas.fillCircle(cx, cy, min(cx, cy) - 2, bg);
  _canvas.drawCircle(cx, cy, min(cx, cy) - 3, accent);
  _canvas.drawCircle(cx, cy, min(cx, cy) - 7, COLOR_PANEL_DARK);

  const bool showingSetpoint = nowMs < _setpointFocusUntilMs;
  String topText = showingSetpoint ? "SETPOINT" : modeText(settings.mode);
  drawPill(50, 16, 140, 24, COLOR_PANEL_DARK, accent, topText, TFT_WHITE, 1);

  if (model.runtimeState == RuntimeState::Fault) {
    drawPill(34, 64, 172, 88, COLOR_PANEL_DARK, COLOR_FAULT, faultText(model.faultCode), TFT_WHITE, 1);
    _canvas.setTextDatum(middle_center);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL_DARK);
    _canvas.drawString("Outputs OFF", cx, 132);
  } else {
    const float largeTempF = showingSetpoint ? settings.targetF : model.tempF;
    _canvas.setTextDatum(middle_center);
    _canvas.setTextSize(1);
    _canvas.setTextColor(TFT_WHITE, bg);
    _canvas.drawString((showingSetpoint || model.tempValid) ? temperatureNumber(largeTempF, settings.unitsFahrenheit) : "--.-",
                       cx, 90, 7);

    const String smallValue = showingSetpoint
                                  ? String("Now ") + (model.tempValid ? formatTemperature(model.tempF, settings.unitsFahrenheit) : "--.-")
                                  : String("Target ") + formatTemperature(settings.targetF, settings.unitsFahrenheit);
    drawPill(52, 128, 136, 28, COLOR_PANEL, COLOR_PANEL, smallValue, TFT_WHITE, 1);
  }

  String state = model.outputTestActive ? (model.outputTestKind == OutputTestKind::Heater ? "TEST HEATER" : "TEST PUMP")
                                        : stateText(model.runtimeState);
  drawPill(64, 164, 112, 26, accent, accent, state, TFT_BLACK, 1);

  if (model.demoSensor) {
    _canvas.setTextSize(1);
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(COLOR_TEXT_MUTED, bg);
    _canvas.drawString("demo sensor", cx, 199);
    _canvas.drawString("outputs disabled", cx, 214);
  } else {
    _canvas.setTextSize(1);
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(COLOR_TEXT_MUTED, bg);
    _canvas.drawString(model.network.wifiConnected ? "WiFi" : "--", cx, 205);
  }
}

void DisplayUI::drawMenu(const Settings &settings, const NetworkSnapshot &network) {
  const int16_t cx = _canvas.width() / 2;
  _canvas.fillCircle(cx, _canvas.height() / 2, 112, COLOR_BG);
  drawPill(62, 18, 116, 24, COLOR_PANEL_DARK, COLOR_BLUE, "SETTINGS", TFT_WHITE, 1);

  int prev = _menuIndex == 0 ? MENU_COUNT - 1 : _menuIndex - 1;
  int next = (_menuIndex + 1) % MENU_COUNT;

  _canvas.setTextDatum(middle_center);
  _canvas.setTextSize(1);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(MENU_LABELS[prev], cx, 63);
  _canvas.drawString(MENU_LABELS[next], cx, 174);

  drawPill(36, 86, 168, 62, COLOR_PANEL, COLOR_BLUE, MENU_LABELS[_menuIndex], TFT_WHITE, 2);
  _canvas.setTextSize(1);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL);
  _canvas.drawString(menuValue(_menuIndex, settings, network), cx, 130);

  const char *hint = "Press selects";
  if (_menuIndex == 6) {
    hint = "Press toggles";
  } else if (_menuIndex == 7) {
    hint = "Press starts AP";
  }
  drawPill(48, 199, 144, 22, COLOR_PANEL_DARK, COLOR_PANEL_DARK, hint, COLOR_TEXT_MUTED, 1);
}

void DisplayUI::drawEdit(const Settings &settings) {
  const int16_t cx = _canvas.width() / 2;
  drawPill(52, 22, 136, 26, COLOR_PANEL_DARK, COLOR_BLUE, MENU_LABELS[_editIndex], TFT_WHITE, 1);

  _canvas.setTextDatum(middle_center);
  _canvas.setTextSize(2);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(menuValue(_editIndex, settings), cx, 112, 4);

  _canvas.setTextSize(1);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(_editIndex == 1 ? "Rotate/tap selects" : "Rotate/tap changes", cx, 173);
  drawPill(70, 194, 100, 22, COLOR_BLUE, COLOR_BLUE, "Save", TFT_WHITE, 1);
}

void DisplayUI::drawConfirmTest() {
  _canvas.setTextDatum(middle_center);
  _canvas.setTextSize(2);
  _canvas.setTextColor(COLOR_WARN, COLOR_BG);
  _canvas.drawString("Confirm test", _canvas.width() / 2, 61);
  _canvas.setTextSize(1);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(_menuIndex == 9 ? "Heater ON for 5 sec" : "Pump ON for 5 sec", _canvas.width() / 2, 108);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString("Mode cannot be OFF", _canvas.width() / 2, 134);
  drawPill(58, 160, 124, 28, COLOR_WARN, COLOR_WARN, "Press to start", TFT_BLACK, 1);
}

void DisplayUI::drawAbout() {
  _canvas.setTextDatum(middle_center);
  _canvas.setTextSize(2);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(FIRMWARE_NAME, _canvas.width() / 2, 74);
  _canvas.setTextSize(1);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(String("Version ") + FIRMWARE_VERSION, _canvas.width() / 2, 108);
  _canvas.drawString("M5Stack Dial / ESP32-S3", _canvas.width() / 2, 132);
}

void DisplayUI::drawPill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fill, uint16_t outline,
                         const String &text, uint16_t textColor, uint8_t textSize) {
  _canvas.fillRoundRect(x, y, w, h, h / 2, fill);
  _canvas.drawRoundRect(x, y, w, h, h / 2, outline);
  _canvas.setTextDatum(middle_center);
  _canvas.setTextSize(textSize);
  _canvas.setTextColor(textColor, fill);
  _canvas.drawString(text, x + w / 2, y + h / 2);
}

String DisplayUI::temperatureNumber(float tempF, bool unitsFahrenheit) const {
  if (isnan(tempF)) {
    return "--.-";
  }
  return String(unitsFahrenheit ? tempF : fToC(tempF), 1);
}

const char *DisplayUI::temperatureUnit(bool unitsFahrenheit) const {
  return unitsFahrenheit ? "F" : "C";
}

String DisplayUI::formatTemperature(float tempF, bool unitsFahrenheit) const {
  if (isnan(tempF)) {
    return "--.-";
  }
  float displayed = unitsFahrenheit ? tempF : fToC(tempF);
  return String(displayed, 1) + (unitsFahrenheit ? "F" : "C");
}

String DisplayUI::menuValue(uint8_t index, const Settings &settings, const NetworkSnapshot &network) const {
  switch (index) {
    case 0:
      return formatTemperature(settings.targetF, settings.unitsFahrenheit);
    case 1:
      return modeText(settings.mode);
    case 2:
      return String(settings.unitsFahrenheit ? settings.hysteresisF : settings.hysteresisF * 5.0f / 9.0f, 1) +
             (settings.unitsFahrenheit ? "F" : "C");
    case 3:
      return String(settings.pumpMinOffSeconds) + "s";
    case 4:
      return String(settings.pumpMinRunSeconds) + "s";
    case 5:
      return String(settings.unitsFahrenheit ? settings.tempOffsetF : settings.tempOffsetF * 5.0f / 9.0f, 1) +
             (settings.unitsFahrenheit ? "F" : "C");
    case 6:
      return settings.unitsFahrenheit ? "F" : "C";
    case 7:
      if (!network.wifiEnabled) {
        return "Disabled";
      }
      return network.status;
    case 8:
      return FERM_ENABLE_NETWORK ? "Enabled" : "Offline";
    case 9:
    case 10:
      return "5s";
    case 11:
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

}  // namespace ferm
