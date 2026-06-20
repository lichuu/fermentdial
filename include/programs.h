// Program step types/helpers. Included twice from config.h: once for POD types
// (before Settings), once for inline helpers (after Settings and clamp/snap).
// Included from within namespace ferm in config.h.

#if !defined(FERM_PROGRAM_TYPES_DEFINED)

#define FERM_PROGRAM_TYPES_DEFINED

// --- Multi-step fermentation programs ---------------------------------------
enum class StepType : uint8_t {
  Hold = 0,
  Ramp = 1,
  Crash = 2,
  DRest = 3,
  ManualWait = 4,
};

enum class StepExit : uint8_t {
  Time = 0,
  GravityBelow = 1,
  GravityStable = 2,
  VelocityBelow = 3,
  Manual = 4,
};

struct ProfileStep {
  StepType type = StepType::Hold;
  StepExit exit = StepExit::Time;
  float targetC = DEFAULT_TARGET_C;
  uint32_t durationSeconds = 0;
  float gravityThreshold = 1.010f;
  uint16_t stableHours = 24;
};

struct ProgramSettings {
  uint8_t stepCount = 0;
  ProfileStep steps[MAX_PROGRAM_STEPS];
};

#elif defined(FERM_PROGRAM_HELPERS_INCLUDED_FROM_CONFIG)

inline bool profileSlotHasProgram(uint8_t slot) {
  return slot == static_cast<uint8_t>(ProfileSlot::Custom1) ||
         slot == static_cast<uint8_t>(ProfileSlot::Custom2);
}

inline bool profileSlotEditable(uint8_t slot) {
  return profileSlotHasProgram(slot);
}

inline uint8_t programIndexForSlot(uint8_t slot) {
  return slot == static_cast<uint8_t>(ProfileSlot::Custom2) ? 1 : 0;
}

inline uint8_t profileSlotForProgramIndex(uint8_t programIndex) {
  return programIndex == 1 ? static_cast<uint8_t>(ProfileSlot::Custom2)
                           : static_cast<uint8_t>(ProfileSlot::Custom1);
}

inline StepExit effectiveStepExit(const ProfileStep &step) {
  return step.type == StepType::ManualWait ? StepExit::Manual : step.exit;
}

inline float computeStepTargetC(const ProfileStep &step, uint32_t elapsedSeconds,
                                float startTargetC) {
  switch (step.type) {
  case StepType::Ramp:
  case StepType::Crash: {
    if (step.durationSeconds == 0) {
      return step.targetC;
    }
    float frac = static_cast<float>(elapsedSeconds) /
                 static_cast<float>(step.durationSeconds);
    if (frac > 1.0f) {
      frac = 1.0f;
    }
    return startTargetC + (step.targetC - startTargetC) * frac;
  }
  case StepType::ManualWait:
    return startTargetC;
  case StepType::Hold:
  case StepType::DRest:
  default:
    return step.targetC;
  }
}

inline void stopProgram(Settings &settings) {
  settings.programActive = false;
  settings.programStepElapsedSeconds = 0;
  settings.programManualAdvance = false;
}

inline void deactivateSupersededModes(Settings &settings) {
  stopProgram(settings);
  cancelDiacetylRest(settings);
}

inline bool startProgram(Settings &settings, uint8_t programIndex) {
  if (programIndex >= PROGRAM_SLOT_COUNT ||
      settings.programs[programIndex].stepCount == 0) {
    return false;
  }
  deactivateSupersededModes(settings);
  settings.programActive = true;
  settings.programRunIndex = programIndex;
  settings.programStepIndex = 0;
  settings.programStepElapsedSeconds = 0;
  settings.programManualAdvance = false;
  settings.activeProfile = profileSlotForProgramIndex(programIndex);
  if (settings.activeProfile != static_cast<uint8_t>(ProfileSlot::Crash)) {
    settings.gradualCrashEnabled = false;
  }
  applyActiveProfileTarget(settings);
  settings.programStepStartTargetC = settings.liveTargetC;
  return true;
}

inline void sanitizeProgramSettings(Settings &settings) {
  if (settings.programRunIndex >= PROGRAM_SLOT_COUNT) {
    settings.programRunIndex = 0;
  }
  for (uint8_t i = 0; i < PROGRAM_SLOT_COUNT; ++i) {
    ProgramSettings &program = settings.programs[i];
    if (program.stepCount > MAX_PROGRAM_STEPS) {
      program.stepCount = MAX_PROGRAM_STEPS;
    }
    for (uint8_t s = 0; s < program.stepCount; ++s) {
      ProfileStep &step = program.steps[s];
      if (static_cast<uint8_t>(step.type) >
          static_cast<uint8_t>(StepType::ManualWait)) {
        step.type = StepType::Hold;
      }
      if (static_cast<uint8_t>(step.exit) >
          static_cast<uint8_t>(StepExit::Manual)) {
        step.exit = StepExit::Time;
      }
      step.targetC = snapTempC(
          clampFloat(step.targetC, MIN_TARGET_C, MAX_TARGET_C),
          settings.unitsFahrenheit);
      if (!gravityIsValid(step.gravityThreshold)) {
        step.gravityThreshold = 1.010f;
      }
    }
  }
  if (settings.programActive) {
    const ProgramSettings &running = settings.programs[settings.programRunIndex];
    if (running.stepCount == 0) {
      settings.programActive = false;
      settings.programStepIndex = 0;
      settings.programStepElapsedSeconds = 0;
    } else if (settings.programStepIndex >= running.stepCount) {
      settings.programStepIndex = running.stepCount - 1;
    }
  }
  if (isnan(settings.programStepStartTargetC)) {
    settings.programStepStartTargetC = settings.liveTargetC;
  }
}

#endif