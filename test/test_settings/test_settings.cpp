#include <unity.h>

#include "config.h"

using namespace ferm;

static Settings makeDirtySettings() {
  Settings settings;
  settings.fermenterName = "  Overflow Fermenter Name Beyond Twenty Four  ";
  settings.brightness = 5;
  settings.activeProfile = 99;
  settings.diacetylRestReturnProfile = 88;
  settings.unitsFahrenheit = true;
  settings.coolOnDeltaC = 99.0f;
  settings.heatOnDeltaC = 99.0f;
  settings.holdDeltaC = 50.0f;
  settings.pumpMinOffSeconds = 99999;
  settings.pumpMinRunSeconds = 99999;
  settings.liveTargetC = 200.0f;
  settings.diacetylRestTargetC = -5.0f;
  settings.gradualCrashStepC = 0.01f;
  settings.gradualCrashStepIntervalHours = 0;
  settings.tempOffsetC = 99.0f;
  settings.hydrometerOriginalGravity = 0.1f;
  settings.hydrometerStableGravity = 9.0f;
  settings.hydrometerStableSeconds = 9999999;
  settings.mode = static_cast<UserMode>(99);
  settings.programs[0].stepCount = 1;
  settings.programs[0].steps[0].type = static_cast<StepType>(99);
  settings.programs[0].steps[0].exit = static_cast<StepExit>(99);
  settings.programs[0].steps[0].targetC = 200.0f;
  settings.programs[0].steps[0].gravityThreshold = 0.1f;
  settings.profiles[static_cast<uint8_t>(ProfileSlot::Custom1)].name =
      "  Custom Name That Is Way Too Long For The UI  ";
  settings.profiles[static_cast<uint8_t>(ProfileSlot::Custom1)].targetC =
      20.0f;
  return settings;
}

void test_sanitize_settings_normalizes_out_of_range_values() {
  Settings settings = makeDirtySettings();
  sanitizeSettings(settings);

  // Truncation runs after trim, so a 24-char cap keeps the trailing space.
  TEST_ASSERT_EQUAL_STRING("Overflow Fermenter Name ",
                            settings.fermenterName);
  TEST_ASSERT_EQUAL(MIN_BRIGHTNESS, settings.brightness);
  TEST_ASSERT_EQUAL(0, settings.activeProfile);
  TEST_ASSERT_EQUAL(0, settings.diacetylRestReturnProfile);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, MAX_DELTA_C, settings.coolOnDeltaC);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, MAX_DELTA_C, settings.heatOnDeltaC);
  TEST_ASSERT_TRUE(settings.holdDeltaC <= settings.coolOnDeltaC);
  TEST_ASSERT_TRUE(settings.holdDeltaC <= settings.heatOnDeltaC);
  TEST_ASSERT_EQUAL(1800U, settings.pumpMinOffSeconds);
  TEST_ASSERT_EQUAL(600U, settings.pumpMinRunSeconds);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, MAX_TARGET_C, settings.liveTargetC);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, MIN_TARGET_C, settings.diacetylRestTargetC);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, MIN_GRADUAL_CRASH_STEP_C,
                            settings.gradualCrashStepC);
  TEST_ASSERT_EQUAL(MIN_GRADUAL_CRASH_STEP_INTERVAL_HOURS,
                    settings.gradualCrashStepIntervalHours);
  TEST_ASSERT_TRUE(isnan(settings.hydrometerOriginalGravity));
  TEST_ASSERT_TRUE(isnan(settings.hydrometerStableGravity));
  TEST_ASSERT_EQUAL(604800U, settings.hydrometerStableSeconds);
  TEST_ASSERT_EQUAL(UserMode::Off, settings.mode);
  TEST_ASSERT_EQUAL(StepType::Hold, settings.programs[0].steps[0].type);
  TEST_ASSERT_EQUAL(StepExit::Time, settings.programs[0].steps[0].exit);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, MAX_TARGET_C,
                            settings.programs[0].steps[0].targetC);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.010f,
                            settings.programs[0].steps[0].gravityThreshold);
  TEST_ASSERT_EQUAL_STRING(
      "Custom Name Tha",
      settings.profiles[static_cast<uint8_t>(ProfileSlot::Custom1)].name);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 20.0f,
                            settings.profiles[static_cast<uint8_t>(
                                                  ProfileSlot::Custom1)]
                                .targetC);
}

void test_compute_step_target_ramp_interpolation() {
  ProfileStep step;
  step.type = StepType::Ramp;
  step.targetC = 10.0f;
  step.durationSeconds = 100;

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f,
                            computeStepTargetC(step, 0, 30.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f,
                            computeStepTargetC(step, 50, 30.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f,
                            computeStepTargetC(step, 100, 30.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f,
                            computeStepTargetC(step, 250, 30.0f));
}

void test_compute_step_target_zero_duration_guard() {
  ProfileStep step;
  step.type = StepType::Ramp;
  step.targetC = 12.0f;
  step.durationSeconds = 0;

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f,
                            computeStepTargetC(step, 0, 30.0f));
}

void test_compute_step_target_hold_crash_manual_wait() {
  ProfileStep hold;
  hold.type = StepType::Hold;
  hold.targetC = 18.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.0f,
                            computeStepTargetC(hold, 10, 30.0f));

  ProfileStep crash;
  crash.type = StepType::Crash;
  crash.targetC = 4.0f;
  crash.durationSeconds = 40;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 16.0f,
                            computeStepTargetC(crash, 10, 20.0f));

  ProfileStep manual;
  manual.type = StepType::ManualWait;
  manual.targetC = 99.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.5f,
                            computeStepTargetC(manual, 10, 21.5f));
}

void test_effective_step_exit_manual_wait_override() {
  ProfileStep step;
  step.type = StepType::ManualWait;
  step.exit = StepExit::Time;
  TEST_ASSERT_EQUAL(StepExit::Manual, effectiveStepExit(step));

  step.type = StepType::Hold;
  step.exit = StepExit::GravityBelow;
  TEST_ASSERT_EQUAL(StepExit::GravityBelow, effectiveStepExit(step));
}

void test_start_and_stop_program_state_transitions() {
  Settings settings;
  settings.liveTargetC = 19.0f;
  settings.programs[0].stepCount = 2;
  settings.programs[0].steps[0].targetC = 20.0f;
  settings.programs[1].stepCount = 0;

  TEST_ASSERT_FALSE(startProgram(settings, 1));
  TEST_ASSERT_TRUE(startProgram(settings, 0));
  TEST_ASSERT_TRUE(settings.programActive);
  TEST_ASSERT_EQUAL(0, settings.programRunIndex);
  TEST_ASSERT_EQUAL(0, settings.programStepIndex);
  TEST_ASSERT_EQUAL(0U, settings.programStepElapsedSeconds);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 19.0f, settings.programStepStartTargetC);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(ProfileSlot::Custom1),
                    settings.activeProfile);
  TEST_ASSERT_FALSE(settings.diacetylRestActive);

  stopProgram(settings);
  TEST_ASSERT_FALSE(settings.programActive);
  TEST_ASSERT_EQUAL(0U, settings.programStepElapsedSeconds);
  TEST_ASSERT_FALSE(settings.programManualAdvance);
}

void test_unit_conversions_and_display_grid() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 32.0f, cToF(0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, fToC(32.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 68.0f, toDisplayTemp(20.0f, true));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 20.0f, fromDisplayTemp(68.0f, true));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 20.0f, snapTempC(20.0f, true));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, DEFAULT_COOL_ON_DELTA_C,
                            snapDeltaC(DEFAULT_COOL_ON_DELTA_C, true));
}

void test_clamp_helpers_edges() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, clampFloat(1.0f, 2.0f, 5.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, clampFloat(9.0f, 2.0f, 5.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, clampFloat(NAN, 2.0f, 5.0f));
  TEST_ASSERT_EQUAL(3U, clampU32(3U, 1U, 9U));
  TEST_ASSERT_EQUAL(1U, clampU32(0U, 1U, 9U));
  TEST_ASSERT_EQUAL(9U, clampU32(99U, 1U, 9U));
  TEST_ASSERT_EQUAL(MIN_BRIGHTNESS, clampBrightness(0));
  TEST_ASSERT_EQUAL(MAX_BRIGHTNESS, clampBrightness(999));
  TEST_ASSERT_EQUAL(100, clampBrightness(100));
}

void setUp(void) {}

void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_sanitize_settings_normalizes_out_of_range_values);
  RUN_TEST(test_compute_step_target_ramp_interpolation);
  RUN_TEST(test_compute_step_target_zero_duration_guard);
  RUN_TEST(test_compute_step_target_hold_crash_manual_wait);
  RUN_TEST(test_effective_step_exit_manual_wait_override);
  RUN_TEST(test_start_and_stop_program_state_transitions);
  RUN_TEST(test_unit_conversions_and_display_grid);
  RUN_TEST(test_clamp_helpers_edges);
  return UNITY_END();
}