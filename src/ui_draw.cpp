#include "ui.h"

#include "fonts/dejavu_sans_bold_44_vlw.h"
#include "fonts/help_glyph.h"
#include "status_hint.h"

#include "ui_internal.h"

namespace ferm {

void DisplayUI::draw(uint32_t nowMs, const Settings &settings,
                     const UiModel &model) {
  if (_screen != Screen::Main) {
    useDefaultFont();
  }
  _canvas.fillScreen(COLOR_BG);
  if (_screen == Screen::Main) {
    drawMain(nowMs, settings, model);
  } else if (_screen == Screen::Hydrometer) {
    drawHydrometer(nowMs, settings, model);
  } else if (_screen == Screen::QuickMenu) {
    drawQuickMenu(settings, model);
  } else if (_screen == Screen::QuickProfile) {
    drawQuickProfile(settings, model);
  } else if (_screen == Screen::QuickMode) {
    drawQuickMode(settings, model);
  } else if (_screen == Screen::QuickConfirm) {
    drawQuickConfirm(settings, model);
  } else if (_screen == Screen::Menu) {
    drawMenu(settings, model.network);
  } else if (_screen == Screen::Edit) {
    drawEdit(settings);
  } else if (_screen == Screen::EditInfo) {
    drawEditInfo(settings);
  } else if (_screen == Screen::ConfirmEdit) {
    drawConfirmEdit(settings);
  } else if (_screen == Screen::ConfirmTest) {
    drawConfirmTest();
  } else if (_screen == Screen::ConfirmWifi) {
    drawConfirmWifi(model.network);
  } else if (_screen == Screen::Help) {
    drawHelp();
  } else {
    drawAbout(model.network);
  }

  if (_toast.length() > 0 && nowMs < _toastUntilMs) {
    const lgfx::IFont *toastFont = &fonts::DejaVu12;
    _canvas.setTextSize(1);
    const int16_t cx = _canvas.width() / 2;
    const int16_t pad = 16;
    const int16_t pillH = 30;
    int16_t pillW = _canvas.textWidth(_toast, toastFont) + pad * 2;
    const int16_t maxW = _canvas.width() - 24;
    if (pillW > maxW) {
      pillW = maxW;
    }
    // Keep the pill in the wide middle band of the round display (not the
    // pinched bottom edge) so longer messages are not clipped.
    const int16_t pillY = 170;
    _canvas.fillSmoothRoundRect(cx - pillW / 2, pillY, pillW, pillH, pillH / 2,
                                COLOR_PANEL);
    _canvas.drawRoundRect(cx - pillW / 2, pillY, pillW, pillH, pillH / 2,
                          COLOR_BLUE);
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(COLOR_GOLD, COLOR_PANEL);
    _canvas.drawString(_toast, cx, pillY + pillH / 2, toastFont);
  }

  _canvas.pushSprite(0, 0);
}

void DisplayUI::drawMain(uint32_t nowMs, const Settings &settings,
                         const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;
  (void)nowMs;
  const uint16_t bg = COLOR_BG;  // flat face; state reads from the arc colour
  const bool editing = _setpointEditing;  // previewing an un-confirmed setpoint
  const bool fault = model.runtimeState == RuntimeState::Fault;
  const bool unitsF = settings.unitsFahrenheit;
  const float targetC =
      _setpointEditing ? _pendingTargetC : currentTargetC(settings);
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

  // 2. hold band around the target (matches the configured in-range band)
  drawArcSeg(GAUGE_R, gaugeAngle(targetC - settings.holdDeltaC),
             gaugeAngle(targetC + settings.holdDeltaC), COLOR_OK, GAUGE_W);

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
  String profileLine;
  if (settings.diacetylRestActive) {
    profileLine = "D-Rest";
  } else if (settings.programActive) {
    const uint8_t ri =
        settings.programRunIndex < PROGRAM_SLOT_COUNT ? settings.programRunIndex
                                                      : 0;
    profileLine = String("Step ") + String(settings.programStepIndex + 1) +
                  "/" + String(settings.programs[ri].stepCount);
  } else {
    profileLine = activeProfile(settings).name;
  }
  _canvas.drawString(profileLine, cx, cy - 52, &fonts::DejaVu18);

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

  // 6. state line (+ optional why-detail when the controller is held off)
  ControlHintInput hintIn;
  hintIn.settings = &settings;
  hintIn.runtimeState = model.runtimeState;
  hintIn.faultCode = model.faultCode;
  hintIn.tempValid = model.tempValid;
  hintIn.tempC = model.tempC;
  hintIn.pumpOn = model.pumpOn;
  hintIn.pumpOffElapsedMs = model.pumpOffElapsedMs;
  hintIn.outputTestActive = model.outputTestActive;
  hintIn.outputTestKind = model.outputTestKind;
  hintIn.hydroSelected = model.hydrometer.selected;
  hintIn.hydroStale = model.hydrometer.selected && model.hydrometer.stale;
  hintIn.notReaching = model.notReaching;
  hintIn.longOutput = model.longOutput;
  const ControlHint hint = buildControlHint(hintIn);
  _lastAttention = hint.attention;

  String stateLine;
  uint16_t stateCol;
  if (editing) {
    stateLine = "SET TARGET";
    stateCol = COLOR_GOLD;
  } else if (fault) {
    stateLine = hint.primary;
    stateCol = COLOR_FAULT;
  } else if (model.outputTestActive) {
    stateLine = hint.primary;
    stateCol = COLOR_WARN;
  } else if (model.runtimeState == RuntimeState::Idle) {
    stateLine = "IN RANGE";
    stateCol = accent;
  } else {
    stateLine = hint.primary;
    stateCol = accent;
  }
  _canvas.setTextColor(stateCol, bg);
  // Anchor rows: temperature centre (cy - 6), hydrometer line (cy + 46).
  constexpr int16_t kHydroRowY = 46;
  constexpr int16_t kTempVisualBottomY = 12;
  const int16_t stateY = cy + (kTempVisualBottomY + kHydroRowY) / 2;
  if (largeFontLoaded) {
    // Scale the loaded DejaVu VLW (44 px) instead of switching to FreeSans,
    // so the state line shares the temperature's typeface.
    _canvas.setTextSize(0.5f);
    _canvas.drawString(stateLine, cx, stateY);
    _canvas.setTextSize(1.0f);
  } else {
    _canvas.drawString(stateLine, cx, stateY, &fonts::FreeSansBold12pt7b);
  }

  // Prefer why-detail over the SG line when it explains idle holdoff; otherwise
  // show hydrometer status as before.
  String subLine = "";
  if (!editing && hint.detail[0] != '\0' &&
      (model.runtimeState == RuntimeState::Idle ||
       settings.programActive || settings.gradualCrashEnabled)) {
    subLine = hint.detail;
  } else if (model.hydrometer.selected && model.hydrometer.valid) {
    subLine = "SG " + String(model.hydrometer.gravity, 3);
    subLine += " | ";
    if (model.hydrometer.stale) {
      subLine += "stale";
    } else {
      const uint32_t ageS = model.hydrometer.lastSeenMs == 0
                                ? 0
                                : (nowMs - model.hydrometer.lastSeenMs) / 1000UL;
      if (ageS < 60) {
        subLine += String(ageS) + "s";
      } else {
        subLine += String(ageS / 60UL) + "m";
      }
    }
  } else if (model.hydrometer.selected) {
    subLine = "Hydrometer waiting";
  }
  if (subLine.length() > 0 && !editing) {
    _canvas.setTextColor(COLOR_TEXT_MUTED, bg);
    _canvas.drawString(subLine, cx, cy + 46, &fonts::DejaVu12);
  }

  // 7. fermenter / batch name + output chips in the bottom gap (hidden while
  // the setpoint preview shows its confirm/cancel row in that space).
  if (!editing) {
    _canvas.setTextColor(COLOR_TEXT_MUTED, bg);
    const String nameLine = settings.batchName.length() > 0
                                ? settings.batchName
                                : settings.fermenterName;
    _canvas.drawString(nameLine, cx, cy + 62, &fonts::DejaVu12);
    drawOutputChips(model);
  }

  // Help badge (left) and attention badge (right) — clear of the text stack.
  if (!editing) {
    _canvas.fillSmoothCircle(cx - 84, cy, 13, COLOR_PANEL);
    _canvas.drawCircle(cx - 84, cy, 13, COLOR_TEXT_MUTED);
    drawHelpIcon(cx - 84, cy, COLOR_TEXT_MUTED, COLOR_PANEL);

    if (hint.attention != 0) {
      const uint16_t attnCol =
          (hint.attention & ATTN_FAULT) ? COLOR_FAULT : COLOR_WARN;
      _canvas.fillSmoothCircle(cx + 84, cy, 13, COLOR_PANEL);
      _canvas.drawCircle(cx + 84, cy, 13, attnCol);
      _canvas.setTextDatum(middle_center);
      _canvas.setTextColor(attnCol, COLOR_PANEL);
      _canvas.drawString("!", cx + 84, cy, &fonts::FreeSansBold12pt7b);
    }
  } else {
    // Confirm/cancel row for the previewed setpoint (rects match
    // confirmRowLeftHit / confirmRowRightHit). Press = Set, swipe/timeout = no.
    const bool cancelPressed = pressInRect(cx - 90, cy + 44, 84, 26);
    drawGhostButton(cx - 48, cy + 57, 84, 26, "Cancel",
                    cancelPressed ? TFT_WHITE : COLOR_TEXT_MUTED,
                    &fonts::DejaVu12);
    drawSolidButton(cx + 48, cy + 57, 84, 26, "Set", COLOR_GOLD, TFT_BLACK,
                    &fonts::DejaVu12);
  }

  drawPageDots(0, 2);
}

void DisplayUI::drawHydrometer(uint32_t nowMs, const Settings &settings,
                               const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;
  const bool unitsF = settings.unitsFahrenheit;
  const HydrometerReading &h = model.hydrometer;

  _canvas.fillScreen(COLOR_BG);
  drawWifiIcon(cx, cy - 82, 11, wifiStatus(model.network), COLOR_BG);
  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(COLOR_ACCENT, COLOR_BG);
  _canvas.drawString("HYDROMETER", cx, cy - 70, &fonts::DejaVu18);

  // No hydrometer chosen yet: point at the on-dial picker rather than
  // showing empty "SG --.--" placeholders.
  if (!h.selected) {
    _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _canvas.drawString("None set up yet", cx, cy - 36, &fonts::DejaVu12);

    _canvas.setTextColor(COLOR_TEXT, COLOR_BG);
    _canvas.drawString("Settings > Hydrometer", cx, cy - 6, &fonts::DejaVu12);
    _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _canvas.drawString("or the web dashboard", cx, cy + 16,
                       &fonts::DejaVu12);
    _canvas.drawString("to scan and select one", cx, cy + 38,
                       &fonts::DejaVu12);

    drawPageDots(1, 2);
    return;
  }

  String title = h.selected && h.valid ? h.label : "No hydrometer";
  if (h.selected && h.valid && h.name.length() > 0) {
    title = h.name;
  }
  _canvas.setTextColor(h.valid ? COLOR_TEXT : COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(title, cx, cy - 42, &fonts::DejaVu12);

  String sg = h.valid ? "SG " + String(h.gravity, 3) : "SG --.--";
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(sg, cx, cy - 4, &fonts::FreeSansBold18pt7b);

  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  String tempLine = h.valid ? formatTemperature(h.temperatureC, unitsF)
                            : "Temp --.-";
  if (h.valid) {
    tempLine += "  RSSI ";
    tempLine += String(h.rssi);
    tempLine += " dBm";
  }
  _canvas.drawString(tempLine, cx, cy + 28, &fonts::DejaVu12);

  String metaParts[3];
  uint8_t metaPartCount = 0;
  if (h.valid && gravityIsValid(h.originalGravity)) {
    metaParts[metaPartCount++] = "OG " + String(h.originalGravity, 3);
  }
  if (h.valid && !isnan(h.abv)) {
    metaParts[metaPartCount++] = "ABV " + String(h.abv, 1) + "%";
  }
  if (h.valid && h.stableSeconds > 0) {
    metaParts[metaPartCount++] =
        "Stable " + String(h.stableSeconds / 3600.0f, 1) + "h";
  }

  // The round bezel narrows the usable width away from screen centre, so
  // wrap onto a second line instead of letting a long combined string (e.g.
  // OG + ABV + Stable all at once) get clipped by the circular mask.
  constexpr int16_t META_MAX_WIDTH = 196;
  String metaLine1 = "";
  String metaLine2 = "";
  for (uint8_t i = 0; i < metaPartCount; i++) {
    String &line = metaLine2.length() > 0 ? metaLine2 : metaLine1;
    const String candidate =
        line.length() > 0 ? line + "  " + metaParts[i] : metaParts[i];
    if (line.length() == 0 ||
        _canvas.textWidth(candidate, &fonts::DejaVu12) <= META_MAX_WIDTH) {
      line = candidate;
    } else {
      metaLine2 = metaParts[i];
    }
  }
  if (metaLine1.length() == 0) {
    metaLine1 = h.selected ? (h.stale ? "stale" : "waiting") : "No selection";
  } else if (h.valid && h.stale) {
    String &line = metaLine2.length() > 0 ? metaLine2 : metaLine1;
    line += "  stale";
  }
  _canvas.drawString(metaLine1, cx, cy + 50, &fonts::DejaVu12);
  if (metaLine2.length() > 0) {
    _canvas.drawString(metaLine2, cx, cy + 70, &fonts::DejaVu12);
  }

  if (h.valid && h.address.length() > 0) {
    _canvas.drawString(h.address, cx, metaLine2.length() > 0 ? cy + 90 : cy + 76,
                       &fonts::DejaVu12);
  }

  drawPageDots(1, 2);
}

void DisplayUI::drawQuickMenu(const Settings &settings, const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;
  const uint8_t count = quickActionCount(settings);
  const uint8_t idx = _quickIndex % count;
  const QuickAction action = quickActionAt(idx, settings);
  const QuickAction prev =
      quickActionAt(static_cast<uint8_t>((idx + count - 1) % count), settings);
  const QuickAction next =
      quickActionAt(static_cast<uint8_t>((idx + 1) % count), settings);

  drawMain(_lastDrawMs, settings, model);
  _canvas.setTextDatum(middle_center);
  _canvas.fillSmoothRoundRect(cx - 100, cy - 84, 200, 152, 15, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 100, cy - 84, 200, 152, 15, COLOR_BLUE);
  _canvas.setTextColor(COLOR_BLUE, COLOR_PANEL);
  _canvas.drawString("QUICK", cx, cy - 64, &fonts::DejaVu18);

  // Prev / focused / next — same carousel pattern as profile and mode pickers.
  _canvas.setTextColor(rgb565(86, 106, 118), COLOR_PANEL);
  _canvas.drawString(quickActionLabel(prev), cx, cy - 36, &fonts::DejaVu12);
  _canvas.drawString(quickActionLabel(next), cx, cy + 36, &fonts::DejaVu12);

  _canvas.fillSmoothRoundRect(cx - 90, cy - 22, 180, 48, 14, COLOR_BG);
  _canvas.drawRoundRect(cx - 90, cy - 22, 180, 48, 14, COLOR_GOLD);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(quickActionLabel(action), cx, cy - 6,
                     &fonts::FreeSansBold12pt7b);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(quickActionValue(action, settings), cx, cy + 13,
                     &fonts::DejaVu18);

  drawGhostButton(cx, cy + 54, 64, 20, "Cancel", COLOR_TEXT_MUTED, &fonts::DejaVu12);
}

void DisplayUI::drawQuickProfile(const Settings &settings, const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;
  const uint8_t prev = _pendingProfile == 0 ? PROFILE_COUNT - 1
                                            : _pendingProfile - 1;
  const uint8_t next = (_pendingProfile + 1) % PROFILE_COUNT;
  const ProfileSettings &profile = settings.profiles[_pendingProfile];

  drawMain(_lastDrawMs, settings, model);
  _canvas.setTextDatum(middle_center);
  _canvas.fillSmoothRoundRect(cx - 100, cy - 84, 200, 152, 15, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 100, cy - 84, 200, 152, 15, COLOR_BLUE);
  _canvas.setTextColor(COLOR_BLUE, COLOR_PANEL);
  _canvas.drawString("PROFILE", cx, cy - 64, &fonts::DejaVu18);

  _canvas.setTextColor(rgb565(86, 106, 118), COLOR_PANEL);
  _canvas.drawString(settings.profiles[prev].name, cx, cy - 36,
                     &fonts::DejaVu12);
  _canvas.drawString(settings.profiles[next].name, cx, cy + 36,
                     &fonts::DejaVu12);

  _canvas.fillSmoothRoundRect(cx - 90, cy - 22, 180, 48, 14, COLOR_BG);
  _canvas.drawRoundRect(cx - 90, cy - 22, 180, 48, 14, COLOR_BLUE);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(profile.name, cx, cy - 6, &fonts::FreeSansBold12pt7b);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(
      formatTemperature(profile.targetC, settings.unitsFahrenheit), cx,
      cy + 13, &fonts::DejaVu18);

  drawGhostButton(cx, cy + 54, 64, 20, "Cancel", COLOR_TEXT_MUTED, &fonts::DejaVu12);
}

void DisplayUI::drawQuickMode(const Settings &settings, const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;
  int32_t prev = static_cast<int32_t>(_pendingMode) - 1;
  if (prev < 0) {
    prev += 4;
  }
  const int32_t next = (static_cast<int32_t>(_pendingMode) + 1) % 4;

  drawMain(_lastDrawMs, settings, model);
  _canvas.setTextDatum(middle_center);
  _canvas.fillSmoothRoundRect(cx - 100, cy - 84, 200, 152, 15, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 100, cy - 84, 200, 152, 15, COLOR_BLUE);
  _canvas.setTextColor(COLOR_BLUE, COLOR_PANEL);
  _canvas.drawString("MODE", cx, cy - 64, &fonts::DejaVu18);

  _canvas.setTextColor(rgb565(86, 106, 118), COLOR_PANEL);
  _canvas.drawString(modeText(static_cast<UserMode>(prev)), cx, cy - 36,
                     &fonts::DejaVu12);
  _canvas.drawString(modeText(static_cast<UserMode>(next)), cx, cy + 36,
                     &fonts::DejaVu12);

  _canvas.fillSmoothRoundRect(cx - 90, cy - 22, 180, 48, 14, COLOR_BG);
  _canvas.drawRoundRect(cx - 90, cy - 22, 180, 48, 14, COLOR_BLUE);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(modeText(_pendingMode), cx, cy + 2,
                     &fonts::FreeSansBold18pt7b);

  drawGhostButton(cx, cy + 54, 64, 20, "Cancel", COLOR_TEXT_MUTED, &fonts::DejaVu12);
  (void)settings;
}

void DisplayUI::drawQuickConfirm(const Settings &settings, const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  drawMain(_lastDrawMs, settings, model);
  _canvas.setTextDatum(middle_center);
  _canvas.fillSmoothRoundRect(cx - 102, cy - 84, 204, 156, 15, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 102, cy - 84, 204, 156, 15, COLOR_GOLD);
  _canvas.setTextColor(COLOR_GOLD, COLOR_PANEL);
  const char *confirmTitle =
      _quickConfirmKind == QuickConfirmKind::CrashGradual  ? "COLD CRASH"
      : _quickConfirmKind == QuickConfirmKind::ProgramControl ? "PROGRAM"
      : _pendingQuickAction == QuickAction::DRest          ? "D-REST"
                                                           : "CONFIRM";
  _canvas.drawString(confirmTitle, cx, cy - 64, &fonts::DejaVu18);

  _canvas.fillSmoothRoundRect(cx - 92, cy - 36, 184, 62, 14, COLOR_BG);
  _canvas.drawRoundRect(cx - 92, cy - 36, 184, 62, 14, COLOR_GOLD);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  if (_quickConfirmKind == QuickConfirmKind::CrashGradual) {
    _canvas.drawString("Step down?", cx, cy - 16, &fonts::FreeSansBold12pt7b);
    _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _canvas.drawString(formatTemperature(settings.profiles[static_cast<uint8_t>(
                                               ProfileSlot::Crash)]
                                             .targetC,
                                         settings.unitsFahrenheit),
                       cx, cy + 6, &fonts::DejaVu12);
    drawSolidButton(cx - 48, cy + 57, 84, 26, "Direct",
                    _pendingGradualCrash ? COLOR_BG : COLOR_GOLD,
                    _pendingGradualCrash ? COLOR_TEXT_MUTED : TFT_BLACK,
                    &fonts::DejaVu12,
                    _pendingGradualCrash ? COLOR_TEXT_MUTED : 0);
    drawSolidButton(cx + 48, cy + 57, 84, 26, "Gradual",
                    _pendingGradualCrash ? COLOR_GOLD : COLOR_BG,
                    _pendingGradualCrash ? TFT_BLACK : COLOR_TEXT_MUTED,
                    &fonts::DejaVu12,
                    _pendingGradualCrash ? 0 : COLOR_TEXT_MUTED);
  } else if (_quickConfirmKind == QuickConfirmKind::ProgramControl) {
    const uint8_t ri =
        settings.programRunIndex < PROGRAM_SLOT_COUNT ? settings.programRunIndex
                                                      : 0;
    _canvas.drawString(
        String("Step ") + String(settings.programStepIndex + 1) + "/" +
            String(settings.programs[ri].stepCount),
        cx, cy - 16, &fonts::FreeSansBold12pt7b);
    _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _canvas.drawString("Skip step or stop", cx, cy + 6, &fonts::DejaVu12);
    drawSolidButton(cx - 48, cy + 57, 84, 26, "Skip",
                    _pendingProgramSkip ? COLOR_GOLD : COLOR_BG,
                    _pendingProgramSkip ? TFT_BLACK : COLOR_TEXT_MUTED,
                    &fonts::DejaVu12,
                    _pendingProgramSkip ? 0 : COLOR_TEXT_MUTED);
    drawSolidButton(cx + 48, cy + 57, 84, 26, "Stop",
                    _pendingProgramSkip ? COLOR_BG : COLOR_GOLD,
                    _pendingProgramSkip ? COLOR_TEXT_MUTED : TFT_BLACK,
                    &fonts::DejaVu12,
                    _pendingProgramSkip ? COLOR_TEXT_MUTED : 0);
  } else {
    _canvas.drawString(quickActionLabel(_pendingQuickAction), cx, cy - 16,
                       &fonts::FreeSansBold12pt7b);
    _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _canvas.drawString(quickPendingValue(settings), cx, cy + 6,
                       &fonts::DejaVu12);
    if (_pendingQuickAction == QuickAction::DRest &&
        !settings.diacetylRestActive) {
      // Discoverability: duration is adjustable on this confirm only.
      _canvas.drawString("turn dial: hours", cx, cy + 26, &fonts::DejaVu12);
    }

    // Action row: Cancel (left, ghost) and Apply (right, filled gold).
    drawGhostButton(cx - 48, cy + 57, 84, 26, "Cancel", COLOR_TEXT_MUTED,
                    &fonts::DejaVu12);
    drawSolidButton(cx + 48, cy + 57, 84, 26, "Apply", COLOR_GOLD, TFT_BLACK,
                    &fonts::DejaVu12);
  }
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

void DisplayUI::drawHelpIcon(int16_t cx, int16_t cy, uint16_t color,
                             uint16_t bg) {
  // Blit the pre-rendered DejaVu Sans Bold "?" alpha mask, centred and
  // anti-aliased over the badge fill.
  const int16_t x0 = cx - HELP_GLYPH_W / 2;
  const int16_t y0 = cy - HELP_GLYPH_H / 2;
  for (int16_t gy = 0; gy < HELP_GLYPH_H; gy++) {
    for (int16_t gx = 0; gx < HELP_GLYPH_W; gx++) {
      const uint8_t a = HELP_GLYPH_ALPHA[gy * HELP_GLYPH_W + gx];
      if (a == 0) {
        continue;
      }
      _canvas.drawPixel(x0 + gx, y0 + gy, blend565(color, bg, a));
    }
  }
}

void DisplayUI::drawPageDots(uint8_t activePage, uint8_t pageCount) {
  if (pageCount == 0) {
    return;
  }
  const int16_t cx = _canvas.width() / 2;
  const int16_t y = _canvas.height() - 14;
  const int16_t gap = 14;
  const int16_t startX = cx - ((static_cast<int16_t>(pageCount) - 1) * gap) / 2;
  for (uint8_t i = 0; i < pageCount; ++i) {
    const bool active = i == activePage;
    _canvas.fillSmoothCircle(startX + i * gap, y, active ? 3 : 2,
                             active ? COLOR_GOLD : COLOR_TRACK);
  }
}

void DisplayUI::drawOutputChips(const UiModel &model) {
  const int16_t cx = _canvas.width() / 2;
  // Sits below the fermenter name (cy + 62) with clear separation so the
  // chip circles no longer overlap the name's glyphs.
  const int16_t y = _canvas.height() / 2 + 86;
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
    // Letter under the chip (icons alone are ambiguous for first-time guests).
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(on[i] ? col[i] : COLOR_TEXT_MUTED, COLOR_BG);
    _canvas.drawString(i == 0 ? "H" : "C", x, y + 18, &fonts::DejaVu12);
  }
  if (model.demoSensor) {
    // Above the right-side attention column (cx+84, mid height) so "!" can share
    // the same radial slot when attention is active.
    _canvas.setTextDatum(middle_center);
    _canvas.setTextColor(COLOR_GOLD, COLOR_BG);
    _canvas.drawString("DEMO", cx + 84, _canvas.height() / 2 - 28,
                       &fonts::DejaVu12);
  }
}

void DisplayUI::drawMenu(const Settings &settings,
                         const NetworkSnapshot &network) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  const uint8_t listCount =
      _menuInGroup ? groupItemCount(_menuGroup) : GROUP_COUNT;
  const uint8_t listPos =
      _menuInGroup ? groupItemPos(_menuGroup, _menuIndex) : _menuGroup;
  const uint8_t prevPos = listPos == 0 ? listCount - 1 : listPos - 1;
  const uint8_t nextPos = static_cast<uint8_t>((listPos + 1) % listCount);

  // Position dots around the arc for the current list (groups or items).
  for (uint8_t i = 0; i < listCount; i++) {
    const float a =
        GA_START +
        (listCount > 1 ? static_cast<float>(i) / (listCount - 1) : 0.0f) *
            GA_SWEEP;
    int16_t x, y;
    polar(104, a, x, y);
    const bool cur = i == listPos;
    _canvas.fillSmoothCircle(x, y, cur ? 4 : 2, cur ? COLOR_GOLD : COLOR_TRACK);
  }

  String focusLabel;
  String focusValue;
  String prevLabel;
  String nextLabel;
  if (!_menuInGroup) {
    focusLabel = GROUP_LABELS[_menuGroup];
    prevLabel = GROUP_LABELS[prevPos];
    nextLabel = GROUP_LABELS[nextPos];
    switch (_menuGroup) {
    case GROUP_CONTROL:
      focusValue = "Bands & pump";
      break;
    case GROUP_SYSTEM:
      focusValue = network.wifiEnabled ? network.status : "Device";
      break;
    default:
      focusValue = activeProfile(settings).name;
      break;
    }
  } else {
    const MenuIndex *items = groupItems(_menuGroup);
    focusLabel = MENU_LABELS[_menuIndex];
    focusValue = menuValue(_menuIndex, settings, network);
    prevLabel = MENU_LABELS[items[prevPos]];
    nextLabel = MENU_LABELS[items[nextPos]];
  }

  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(COLOR_BLUE, COLOR_BG);
  if (_menuInGroup) {
    _canvas.drawString(GROUP_LABELS[_menuGroup], cx, cy - 74, &fonts::DejaVu18);
  } else {
    _canvas.drawString("SETTINGS", cx, cy - 74, &fonts::DejaVu18);
  }

  _canvas.setTextColor(rgb565(86, 106, 118), COLOR_BG);  // dim neighbours
  _canvas.drawString(prevLabel, cx, cy - 44, &fonts::DejaVu12);
  _canvas.drawString(nextLabel, cx, cy + 48, &fonts::DejaVu12);

  // focused item card
  _canvas.fillSmoothRoundRect(cx - 94, cy - 26, 188, 52, 14, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 94, cy - 26, 188, 52, 14, COLOR_BLUE);
  _canvas.setTextColor(TFT_WHITE, COLOR_PANEL);
  _canvas.drawString(focusLabel, cx, cy - 9, &fonts::FreeSansBold12pt7b);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL);
  _canvas.drawString(focusValue, cx, cy + 13, &fonts::DejaVu18);

  String hint = "swipe up/down";
  if (!_menuInGroup) {
    hint = "tap to open";
  } else if (_menuIndex == MENU_DREST) {
    hint = settings.diacetylRestActive ? "tap to finish" : "tap to start";
  } else if (_menuIndex == MENU_UNITS) {
    hint = "tap to toggle";
  } else if (_menuIndex == MENU_WIFI) {
    if (!network.wifiEnabled) {
      hint = "network disabled";
    } else {
      hint = network.status == "Setup AP" ? "AP is active" : "tap to start AP";
    }
  } else if (_menuIndex == MENU_MQTT) {
    if (!network.wifiEnabled) {
      hint = "network disabled";
    } else if (!network.mqttEnabled) {
      hint = "enable in web UI";
    } else {
      hint = network.mqttConnected ? "broker connected" : "connecting...";
    }
  } else if (_menuIndex == MENU_BRIGHTNESS) {
    hint = "tap to adjust";
  }
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(hint, cx, cy + 78, &fonts::DejaVu12);
}

void DisplayUI::drawEdit(const Settings &settings) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(COLOR_BLUE, COLOR_BG);
  _canvas.drawString(MENU_LABELS[_editIndex], cx, cy - 68, &fonts::DejaVu18);

  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(_editIndex == MENU_DREST
                         ? diacetylRestRemainingText(settings)
                         : menuValue(_editIndex, settings),
                     cx, cy - 30, &fonts::FreeSansBold18pt7b);

  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(editDefaultLine(settings), cx, cy - 2, &fonts::DejaVu12);

  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(
      _editIndex == MENU_PROFILE || _editIndex == MENU_HYDROMETER
          ? "swipe/rotate to choose"
          : _editIndex == MENU_DREST
                ? "swipe/rotate to set hours"
                : "swipe/rotate to change",
      cx, cy + 18, &fonts::DejaVu12);

  const int16_t topY = cy + 34;
  const int16_t backY = cy + 66;
  const int16_t buttonW = 82;
  const int16_t buttonH = 24;
  const char *commitLabel = editCommitLabel();
  if (editHasReset(settings)) {
    drawSolidButton(cx - 47, topY + 12, buttonW, buttonH, "Reset", COLOR_PANEL,
                    COLOR_ACCENT, &fonts::DejaVu12, COLOR_BLUE);
    drawSolidButton(cx + 47, topY + 12, buttonW, buttonH, commitLabel,
                    COLOR_BLUE, TFT_WHITE, &fonts::DejaVu12);
  } else {
    drawSolidButton(cx, topY + 12, 110, buttonH, commitLabel, COLOR_BLUE,
                    TFT_WHITE, &fonts::DejaVu12);
  }

  drawGhostButton(cx, backY + 12, 96, 24, "Back", COLOR_TEXT_MUTED, &fonts::DejaVu12);
}

void DisplayUI::drawEditInfo(const Settings &settings) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(COLOR_BLUE, COLOR_BG);
  _canvas.drawString(MENU_LABELS[_editIndex], cx, cy - 68, &fonts::DejaVu18);

  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(formatTemperature(currentTargetC(settings),
                                     settings.unitsFahrenheit),
                     cx, cy - 30, &fonts::FreeSansBold18pt7b);

  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString("Live setpoint", cx, cy - 2, &fonts::DejaVu12);
  _canvas.drawString("Built-in preset - adjust on main screen", cx, cy + 18,
                     &fonts::DejaVu12);

  const int16_t backY = cy + 66;
  drawGhostButton(cx, backY + 12, 96, 24, "Back", COLOR_TEXT_MUTED, &fonts::DejaVu12);
}

void DisplayUI::drawConfirmEdit(const Settings &settings) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;
  const bool reset = _pendingEditConfirm == EditConfirmAction::Reset;

  const uint16_t accent = reset ? COLOR_GOLD : COLOR_BLUE;

  // Mirror QuickConfirm's layout so both confirm screens read identically.
  _canvas.setTextDatum(middle_center);
  _canvas.fillSmoothRoundRect(cx - 102, cy - 84, 204, 156, 15, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 102, cy - 84, 204, 156, 15, accent);
  _canvas.setTextColor(accent, COLOR_PANEL);
  const char *confirmTitle =
      reset ? "Confirm reset"
            : (_editIndex == MENU_DREST ? "Confirm start"
                                        : editCommitsProfile() ? "Confirm apply"
                                                               : "Confirm save");
  _canvas.drawString(confirmTitle, cx, cy - 64, &fonts::DejaVu18);

  _canvas.fillSmoothRoundRect(cx - 92, cy - 36, 184, 62, 14, COLOR_BG);
  _canvas.drawRoundRect(cx - 92, cy - 36, 184, 62, 14, accent);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(MENU_LABELS[_editIndex], cx, cy - 16,
                     &fonts::FreeSansBold12pt7b);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(editConfirmLine(settings), cx, cy + 6, &fonts::DejaVu12);

  // Action row: Cancel (left, ghost) and Reset/Apply/Save (right, filled).
  const char *commitLabel = reset ? "Reset" : editCommitLabel();
  drawGhostButton(cx - 48, cy + 57, 84, 26, "Cancel", COLOR_TEXT_MUTED,
                  &fonts::DejaVu12);
  drawSolidButton(cx + 48, cy + 57, 84, 26, commitLabel, accent,
                  reset ? TFT_BLACK : TFT_WHITE, &fonts::DejaVu12);
}

void DisplayUI::drawConfirmTest() {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  // Mirror QuickConfirm/ConfirmEdit's layout so every confirm screen reads
  // identically; Start only fires from its button (see handleTouchClick).
  _canvas.setTextDatum(middle_center);
  _canvas.fillSmoothRoundRect(cx - 102, cy - 84, 204, 156, 15, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 102, cy - 84, 204, 156, 15, COLOR_WARN);
  _canvas.setTextColor(COLOR_WARN, COLOR_PANEL);
  _canvas.drawString("Confirm test", cx, cy - 64, &fonts::DejaVu18);

  _canvas.fillSmoothRoundRect(cx - 92, cy - 36, 184, 62, 14, COLOR_BG);
  _canvas.drawRoundRect(cx - 92, cy - 36, 184, 62, 14, COLOR_WARN);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString(MENU_LABELS[_menuIndex], cx, cy - 20,
                     &fonts::FreeSansBold12pt7b);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  _canvas.drawString(_menuIndex == MENU_HEATER_TEST ? "Heater ON for 5 sec"
                                                    : "Pump ON for 5 sec",
                     cx, cy + 2, &fonts::DejaVu12);
  _canvas.drawString("Mode cannot be OFF", cx, cy + 18, &fonts::DejaVu12);

  // Action row: Cancel (left, ghost) and Start (right, filled warn).
  drawGhostButton(cx - 48, cy + 57, 84, 26, "Cancel", COLOR_TEXT_MUTED,
                  &fonts::DejaVu12);
  drawSolidButton(cx + 48, cy + 57, 84, 26, "Start", COLOR_WARN, TFT_BLACK,
                  &fonts::DejaVu12);
}

void DisplayUI::drawConfirmWifi(const NetworkSnapshot &network) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  _canvas.setTextDatum(middle_center);
  _canvas.fillSmoothRoundRect(cx - 102, cy - 84, 204, 156, 15, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 102, cy - 84, 204, 156, 15, COLOR_WARN);
  _canvas.setTextColor(COLOR_WARN, COLOR_PANEL);
  _canvas.drawString("Wi-Fi setup", cx, cy - 64, &fonts::DejaVu18);

  _canvas.fillSmoothRoundRect(cx - 92, cy - 36, 184, 62, 14, COLOR_BG);
  _canvas.drawRoundRect(cx - 92, cy - 36, 184, 62, 14, COLOR_WARN);
  _canvas.setTextColor(TFT_WHITE, COLOR_BG);
  _canvas.drawString("Start setup AP?", cx, cy - 16, &fonts::FreeSansBold12pt7b);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  if (network.ipAddress.length() > 0) {
    _canvas.drawString(network.ipAddress, cx, cy + 6, &fonts::DejaVu12);
  } else {
    _canvas.drawString("Drops home Wi-Fi", cx, cy + 6, &fonts::DejaVu12);
  }

  drawGhostButton(cx - 48, cy + 57, 84, 26, "Cancel", COLOR_TEXT_MUTED,
                  &fonts::DejaVu12);
  drawSolidButton(cx + 48, cy + 57, 84, 26, "Start", COLOR_WARN, TFT_BLACK,
                  &fonts::DejaVu12);
}

void DisplayUI::drawAbout(const NetworkSnapshot &network) {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  drawArcSeg(GAUGE_R, GA_START, GA_START + GA_SWEEP, COLOR_TRACK, 5);
  drawArcSeg(GAUGE_R, GA_START, GA_START + 60.0f, COLOR_COOL, 5);
  drawArcSeg(GAUGE_R, GA_START + GA_SWEEP - 60.0f, GA_START + GA_SWEEP,
             COLOR_HEAT, 5);

  _canvas.fillSmoothRoundRect(cx - 100, cy - 62, 200, 124, 14, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 100, cy - 62, 200, 124, 14, COLOR_BLUE);

  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(COLOR_ACCENT, COLOR_PANEL);
  _canvas.drawString(FIRMWARE_NAME, cx, cy - 46, &fonts::DejaVu18);
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL);
  _canvas.drawString(String("v") + FIRMWARE_VERSION + " - " + FIRMWARE_GIT_SHA,
                     cx, cy - 24, &fonts::DejaVu12);
  _canvas.setTextColor(COLOR_TEXT, COLOR_PANEL);
  if (network.hostname.length() > 0) {
    _canvas.drawString(network.hostname, cx, cy - 2, &fonts::DejaVu12);
  } else {
    _canvas.drawString("M5Stack Dial", cx, cy - 2, &fonts::DejaVu12);
  }
  if (network.ipAddress.length() > 0) {
    _canvas.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    _canvas.drawString(network.ipAddress, cx, cy + 20, &fonts::DejaVu12);
  } else {
    _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL);
    _canvas.drawString(network.status.length() ? network.status : "No IP", cx,
                       cy + 20, &fonts::DejaVu12);
  }
  _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL);
  _canvas.drawString("ESP32-S3", cx, cy + 42, &fonts::DejaVu12);
}

void DisplayUI::drawHelp() {
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;

  _canvas.setTextDatum(middle_center);
  _canvas.fillSmoothRoundRect(cx - 104, cy - 86, 208, 172, 16, COLOR_PANEL);
  _canvas.drawRoundRect(cx - 104, cy - 86, 208, 172, 16, COLOR_BLUE);
  _canvas.setTextColor(COLOR_BLUE, COLOR_PANEL);
  _canvas.drawString("HELP", cx, cy - 66, &fonts::DejaVu18);

  // Gesture legend: how to drive the controller from the main screen.
  const char *gestures[][2] = {
      {"Turn dial", "Set temp"},
      {"Tap screen", "Quick menu"},
      {"Swipe L/R", "Pages"},
      {"Press knob", "Settings"},
      {"Long press", "Back"},
      {"Tap !", "Alerts"},
  };
  int16_t y = cy - 46;
  for (auto &g : gestures) {
    _canvas.setTextDatum(middle_left);
    _canvas.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    _canvas.drawString(g[0], cx - 86, y, &fonts::DejaVu12);
    _canvas.setTextDatum(middle_right);
    _canvas.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL);
    _canvas.drawString(g[1], cx + 86, y, &fonts::DejaVu12);
    y += 20;
  }

  _canvas.setTextDatum(middle_center);
  drawGhostButton(cx, cy + 70, 84, 24, "Close", COLOR_TEXT_MUTED,
                  &fonts::DejaVu12);
}

bool DisplayUI::pressInRect(int16_t x, int16_t y, int16_t w, int16_t h) const {
  return _pressX >= x && _pressX < x + w && _pressY >= y && _pressY < y + h;
}

void DisplayUI::drawGhostButton(int16_t cx, int16_t cy, int16_t w, int16_t h,
                                const String &text, uint16_t outline,
                                const lgfx::IFont *font) {
  const int16_t x = cx - w / 2;
  const int16_t y = cy - h / 2;
  // Dark ghost button: lift the fill toward the panel tone when pressed so the
  // (already dark) control still reads as activated.
  const uint16_t fill = pressInRect(x, y, w, h) ? COLOR_PANEL : COLOR_PANEL_DARK;
  _canvas.fillSmoothRoundRect(x, y, w, h, h / 2, fill);
  _canvas.drawRoundRect(x, y, w, h, h / 2, outline);
  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(outline, fill);
  _canvas.drawString(text, cx, cy, font);
}

void DisplayUI::drawSolidButton(int16_t cx, int16_t cy, int16_t w, int16_t h,
                                const String &text, uint16_t fill,
                                uint16_t textColor, const lgfx::IFont *font,
                                uint16_t outline) {
  const int16_t x = cx - w / 2;
  const int16_t y = cy - h / 2;
  const uint16_t shown = pressInRect(x, y, w, h) ? dim565(fill) : fill;
  _canvas.fillSmoothRoundRect(x, y, w, h, h / 2, shown);
  if (outline != 0) {
    _canvas.drawRoundRect(x, y, w, h, h / 2, outline);
  }
  _canvas.setTextDatum(middle_center);
  _canvas.setTextColor(textColor, shown);
  _canvas.drawString(text, cx, cy, font);
}

bool DisplayUI::quickCancelHit(int16_t x, int16_t y) const {
  if (_screen == Screen::QuickConfirm) {
    return confirmRowLeftHit(x, y);
  }
  const int16_t cx = _canvas.width() / 2;
  const int16_t cy = _canvas.height() / 2;
  return x >= cx - 32 && x <= cx + 32 && y >= cy + 44 && y <= cy + 64;
}

String DisplayUI::temperatureNumber(float tempC, bool unitsFahrenheit) const {
  if (isnan(tempC)) {
    return "--.-";
  }
  return String(toDisplayTemp(tempC, unitsFahrenheit), 1);
}

const char *DisplayUI::temperatureUnit(bool unitsFahrenheit) const {
  return unitLabel(unitsFahrenheit);
}

String DisplayUI::formatTemperature(float tempC, bool unitsFahrenheit) const {
  if (isnan(tempC)) {
    return "--.-";
  }
  return String(toDisplayTemp(tempC, unitsFahrenheit), 1) +
         temperatureUnit(unitsFahrenheit);
}

String DisplayUI::diacetylRestRemainingText(const Settings &settings) const {
  const uint32_t seconds = settings.diacetylRestActive
                               ? settings.diacetylRestRemainingSeconds
                               : settings.diacetylRestDurationSeconds;
  const uint32_t hours = seconds / 3600UL;
  const uint32_t minutes = (seconds % 3600UL) / 60UL;
  if (hours > 0 && minutes > 0) {
    return String(hours) + "h " + String(minutes) + "m";
  }
  if (hours > 0) {
    return String(hours) + "h";
  }
  return String(minutes) + "m";
}

uint8_t DisplayUI::quickActionCount(const Settings &settings) const {
  return settings.programActive ? QUICK_ACTION_MAX : QUICK_ACTION_BASE;
}

DisplayUI::QuickAction DisplayUI::quickActionAt(
    uint8_t index, const Settings &settings) const {
  // Base order: Profile, Mode, D-Rest; Program is last while a program runs.
  if (settings.programActive && index == QUICK_ACTION_BASE) {
    return QuickAction::Program;
  }
  switch (index % QUICK_ACTION_BASE) {
  case 1:
    return QuickAction::Mode;
  case 2:
    return QuickAction::DRest;
  default:
    return QuickAction::Profile;
  }
}

const char *DisplayUI::quickActionLabel(QuickAction action) const {
  switch (action) {
  case QuickAction::Mode:
    return "Mode";
  case QuickAction::DRest:
    return "D-Rest";
  case QuickAction::Program:
    return "Program";
  default:
    return "Profile";
  }
}

String DisplayUI::quickActionValue(QuickAction action,
                                   const Settings &settings) const {
  if (action == QuickAction::Profile) {
    return activeProfile(settings).name;
  }
  if (action == QuickAction::Mode) {
    return modeText(settings.mode);
  }
  if (action == QuickAction::Program) {
    const uint8_t ri =
        settings.programRunIndex < PROGRAM_SLOT_COUNT ? settings.programRunIndex
                                                      : 0;
    return String(settings.programStepIndex + 1) + "/" +
           String(settings.programs[ri].stepCount);
  }
  // D-Rest: show remaining time when active, otherwise the configured duration.
  return settings.diacetylRestActive
             ? diacetylRestRemainingText(settings)
             : String("Start ") + diacetylRestRemainingText(settings);
}

String DisplayUI::quickPendingValue(const Settings &settings) const {
  if (_pendingQuickAction == QuickAction::Profile) {
    return String("Use ") + settings.profiles[_pendingProfile].name + " (" +
           formatTemperature(settings.profiles[_pendingProfile].targetC,
                             settings.unitsFahrenheit) +
           ")";
  }
  if (_pendingQuickAction == QuickAction::Mode) {
    return String("Use ") + modeText(_pendingMode) + " mode";
  }
  if (_pendingQuickAction == QuickAction::Program) {
    return _pendingProgramSkip ? "Skip this step" : "Stop program";
  }
  if (settings.diacetylRestActive) {
    // completeDiacetylRest activates the configured return profile, not the
    // currently active one (dashboard "After rest" may differ).
    return String("Finish -> ") +
           settings.profiles[diacetylRestReturnProfileIndex(settings)].name;
  }
  // Start path: show pending duration + return profile.
  const uint32_t hours = _pendingDrestDurationSeconds / 3600UL;
  return String("Start ") + String(hours) + "h then " +
         settings.profiles[diacetylRestReturnProfileIndex(settings)].name;
}

String DisplayUI::menuValue(uint8_t index, const Settings &settings,
                            const NetworkSnapshot &network) const {
  switch (index) {
  case MENU_PROFILE:
    return activeProfile(settings).name;
  case MENU_TARGET:
    return formatTemperature(activeTargetC(settings), settings.unitsFahrenheit);
  case MENU_DREST:
    return settings.diacetylRestActive
               ? diacetylRestRemainingText(settings)
               : String("Start ") + diacetylRestRemainingText(settings);
  case MENU_MODE:
    return modeText(settings.mode);
  case MENU_COOL_ON:
    return String(toDisplayDelta(settings.coolOnDeltaC, settings.unitsFahrenheit),
                  1) +
           temperatureUnit(settings.unitsFahrenheit);
  case MENU_HEAT_ON:
    return String(toDisplayDelta(settings.heatOnDeltaC, settings.unitsFahrenheit),
                  1) +
           temperatureUnit(settings.unitsFahrenheit);
  case MENU_HOLD_BAND:
    return String(toDisplayDelta(settings.holdDeltaC, settings.unitsFahrenheit),
                  1) +
           temperatureUnit(settings.unitsFahrenheit);
  case MENU_COOLING_OFF:
    return String(settings.pumpMinOffSeconds) + "s";
  case MENU_COOLING_RUN:
    return String(settings.pumpMinRunSeconds) + "s";
  case MENU_OFFSET:
    return String(toDisplayDelta(settings.tempOffsetC, settings.unitsFahrenheit),
                  1) +
           temperatureUnit(settings.unitsFahrenheit);
  case MENU_UNITS:
    return temperatureUnit(settings.unitsFahrenheit);
  case MENU_WIFI:
    if (!network.wifiEnabled) {
      return "Disabled";
    }
    return network.status;
  case MENU_MQTT:
    if (!network.wifiEnabled) {
      return "Offline";
    }
    if (!network.mqttEnabled) {
      return "Off";
    }
    return network.mqttConnected ? "Connected" : "Connecting";
  case MENU_HYDROMETER:
    return hydrometerValueText(settings);
  case MENU_BRIGHTNESS:
    return String((settings.brightness * 100) / 255) + "%";
  case MENU_HEATER_TEST:
  case MENU_PUMP_TEST:
    return "5s";
  case MENU_ABOUT:
    return FIRMWARE_VERSION;
  default:
    return "";
  }
}

String DisplayUI::defaultMenuValue(uint8_t index,
                                   const Settings &settings) const {
  switch (index) {
  case MENU_PROFILE:
  case MENU_TARGET:
    return formatTemperature(defaultProfileTargetC(activeProfileIndex(settings)),
                             settings.unitsFahrenheit);
  case MENU_DREST: {
    const uint32_t hours = DEFAULT_DIACETYL_REST_DURATION_SECONDS / 3600UL;
    return String(hours) + "h";
  }
  case MENU_MODE:
    return modeText(UserMode::Off);
  case MENU_COOL_ON:
    return String(
               toDisplayDelta(DEFAULT_COOL_ON_DELTA_C, settings.unitsFahrenheit),
               1) +
           temperatureUnit(settings.unitsFahrenheit);
  case MENU_HEAT_ON:
    return String(
               toDisplayDelta(DEFAULT_HEAT_ON_DELTA_C, settings.unitsFahrenheit),
               1) +
           temperatureUnit(settings.unitsFahrenheit);
  case MENU_HOLD_BAND:
    return String(
               toDisplayDelta(DEFAULT_HOLD_DELTA_C, settings.unitsFahrenheit),
               1) +
           temperatureUnit(settings.unitsFahrenheit);
  case MENU_COOLING_OFF:
    return String(DEFAULT_PUMP_MIN_OFF_SECONDS) + "s";
  case MENU_COOLING_RUN:
    return String(DEFAULT_PUMP_MIN_RUN_SECONDS) + "s";
  case MENU_OFFSET:
    return String(
               toDisplayDelta(DEFAULT_TEMP_OFFSET_C, settings.unitsFahrenheit),
               1) +
           temperatureUnit(settings.unitsFahrenheit);
  case MENU_BRIGHTNESS:
    return String((DEFAULT_BRIGHTNESS * 100) / 255) + "%";
  default:
    return "";
  }
}

String DisplayUI::hydrometerValueText(const Settings &settings) const {
  if (settings.hydrometerSelectionKey.length() > 0) {
    for (uint8_t i = 0; i < _hydroDeviceCount; i++) {
      if (_hydroDevices[i].key == settings.hydrometerSelectionKey) {
        const HydrometerReading &dev = _hydroDevices[i];
        String label = dev.name.length() > 0 ? dev.name : dev.label;
        if (label.length() == 0) {
          label = "Device";
        }
        if (gravityIsValid(dev.gravity)) {
          label += " " + String(dev.gravity, 3);
        }
        return label;
      }
    }
    return "Selected (waiting)";
  }
  switch (settings.hydrometerScanType) {
  case HydrometerScanType::Tilt:
  case HydrometerScanType::Rapt:
  case HydrometerScanType::All:
    return "On";
  default:
    return "Off";
  }
}

int32_t DisplayUI::hydrometerOptionIndexFromSettings(
    const Settings &settings) const {
  if (settings.hydrometerSelectionKey.length() > 0) {
    for (uint8_t i = 0; i < _hydroDeviceCount; i++) {
      if (_hydroDevices[i].key == settings.hydrometerSelectionKey) {
        return 2 + static_cast<int32_t>(i);
      }
    }
  }
  if (settings.hydrometerScanType != HydrometerScanType::Unknown) {
    return 1;
  }
  return 0;
}

bool DisplayUI::editHasReset(const Settings &settings) const {
  // Mode just cycles OFF/AUTO/HEAT/COOL, and Hydrometer just cycles
  // Off/On/devices, so a "reset to default" button adds nothing over
  // rotating or swiping back to the first option directly.
  if (_editIndex == MENU_MODE || _editIndex == MENU_HYDROMETER) {
    return false;
  }
  if (_editIndex == MENU_PROFILE &&
      !profileSlotEditable(activeProfileIndex(settings))) {
    return false;
  }
  return true;  // includes Brightness (reset to full)
}

bool DisplayUI::editCommitsProfile() const {
  return _editIndex == MENU_PROFILE || _editIndex == MENU_TARGET;
}

const char *DisplayUI::editCommitLabel() const {
  if (_editIndex == MENU_DREST) {
    return "Start";
  }
  if (editCommitsProfile()) {
    return "Apply";
  }
  return "Save";
}

String DisplayUI::editDefaultLine(const Settings &settings) const {
  if (_editIndex == MENU_PROFILE) {
    // Picking a profile reads as "this profile holds at N", so show the
    // selected profile's target plainly rather than a reset preview.
    return String("Target ") +
           formatTemperature(activeTargetC(settings), settings.unitsFahrenheit);
  }
  if (_editIndex == MENU_TARGET &&
      profileSlotEditable(activeProfileIndex(settings))) {
    return String("Live ") +
           formatTemperature(currentTargetC(settings), settings.unitsFahrenheit);
  }
  // Short meaning lines for regulation fields (round display has little room).
  if (_editIndex == MENU_COOL_ON) {
    return "Cool this far above target";
  }
  if (_editIndex == MENU_HEAT_ON) {
    return "Heat this far below target";
  }
  if (_editIndex == MENU_HOLD_BAND) {
    return "In-range band around target";
  }
  if (_editIndex == MENU_COOLING_OFF) {
    return "Min time pump stays off";
  }
  if (_editIndex == MENU_COOLING_RUN) {
    return "Min time pump stays on";
  }
  if (_editIndex == MENU_OFFSET) {
    return "Added to sensor reading";
  }
  if (!editHasReset(settings)) {
    return "";  // no Reset button, so no "Default ..." reset hint
  }
  // The current value is already shown large above; this just notes what the
  // Reset button restores, so keep it to the default alone.
  return String("Default ") + defaultMenuValue(_editIndex, settings);
}

String DisplayUI::editConfirmLine(const Settings &settings) const {
  if (_pendingEditConfirm == EditConfirmAction::Reset) {
    if (_editIndex == MENU_PROFILE) {
      return String(activeProfile(settings).name) + " target -> " +
             defaultMenuValue(_editIndex, settings);
    }
    if (_editIndex == MENU_DREST) {
      return diacetylRestRemainingText(settings) + " -> " +
             defaultMenuValue(_editIndex, settings);
    }
    return String(menuValue(_editIndex, settings)) + " -> " +
           defaultMenuValue(_editIndex, settings);
  }
  if (_editIndex == MENU_PROFILE) {
    return String("Apply ") + activeProfile(settings).name;
  }
  if (_editIndex == MENU_DREST) {
    return String("Start for ") + diacetylRestRemainingText(settings);
  }
  if (_editIndex == MENU_TARGET) {
    return String("Apply ") + menuValue(_editIndex, settings);
  }
  return String("Save ") + menuValue(_editIndex, settings);
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

