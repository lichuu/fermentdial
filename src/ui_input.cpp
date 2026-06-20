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
void DisplayUI::queueRemoteInput(const ScreenInputEvent &event) {
  _pendingRemoteInput = event;
  _hasPendingRemoteInput = true;
}

void DisplayUI::applyPendingRemoteInput(uint32_t nowMs, Settings &settings) {
  if (!_hasPendingRemoteInput) {
    return;
  }
  _hasPendingRemoteInput = false;
  const ScreenInputEvent event = _pendingRemoteInput;
  switch (event.kind) {
  case ScreenInputKind::Tap:
    handleTouchClick(event.x, event.y, nowMs, settings);
    break;
  case ScreenInputKind::Swipe:
    handleSwipe(nowMs, settings, event.dx, event.dy);
    break;
  case ScreenInputKind::Hold:
    if (_screen == Screen::Main || _screen == Screen::Hydrometer) {
      if (_setpointEditing) {
        cancelPendingSetpoint();
      }
      _screen = Screen::Menu;
      resetSettingsEncoderFilters();
      _dirty = true;
      markActivity(nowMs);
    } else {
      handleLongPress(settings);
      markActivity(nowMs);
    }
    break;
  case ScreenInputKind::Scroll:
    handleEncoder(event.delta, settings);
    markActivity(nowMs);
    break;
  }
}

void DisplayUI::processInput(uint32_t nowMs, Settings &settings) {
  applyPendingRemoteInput(nowMs, settings);

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

  // Track the live press point so buttons can show pressed-state feedback.
  // Redraw on press begin/end; while held stationary the sprite persists.
  const bool pressed = touch.isPressed();
  if (pressed != _touchPressActive) {
    _touchPressActive = pressed;
    _dirty = true;
  }
  _pressX = pressed ? touch.x : -1;
  _pressY = pressed ? touch.y : -1;

  if (_screen == Screen::Menu && touch.isFlicking()) {
    markActivity(nowMs);
    scrollMenuByTouch(touch.deltaY());
    return;
  }
  if ((_screen == Screen::QuickMenu || _screen == Screen::QuickProfile ||
       _screen == Screen::QuickMode) &&
      touch.isFlicking()) {
    markActivity(nowMs);
    scrollQuickByTouch(touch.deltaY(), settings);
    return;
  }

  if (touch.wasFlicked()) {
    if (_screen == Screen::Menu) {
      _touchMenuScrollAccumulator = 0;
      const bool horizontal = abs(touch.distanceX()) > abs(touch.distanceY());
      if (horizontal) {
        handleSwipe(nowMs, settings, touch.distanceX(), touch.distanceY());
      }
      return;
    }
    handleSwipe(nowMs, settings, touch.distanceX(), touch.distanceY());
    return;
  }

  if (!touch.wasClicked()) {
    return;
  }

  handleTouchClick(touch.x, touch.y, nowMs, settings);
}

void DisplayUI::handleTouchClick(int16_t x, int16_t y, uint32_t nowMs,
                                 Settings &settings) {
  markActivity(nowMs);

  const int16_t h = M5Dial.Display.height();
  if (_screen == Screen::Main || _screen == Screen::Hydrometer) {
    const int16_t cx = M5Dial.Display.width() / 2;
    const int16_t cy = h / 2;
    if (_setpointEditing) {
      // While previewing a setpoint, taps only hit the confirm/cancel row.
      if (setpointConfirmHit(x, y)) {
        commitPendingSetpoint(nowMs, settings);
      } else if (setpointCancelHit(x, y)) {
        cancelPendingSetpoint();
      }
      return;
    }
    // Help badge carve-out (matches the "?" rendered in drawMain).
    if (x >= cx - 98 && x <= cx - 70 && y >= cy - 14 && y <= cy + 14) {
      _screen = Screen::Help;
      _dirty = true;
      return;
    }
    openQuickMenu(settings);
    return;
  }

  if (_screen == Screen::Help) {
    _screen = Screen::Main;
    _dirty = true;
    return;
  }

  if (_screen == Screen::QuickMenu) {
    if (quickCancelHit(x, y)) {
      cancelQuickFlow();
      return;
    }
    if (y < h / 3) {
      handleEncoder(-MENU_ENCODER_DIVISOR, settings);
    } else if (y > (h * 2) / 3) {
      handleEncoder(MENU_ENCODER_DIVISOR, settings);
    } else {
      handleShortPress(nowMs, settings);
    }
    return;
  }

  if (_screen == Screen::QuickProfile || _screen == Screen::QuickMode) {
    if (quickCancelHit(x, y)) {
      cancelQuickFlow();
      return;
    }
    if (y < h / 3) {
      handleEncoder(-MENU_ENCODER_DIVISOR, settings);
    } else if (y > (h * 2) / 3) {
      handleEncoder(MENU_ENCODER_DIVISOR, settings);
    } else {
      requestQuickConfirm(settings);
    }
    return;
  }

  if (_screen == Screen::QuickConfirm) {
    const int16_t cx = M5Dial.Display.width() / 2;
    const int16_t cy = h / 2;
    if (_quickConfirmKind == QuickConfirmKind::CrashGradual) {
      if (x >= cx - 90 && x <= cx - 6 && y >= cy + 44 && y <= cy + 70) {
        _pendingGradualCrash = false;
        confirmQuickAction(settings);
      } else if (x >= cx + 6 && x <= cx + 90 && y >= cy + 44 && y <= cy + 70) {
        _pendingGradualCrash = true;
        confirmQuickAction(settings);
      }
      return;
    }
    if (quickCancelHit(x, y)) {
      cancelQuickFlow();
      return;
    }
    // Apply is the right button of the confirm action row.
    if (x >= cx + 6 && x <= cx + 90 && y >= cy + 44 && y <= cy + 70) {
      confirmQuickAction(settings);
    }
    return;
  }

  if (_screen == Screen::Menu) {
    if (y < h / 3) {
      handleEncoder(-MENU_ENCODER_DIVISOR, settings);
    } else if (y > (h * 2) / 3) {
      handleEncoder(MENU_ENCODER_DIVISOR, settings);
    } else {
      handleShortPress(nowMs, settings);
    }
    return;
  }

  if (_screen == Screen::EditInfo) {
    const int16_t cx = M5Dial.Display.width() / 2;
    const int16_t cy = h / 2;
    const int16_t backY = cy + 66;
    if (y >= backY && y <= backY + 24) {
      _screen = Screen::Menu;
      resetSettingsEncoderFilters();
      _dirty = true;
    }
    return;
  }

  if (_screen == Screen::Edit) {
    // Hit zones must match the rendered button rects in drawEdit().
    const int16_t cx = M5Dial.Display.width() / 2;
    const int16_t cy = h / 2;
    const int16_t topY = cy + 34;   // Reset / Save row (height 24)
    const int16_t backY = cy + 66;  // Back row (height 24)
    if (y >= topY && y < topY + 24) {
      if (!editHasReset(settings)) {
        if (x >= cx - 55 && x <= cx + 55) {
          requestEditConfirm(EditConfirmAction::Save);
        }
      } else if (x < cx) {
        requestEditConfirm(EditConfirmAction::Reset);
      } else {
        requestEditConfirm(EditConfirmAction::Save);
      }
    } else if (y >= backY && y <= backY + 24) {
      cancelEdit(settings);
    } else if (y < topY) {
      // Tap the upper area to nudge the value: left = down, right = up.
      handleEncoder(x < cx ? -EDIT_ENCODER_DIVISOR : EDIT_ENCODER_DIVISOR,
                    settings);
    }
    return;
  }

  if (_screen == Screen::ConfirmEdit) {
    const int16_t cx = M5Dial.Display.width() / 2;
    const int16_t cy = h / 2;
    // Same action-row rects as QuickConfirm: Cancel (left) / confirm (right).
    if (x >= cx - 90 && x <= cx - 6 && y >= cy + 44 && y <= cy + 70) {
      cancelEditConfirm();
    } else if (x >= cx + 6 && x <= cx + 90 && y >= cy + 44 && y <= cy + 70) {
      confirmEditAction(settings);
    }
    return;
  }

  handleShortPress(nowMs, settings);
}

void DisplayUI::handleSwipe(uint32_t nowMs, Settings &settings, int16_t dx,
                            int16_t dy) {
  const int16_t absX = abs(dx);
  const int16_t absY = abs(dy);
  const bool horizontal = absX > absY;

  markActivity(nowMs);

  if (_screen == Screen::Main || _screen == Screen::Hydrometer) {
    if (_setpointEditing) {
      cancelPendingSetpoint();
      return;
    }
    if (horizontal) {
      _screen = (_screen == Screen::Main) ? Screen::Hydrometer : Screen::Main;
      _dirty = true;
      return;
    }
    openQuickMenu(settings);
    return;
  }

  if (_screen == Screen::QuickMenu) {
    if (horizontal) {
      moveQuickMenu(dx > 0 ? 1 : -1);
    }
    _dirty = true;
    return;
  }

  if (_screen == Screen::QuickProfile || _screen == Screen::QuickMode) {
    if (horizontal) {
      if (dx > 0) {
        requestQuickConfirm(settings);
      } else {
        _screen = Screen::QuickMenu;
        resetSettingsEncoderFilters();
      }
    } else {
      if (dy > 0) {
        cancelQuickFlow();
      } else {
        moveQuickSelection(-1, settings);
      }
    }
    _dirty = true;
    return;
  }

  if (_screen == Screen::QuickConfirm) {
    if (_quickConfirmKind == QuickConfirmKind::CrashGradual) {
      if (horizontal) {
        _pendingGradualCrash = dx > 0;
        confirmQuickAction(settings);
      } else {
        _pendingGradualCrash = dy < 0;
        _dirty = true;
      }
    } else if (dx > 0) {
      confirmQuickAction(settings);
    } else {
      cancelQuickFlow();
    }
    return;
  }

  if (_screen == Screen::Menu) {
    if (horizontal) {
      if (dx > 0) {
        handleShortPress(nowMs, settings);
      } else {
        _screen = Screen::Main;
        resetSettingsEncoderFilters();
      }
    }
    _dirty = true;
    return;
  }

  if (_screen == Screen::EditInfo) {
    if (dy > 0 || (horizontal && dx < 0)) {
      _screen = Screen::Menu;
      resetSettingsEncoderFilters();
      _dirty = true;
    }
    return;
  }

  if (_screen == Screen::Edit) {
    if (horizontal) {
      editCurrentValue(dx > 0 ? 1 : -1, settings);
    } else if (dy < 0) {
      requestEditConfirm(EditConfirmAction::Reset);
    } else {
      cancelEdit(settings);
    }
    _dirty = true;
    return;
  }

  if (dx < 0 || dy > 0) {
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
    _dirty = true;
  }
}

void DisplayUI::scrollMenuByTouch(int16_t deltaY) {
  _touchMenuScrollAccumulator -= deltaY;
  int32_t steps = _touchMenuScrollAccumulator / TOUCH_MENU_ITEM_PX;
  if (steps == 0) {
    return;
  }
  _touchMenuScrollAccumulator -= steps * TOUCH_MENU_ITEM_PX;

  int32_t next = static_cast<int32_t>(_menuIndex) + steps;
  while (next < 0) {
    next += MENU_COUNT;
  }
  _menuIndex = static_cast<uint8_t>(next % MENU_COUNT);
  _dirty = true;
}

void DisplayUI::scrollQuickByTouch(int16_t deltaY, const Settings &settings) {
  _touchMenuScrollAccumulator -= deltaY;
  int32_t steps = _touchMenuScrollAccumulator / TOUCH_MENU_ITEM_PX;
  if (steps == 0) {
    return;
  }
  _touchMenuScrollAccumulator -= steps * TOUCH_MENU_ITEM_PX;

  if (_screen == Screen::QuickMenu) {
    moveQuickMenu(steps);
  } else {
    moveQuickSelection(steps, settings);
  }
  _dirty = true;
}

void DisplayUI::handleEncoder(int32_t delta, Settings &settings) {
  if (_screen == Screen::Main) {
    // Rotating on the main screen previews a new setpoint in gold but does not
    // commit it — the user must confirm (button / on-screen check) or it
    // auto-cancels, so an accidental bump of the dial can't change the target.
    int32_t filteredDelta = filteredSettingsDelta(
        delta, _editEncoderAccumulator, EDIT_ENCODER_DIVISOR);
    if (filteredDelta == 0) {
      return;
    }
    if (!_setpointEditing) {
      _setpointEditing = true;
      _pendingTargetC = currentTargetC(settings);
    }
    _pendingTargetC = clampFloat(
        _pendingTargetC +
            (filteredDelta * displayStepC(settings.unitsFahrenheit)),
        MIN_TARGET_C, MAX_TARGET_C);
    _setpointFocusUntilMs = millis() + SETPOINT_EDIT_TIMEOUT_MS;
    _dirty = true;
    return;
  } else if (_screen == Screen::QuickMenu) {
    int32_t filteredDelta = filteredSettingsDelta(
        delta, _quickEncoderAccumulator, MENU_ENCODER_DIVISOR);
    if (filteredDelta == 0) {
      return;
    }
    moveQuickMenu(filteredDelta);
  } else if (_screen == Screen::QuickProfile || _screen == Screen::QuickMode) {
    int32_t filteredDelta = filteredSettingsDelta(
        delta, _quickEncoderAccumulator, MENU_ENCODER_DIVISOR);
    if (filteredDelta == 0) {
      return;
    }
    moveQuickSelection(filteredDelta, settings);
  } else if (_screen == Screen::QuickConfirm &&
             _quickConfirmKind == QuickConfirmKind::CrashGradual) {
    int32_t filteredDelta = filteredSettingsDelta(
        delta, _quickEncoderAccumulator, MENU_ENCODER_DIVISOR);
    if (filteredDelta == 0) {
      return;
    }
    _pendingGradualCrash = filteredDelta > 0;
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
  if (_screen == Screen::Main || _screen == Screen::Hydrometer) {
    if (_setpointEditing) {
      commitPendingSetpoint(nowMs, settings);  // press confirms the new setpoint
      return;
    }
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
    _dirty = true;
    return;
  } else if (_screen == Screen::QuickMenu) {
    selectQuickAction(settings);
  } else if (_screen == Screen::QuickProfile || _screen == Screen::QuickMode) {
    requestQuickConfirm(settings);
  } else if (_screen == Screen::QuickConfirm) {
    confirmQuickAction(settings);
  } else if (_screen == Screen::Menu) {
    if (_menuIndex == MENU_DREST) {
      if (settings.diacetylRestActive) {
        completeDiacetylRest(settings);
        _toast = String("D-rest -> ") + activeProfile(settings).name;
        _toastUntilMs = nowMs + 1800;
        requestSave();
        resetSettingsEncoderFilters();
      } else {
        // Prompt for how long to run before starting, rather than starting
        // immediately with whatever duration was last used.
        beginEdit(_menuIndex, settings);
      }
      _dirty = true;
      return;
    } else if (_menuIndex <= MENU_OFFSET) {
      beginEdit(_menuIndex, settings);
    } else if (_menuIndex == MENU_UNITS) {
      settings.unitsFahrenheit = !settings.unitsFahrenheit;
      _toast = String("Units: ") + temperatureUnit(settings.unitsFahrenheit);
      _toastUntilMs = nowMs + 1500;
      requestSave();
      resetSettingsEncoderFilters();
      _dirty = true;
      return;
    } else if (_menuIndex == MENU_HYDROMETER) {
      beginEdit(_menuIndex, settings);
    } else if (_menuIndex == MENU_WIFI) {
      _wifiSetupRequested = true;
      _toast = FERM_ENABLE_NETWORK ? "Setup AP active" : "Wi-Fi disabled";
      _toastUntilMs = nowMs + 3500;
    } else if (_menuIndex == MENU_MQTT) {
      _toast = FERM_ENABLE_NETWORK ? "Set up MQTT in web UI" : "Network disabled";
      _toastUntilMs = nowMs + 2500;
    } else if (_menuIndex == MENU_HEATER_TEST || _menuIndex == MENU_PUMP_TEST) {
      _screen = Screen::ConfirmTest;
      resetSettingsEncoderFilters();
    } else if (_menuIndex == MENU_ABOUT) {
      _screen = Screen::About;
      resetSettingsEncoderFilters();
    }
  } else if (_screen == Screen::EditInfo) {
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
  } else if (_screen == Screen::Edit) {
    requestEditConfirm(EditConfirmAction::Save);
  } else if (_screen == Screen::ConfirmEdit) {
    confirmEditAction(settings);
  } else if (_screen == Screen::ConfirmTest) {
    _pendingOutputTest =
        _menuIndex == MENU_HEATER_TEST ? OutputTestKind::Heater
                                       : OutputTestKind::Pump;
    _screen = Screen::Main;
    resetSettingsEncoderFilters();
  } else if (_screen == Screen::About) {
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
  } else if (_screen == Screen::Help) {
    _screen = Screen::Main;
    resetSettingsEncoderFilters();
  }
  _dirty = true;
}

void DisplayUI::handleLongPress(Settings &settings) {
  if (_screen == Screen::Main) {
    if (_setpointEditing) {
      cancelPendingSetpoint();  // long-press discards the previewed setpoint
      return;
    }
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
    _dirty = true;
    return;
  } else if (_screen == Screen::QuickMenu) {
    _screen = Screen::Menu;
  } else if (_screen == Screen::QuickProfile ||
             _screen == Screen::QuickMode ||
             _screen == Screen::QuickConfirm) {
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
    _dirty = true;
    return;
  } else if (_screen == Screen::EditInfo) {
    _screen = Screen::Menu;
    resetSettingsEncoderFilters();
    _dirty = true;
    return;
  } else if (_screen == Screen::Edit) {
    cancelEdit(settings);
    return;
  } else if (_screen == Screen::ConfirmEdit) {
    cancelEditConfirm();
    return;
  } else {
    _screen = Screen::Main;
  }
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::commitPendingSetpoint(uint32_t nowMs, Settings &settings) {
  if (!_setpointEditing) {
    return;
  }
  setCurrentTargetC(settings, _pendingTargetC);
  _setpointEditing = false;
  _setpointFocusUntilMs = 0;
  requestSave();
  _toast = String("Set ") +
           formatTemperature(currentTargetC(settings), settings.unitsFahrenheit);
  _toastUntilMs = nowMs + 1500;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::cancelPendingSetpoint() {
  if (!_setpointEditing) {
    return;
  }
  _setpointEditing = false;
  _setpointFocusUntilMs = 0;
  resetSettingsEncoderFilters();
  _dirty = true;
}

bool DisplayUI::setpointCancelHit(int16_t x, int16_t y) const {
  const int16_t cx = M5Dial.Display.width() / 2;
  const int16_t cy = M5Dial.Display.height() / 2;
  return x >= cx - 90 && x <= cx - 6 && y >= cy + 44 && y <= cy + 70;
}

bool DisplayUI::setpointConfirmHit(int16_t x, int16_t y) const {
  const int16_t cx = M5Dial.Display.width() / 2;
  const int16_t cy = M5Dial.Display.height() / 2;
  return x >= cx + 6 && x <= cx + 90 && y >= cy + 44 && y <= cy + 70;
}

void DisplayUI::openQuickMenu(const Settings &settings) {
  _pendingProfile = activeProfileIndex(settings);
  _pendingMode = settings.mode;
  _screen = Screen::QuickMenu;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::moveQuickMenu(int32_t delta) {
  int32_t next = static_cast<int32_t>(_quickIndex) + delta;
  while (next < 0) {
    next += QUICK_ACTION_COUNT;
  }
  _quickIndex = static_cast<uint8_t>(next % QUICK_ACTION_COUNT);
  _dirty = true;
}

void DisplayUI::selectQuickAction(const Settings &settings) {
  if (_quickIndex == 0) {
    _pendingQuickAction = QuickAction::Profile;
    _pendingProfile = activeProfileIndex(settings);
    _screen = Screen::QuickProfile;
  } else {
    _pendingQuickAction = QuickAction::Mode;
    _pendingMode = settings.mode;
    _screen = Screen::QuickMode;
  }
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::moveQuickSelection(int32_t delta, const Settings &settings) {
  if (_screen == Screen::QuickProfile) {
    int32_t profile = static_cast<int32_t>(_pendingProfile) + delta;
    while (profile < 0) {
      profile += PROFILE_COUNT;
    }
    _pendingProfile = static_cast<uint8_t>(profile % PROFILE_COUNT);
  } else if (_screen == Screen::QuickMode) {
    int32_t mode = static_cast<int32_t>(_pendingMode) + delta;
    while (mode < 0) {
      mode += 4;
    }
    _pendingMode = static_cast<UserMode>(mode % 4);
  }
  (void)settings;
  _dirty = true;
}

void DisplayUI::requestQuickConfirm(const Settings &settings) {
  if (_screen == Screen::QuickProfile) {
    _pendingQuickAction = QuickAction::Profile;
    if (_pendingProfile == static_cast<uint8_t>(ProfileSlot::Crash)) {
      requestCrashGradualPrompt(settings.gradualCrashEnabled);
      return;
    }
  } else if (_screen == Screen::QuickMode) {
    _pendingQuickAction = QuickAction::Mode;
  }
  _quickConfirmKind = QuickConfirmKind::Apply;
  _screen = Screen::QuickConfirm;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::requestCrashGradualPrompt(bool defaultGradual) {
  _pendingQuickAction = QuickAction::Profile;
  _pendingProfile = static_cast<uint8_t>(ProfileSlot::Crash);
  _pendingGradualCrash = defaultGradual;
  _quickConfirmKind = QuickConfirmKind::CrashGradual;
  _screen = Screen::QuickConfirm;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::confirmQuickAction(Settings &settings) {
  if (_quickConfirmKind == QuickConfirmKind::CrashGradual) {
    applyCrashProfile(settings, _pendingGradualCrash);
    _quickConfirmKind = QuickConfirmKind::Apply;
    _toast = _pendingGradualCrash ? "Crash: gradual" : "Crash: direct";
  } else if (_pendingQuickAction == QuickAction::Profile) {
    activateProfile(settings, _pendingProfile);
    _toast = String("Profile: ") + activeProfile(settings).name;
  } else {
    settings.mode = _pendingMode;
    _toast = String("Mode: ") + modeText(settings.mode);
  }
  _toastUntilMs = millis() + 1500;
  requestSave();
  _screen = Screen::Main;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::applyCrashProfile(Settings &settings, bool gradual) {
  deactivateSupersededModes(settings);
  settings.activeProfile = static_cast<uint8_t>(ProfileSlot::Crash);
  settings.gradualCrashEnabled = gradual;
  applyActiveProfileTarget(settings);  // direct recalls target, gradual preserves high target
}

void DisplayUI::cancelQuickFlow() {
  _quickConfirmKind = QuickConfirmKind::Apply;
  _screen = Screen::Main;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::beginEdit(uint8_t index, const Settings &settings) {
  _editIndex = index;
  if (index == MENU_TARGET &&
      !profileSlotEditable(activeProfileIndex(settings))) {
    _screen = Screen::EditInfo;
    resetSettingsEncoderFilters();
    _dirty = true;
    return;
  }
  _editSnapshot = settings;
  _editSnapshotValid = true;
  _screen = Screen::Edit;
  if (index == MENU_HYDROMETER) {
    _hydroOptionIndex = hydrometerOptionIndexFromSettings(settings);
  }
  resetSettingsEncoderFilters();
}

void DisplayUI::cancelEdit(Settings &settings) {
  if (_editSnapshotValid) {
    settings = _editSnapshot;
  }
  _editSnapshotValid = false;
  _screen = Screen::Menu;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::saveEdit(Settings &settings) {
  if (_editIndex == MENU_PROFILE &&
      activeProfileIndex(settings) == static_cast<uint8_t>(ProfileSlot::Crash)) {
    const bool defaultGradual = settings.gradualCrashEnabled;
    if (_editSnapshotValid) {
      settings = _editSnapshot;
    }
    _editSnapshotValid = false;
    requestCrashGradualPrompt(defaultGradual);
    return;
  }

  if (_editIndex == MENU_PROFILE) {
    activateProfile(settings, activeProfileIndex(settings));
    _toast = String("Profile: ") + activeProfile(settings).name;
    _toastUntilMs = millis() + 1800;
  } else if (_editIndex == MENU_TARGET) {
    applyActiveProfileTarget(settings);
    _toast = String("Saved: ") + activeProfile(settings).name;
    _toastUntilMs = millis() + 1800;
  } else if (_editIndex == MENU_DREST) {
    startDiacetylRest(settings);
    _toast = "D-rest started";
    _toastUntilMs = millis() + 1800;
  }
  _editSnapshotValid = false;
  _screen = Screen::Menu;
  resetSettingsEncoderFilters();
  requestSave();
  _dirty = true;
}

void DisplayUI::requestEditConfirm(EditConfirmAction action) {
  _pendingEditConfirm = action;
  _screen = Screen::ConfirmEdit;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::confirmEditAction(Settings &settings) {
  if (_pendingEditConfirm == EditConfirmAction::Reset) {
    resetCurrentValue(settings);
    _screen = Screen::Edit;
  } else if (_pendingEditConfirm == EditConfirmAction::Save) {
    saveEdit(settings);
  } else {
    _screen = Screen::Edit;
  }
  _pendingEditConfirm = EditConfirmAction::None;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::cancelEditConfirm() {
  _pendingEditConfirm = EditConfirmAction::None;
  _screen = Screen::Edit;
  resetSettingsEncoderFilters();
  _dirty = true;
}

void DisplayUI::editCurrentValue(int32_t delta, Settings &settings) {
  switch (_editIndex) {
  case MENU_PROFILE: {
    int32_t profile =
        static_cast<int32_t>(settings.activeProfile) + (delta > 0 ? 1 : -1);
    while (profile < 0) {
      profile += PROFILE_COUNT;
    }
    settings.activeProfile = static_cast<uint8_t>(profile % PROFILE_COUNT);
    break;
  }
  case MENU_TARGET:
    if (!profileSlotEditable(activeProfileIndex(settings))) {
      break;
    }
    setActiveTargetC(
        settings,
        activeTargetC(settings) +
            (delta * (displayStepC(settings.unitsFahrenheit))));
    break;
  case MENU_DREST: {
    const int32_t steps = delta > 0 ? 1 : -1;
    const int32_t next = static_cast<int32_t>(settings.diacetylRestDurationSeconds) +
        steps * static_cast<int32_t>(DIACETYL_REST_DURATION_STEP_SECONDS);
    settings.diacetylRestDurationSeconds = clampU32(
        static_cast<uint32_t>(next < 0 ? 0 : next),
        MIN_DIACETYL_REST_DURATION_SECONDS, MAX_DIACETYL_REST_DURATION_SECONDS);
    break;
  }
  case MENU_MODE: {
    int32_t mode = static_cast<int32_t>(settings.mode) + (delta > 0 ? 1 : -1);
    while (mode < 0) {
      mode += 4;
    }
    settings.mode = static_cast<UserMode>(mode % 4);
    break;
  }
  case MENU_COOL_ON:
    settings.coolOnDeltaC = clampFloat(
        settings.coolOnDeltaC +
            (delta * (displayStepC(settings.unitsFahrenheit))),
        MIN_DELTA_C, MAX_DELTA_C);
    break;
  case MENU_HEAT_ON:
    settings.heatOnDeltaC = clampFloat(
        settings.heatOnDeltaC +
            (delta * (displayStepC(settings.unitsFahrenheit))),
        MIN_DELTA_C, MAX_DELTA_C);
    break;
  case MENU_HOLD_BAND:
    settings.holdDeltaC = clampFloat(
        settings.holdDeltaC +
            (delta * (displayStepC(settings.unitsFahrenheit))),
        MIN_DELTA_C, MAX_DELTA_C);
    if (settings.holdDeltaC > settings.coolOnDeltaC) {
      settings.holdDeltaC = settings.coolOnDeltaC;
    }
    if (settings.holdDeltaC > settings.heatOnDeltaC) {
      settings.holdDeltaC = settings.heatOnDeltaC;
    }
    break;
  case MENU_COOLING_OFF: {
    int32_t next =
        static_cast<int32_t>(settings.pumpMinOffSeconds) + (delta * 5);
    if (next < 0) {
      next = 0;
    }
    settings.pumpMinOffSeconds = clampU32(static_cast<uint32_t>(next), 0, 1800);
    break;
  }
  case MENU_COOLING_RUN: {
    int32_t next =
        static_cast<int32_t>(settings.pumpMinRunSeconds) + (delta * 5);
    if (next < 0) {
      next = 0;
    }
    settings.pumpMinRunSeconds = clampU32(static_cast<uint32_t>(next), 0, 600);
    break;
  }
  case MENU_OFFSET:
    settings.tempOffsetC = clampFloat(
        settings.tempOffsetC +
            (delta * (displayStepC(settings.unitsFahrenheit))),
        MIN_OFFSET_C, MAX_OFFSET_C);
    break;
  case MENU_UNITS:
    settings.unitsFahrenheit = !settings.unitsFahrenheit;
    break;
  case MENU_HYDROMETER: {
    // Options: 0 = off, 1 = scan on, 2+ = a discovered device.
    const int32_t total = 2 + static_cast<int32_t>(_hydroDeviceCount);
    int32_t next = _hydroOptionIndex + (delta > 0 ? 1 : -1);
    while (next < 0) {
      next += total;
    }
    next %= total;
    _hydroOptionIndex = next;
    if (next == 0) {
      setHydrometerScanEnabled(settings, false);
    } else if (next == 1) {
      setHydrometerScanTypeFromUi(settings, HydrometerScanType::All);
    } else {
      const uint8_t devIndex = static_cast<uint8_t>(next - 2);
      if (devIndex < _hydroDeviceCount) {
        selectHydrometerDevice(settings, _hydroDevices[devIndex].key);
      }
    }
    break;
  }
  default:
    break;
  }
}

void DisplayUI::resetCurrentValue(Settings &settings) {
  switch (_editIndex) {
  case MENU_PROFILE:
    if (profileSlotEditable(activeProfileIndex(settings))) {
      setActiveTargetC(settings,
                       defaultProfileTargetC(activeProfileIndex(settings)));
    }
    break;
  case MENU_TARGET:
    if (profileSlotEditable(settings.activeProfile)) {
      setActiveTargetC(settings,
                       defaultProfileTargetC(settings.activeProfile));
    }
    break;
  case MENU_DREST:
    settings.diacetylRestDurationSeconds = DEFAULT_DIACETYL_REST_DURATION_SECONDS;
    break;
  case MENU_MODE:
    settings.mode = UserMode::Off;
    break;
  case MENU_COOL_ON:
    settings.coolOnDeltaC = DEFAULT_COOL_ON_DELTA_C;
    if (settings.holdDeltaC > settings.coolOnDeltaC) {
      settings.holdDeltaC = settings.coolOnDeltaC;
    }
    break;
  case MENU_HEAT_ON:
    settings.heatOnDeltaC = DEFAULT_HEAT_ON_DELTA_C;
    if (settings.holdDeltaC > settings.heatOnDeltaC) {
      settings.holdDeltaC = settings.heatOnDeltaC;
    }
    break;
  case MENU_HOLD_BAND:
    settings.holdDeltaC = DEFAULT_HOLD_DELTA_C;
    break;
  case MENU_COOLING_OFF:
    settings.pumpMinOffSeconds = DEFAULT_PUMP_MIN_OFF_SECONDS;
    break;
  case MENU_COOLING_RUN:
    settings.pumpMinRunSeconds = DEFAULT_PUMP_MIN_RUN_SECONDS;
    break;
  case MENU_OFFSET:
    settings.tempOffsetC = DEFAULT_TEMP_OFFSET_C;
    break;
  default:
    break;
  }
  resetSettingsEncoderFilters();
  _dirty = true;
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
  _quickEncoderAccumulator = 0;
  _touchMenuScrollAccumulator = 0;
}

void DisplayUI::requestSave() { _saveRequested = true; }

void DisplayUI::markActivity(uint32_t nowMs) {
  _lastActivityMs = nowMs;
  if (_dimmed) {
    _dimmed = false;
    _appliedBrightness = 0;  // force re-apply at configured brightness next update
  }
}

} // namespace ferm
