#pragma once

// Shared "what is it doing / why" + attention bits for Dial main and status JSON.
// Pure derivation from live inputs — no NVS, no side effects.

#include <stdio.h>
#include <string.h>

#include "config.h"

namespace ferm {

// Sticky attention flags (OR together).
constexpr uint8_t ATTN_FAULT = 1u << 0;
constexpr uint8_t ATTN_HYDRO_STALE = 1u << 1;
constexpr uint8_t ATTN_NOT_REACHING = 1u << 2;
constexpr uint8_t ATTN_LONG_OUTPUT = 1u << 3;

struct ControlHintInput {
  const Settings *settings = nullptr;
  RuntimeState runtimeState = RuntimeState::Boot;
  FaultCode faultCode = FaultCode::None;
  bool tempValid = false;
  float tempC = NAN;
  bool pumpOn = false;
  uint32_t pumpOffElapsedMs = 0;
  bool outputTestActive = false;
  OutputTestKind outputTestKind = OutputTestKind::None;
  bool hydroSelected = false;
  bool hydroStale = false;
  bool notReaching = false;
  bool longOutput = false;
};

struct ControlHint {
  // Primary status (short, upper-ish): "IN RANGE", "HEATING", fault text, …
  const char *primary = "";
  // Optional second line: "Pump rest 1m", "Heat only", …
  char detail[36] = "";
  uint8_t attention = 0;
};

// First attention reason for toast / list (static strings).
inline const char *attentionReasonText(uint8_t flags) {
  if (flags & ATTN_FAULT) {
    return "Sensor or interlock fault";
  }
  if (flags & ATTN_NOT_REACHING) {
    return "Temp off target too long";
  }
  if (flags & ATTN_LONG_OUTPUT) {
    return "Heater/pump on over 4h";
  }
  if (flags & ATTN_HYDRO_STALE) {
    return "Hydrometer stale";
  }
  return "";
}

inline ControlHint buildControlHint(const ControlHintInput &in) {
  ControlHint out;
  if (in.settings == nullptr) {
    out.primary = "BOOT";
    return out;
  }
  const Settings &s = *in.settings;

  if (in.faultCode != FaultCode::None ||
      in.runtimeState == RuntimeState::Fault) {
    out.primary = faultText(in.faultCode);
    out.attention |= ATTN_FAULT;
  } else if (in.outputTestActive) {
    out.primary = in.outputTestKind == OutputTestKind::Heater ? "TEST HEATER"
                                                              : "TEST PUMP";
  } else if (in.runtimeState == RuntimeState::Idle) {
    out.primary = "IN RANGE";
  } else {
    out.primary = profileRuntimeText(s, in.runtimeState);
  }

  if (in.hydroSelected && in.hydroStale) {
    out.attention |= ATTN_HYDRO_STALE;
  }
  if (in.notReaching) {
    out.attention |= ATTN_NOT_REACHING;
  }
  if (in.longOutput) {
    out.attention |= ATTN_LONG_OUTPUT;
  }

  // Detail: only when it explains an unexpected idle or restricted mode.
  out.detail[0] = '\0';
  if (in.faultCode != FaultCode::None || in.outputTestActive ||
      s.mode == UserMode::Off) {
    return out;
  }

  if (in.tempValid && !isnan(in.tempC)) {
    const float targetC = currentTargetC(s);
    const bool wantCool = in.tempC > targetC + s.coolOnDeltaC;
    const bool wantHeat = in.tempC < targetC - s.heatOnDeltaC;
    const bool coolingAllowed =
        s.mode == UserMode::Auto || s.mode == UserMode::CoolOnly;
    const bool heatingAllowed =
        s.mode == UserMode::Auto || s.mode == UserMode::HeatOnly;

    if (wantCool && coolingAllowed && !in.pumpOn &&
        in.runtimeState == RuntimeState::Idle) {
      const uint32_t minOffMs = s.pumpMinOffSeconds * 1000UL;
      if (in.pumpOffElapsedMs < minOffMs) {
        const uint32_t leftS =
            (minOffMs - in.pumpOffElapsedMs + 999UL) / 1000UL;
        const uint32_t m = leftS / 60UL;
        const uint32_t sec = leftS % 60UL;
        if (m > 0) {
          snprintf(out.detail, sizeof(out.detail), "Pump rest %lum",
                   static_cast<unsigned long>(m));
        } else {
          snprintf(out.detail, sizeof(out.detail), "Pump rest %lus",
                   static_cast<unsigned long>(sec));
        }
        return out;
      }
    }

    if (wantCool && !coolingAllowed && heatingAllowed) {
      strncpy(out.detail, "Heat only mode", sizeof(out.detail) - 1);
      return out;
    }
    if (wantHeat && !heatingAllowed && coolingAllowed) {
      strncpy(out.detail, "Cool only mode", sizeof(out.detail) - 1);
      return out;
    }
  }

  if (s.gradualCrashEnabled &&
      activeProfileIndex(s) == static_cast<uint8_t>(ProfileSlot::Crash) &&
      (in.runtimeState == RuntimeState::Cooling ||
       in.runtimeState == RuntimeState::Idle)) {
    strncpy(out.detail, "Gradual crash", sizeof(out.detail) - 1);
    return out;
  }

  if (s.programActive) {
    const uint8_t idx =
        s.programRunIndex < PROGRAM_SLOT_COUNT ? s.programRunIndex : 0;
    const uint8_t steps = s.programs[idx].stepCount;
    snprintf(out.detail, sizeof(out.detail), "Prog step %u/%u",
             static_cast<unsigned>(s.programStepIndex + 1),
             static_cast<unsigned>(steps));
  }

  return out;
}

}  // namespace ferm
