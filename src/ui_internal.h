#pragma once

// Shared constants for the DisplayUI implementation, which is split across
// ui.cpp, ui_draw.cpp, and ui_input.cpp. Constexpr values keep internal linkage
// so each TU gets its own copy, matching the old per-file anonymous-namespace
// blocks. MenuIndex is a shared implementation-detail enum for the ui*.cpp
// split, not a public UI API type. Only the ui*.cpp files should include this.

#include <stdint.h>

namespace ferm {

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
  MENU_BRIGHTNESS = 14,
  MENU_HEATER_TEST = 15,
  MENU_PUMP_TEST = 16,
  MENU_ABOUT = 17,
};

constexpr uint8_t MENU_COUNT = MENU_ABOUT + 1;

// Settings is two-level: pick a group, then an item within it.
enum MenuGroup : uint8_t {
  GROUP_DAILY = 0,
  GROUP_CONTROL = 1,
  GROUP_SYSTEM = 2,
};

constexpr uint8_t GROUP_COUNT = 3;

// Quick menu base: Profile / Mode / D-Rest. Program is appended while active.
constexpr uint8_t QUICK_ACTION_BASE = 3;
constexpr uint8_t QUICK_ACTION_MAX = 4;

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

// Friendlier labels for the round display (short enough for the card).
constexpr const char *MENU_LABELS[MENU_COUNT] = {
    "Profile",      "Target",       "D-Rest",       "Mode",
    "Cool above",   "Heat below",   "In range",     "Pump rest",
    "Pump min run", "Sensor offset","Units",        "Wi-Fi",
    "MQTT",         "Hydrometer",   "Brightness",   "Heater test",
    "Pump test",    "About",
};

constexpr const char *GROUP_LABELS[GROUP_COUNT] = {
    "Daily",
    "Control",
    "System",
};

// Daily: brew-side actions. Control: regulation bands. System: device/setup.
constexpr MenuIndex DAILY_ITEMS[] = {
    MENU_PROFILE, MENU_TARGET, MENU_MODE, MENU_DREST, MENU_UNITS,
};
constexpr MenuIndex CONTROL_ITEMS[] = {
    MENU_COOL_ON,     MENU_HEAT_ON,      MENU_HOLD_BAND,
    MENU_COOLING_OFF, MENU_COOLING_RUN,  MENU_OFFSET,
};
constexpr MenuIndex SYSTEM_ITEMS[] = {
    MENU_WIFI,        MENU_MQTT,       MENU_HYDROMETER, MENU_BRIGHTNESS,
    MENU_HEATER_TEST, MENU_PUMP_TEST,  MENU_ABOUT,
};

inline const MenuIndex *groupItems(uint8_t group) {
  switch (group) {
  case GROUP_CONTROL:
    return CONTROL_ITEMS;
  case GROUP_SYSTEM:
    return SYSTEM_ITEMS;
  default:
    return DAILY_ITEMS;
  }
}

inline uint8_t groupItemCount(uint8_t group) {
  switch (group) {
  case GROUP_CONTROL:
    return static_cast<uint8_t>(sizeof(CONTROL_ITEMS) / sizeof(CONTROL_ITEMS[0]));
  case GROUP_SYSTEM:
    return static_cast<uint8_t>(sizeof(SYSTEM_ITEMS) / sizeof(SYSTEM_ITEMS[0]));
  default:
    return static_cast<uint8_t>(sizeof(DAILY_ITEMS) / sizeof(DAILY_ITEMS[0]));
  }
}

// Position of `item` within its group list, or 0 if not found.
inline uint8_t groupItemPos(uint8_t group, uint8_t item) {
  const MenuIndex *items = groupItems(group);
  const uint8_t count = groupItemCount(group);
  for (uint8_t i = 0; i < count; i++) {
    if (items[i] == item) {
      return i;
    }
  }
  return 0;
}

}  // namespace ferm
