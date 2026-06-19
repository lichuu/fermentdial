#include <unity.h>

#include "control.h"

using namespace ferm;

// Exercises the interlock guard directly. update() only ever requests one
// output at a time, so this proves applyOutputs rejects simultaneous requests,
// not that a production code path can reach that state.
class TestController : public FermentationController {
public:
  void driveOutputs(uint32_t nowMs, const Settings &settings,
                    bool heaterRequested, bool pumpRequested,
                    RuntimeState requestedState) {
    applyOutputs(nowMs, settings, heaterRequested, pumpRequested, false,
                 FaultCode::None, requestedState);
  }
};

static Settings autoSettings() {
  Settings settings;
  settings.mode = UserMode::Auto;
  settings.liveTargetC = 20.0f;
  settings.coolOnDeltaC = DEFAULT_COOL_ON_DELTA_C;
  settings.heatOnDeltaC = DEFAULT_HEAT_ON_DELTA_C;
  settings.holdDeltaC = DEFAULT_HOLD_DELTA_C;
  settings.pumpMinOffSeconds = 120;
  settings.pumpMinRunSeconds = 30;
  return settings;
}

void test_interlock_fault_when_both_outputs_requested() {
  TestController controller;
  Settings settings = autoSettings();
  controller.begin(0);
  controller.driveOutputs(0, settings, true, true, RuntimeState::Heating);

  TEST_ASSERT_EQUAL(RuntimeState::Fault, controller.runtimeState());
  TEST_ASSERT_EQUAL(FaultCode::Interlock, controller.faultCode());
  TEST_ASSERT_FALSE(controller.heaterOn());
  TEST_ASSERT_FALSE(controller.pumpOn());
}

void test_sensor_fault_forces_outputs_off() {
  FermentationController controller;
  Settings settings = autoSettings();
  controller.begin(0);
  controller.update(1000, settings, false, NAN);

  TEST_ASSERT_EQUAL(RuntimeState::Fault, controller.runtimeState());
  TEST_ASSERT_EQUAL(FaultCode::Sensor, controller.faultCode());
  TEST_ASSERT_FALSE(controller.heaterOn());
  TEST_ASSERT_FALSE(controller.pumpOn());
}

#if !FERM_DEMO_SENSOR
void test_pump_min_run_holds_pump_on() {
  FermentationController controller;
  Settings settings = autoSettings();
  settings.pumpMinRunSeconds = 30;
  settings.pumpMinOffSeconds = 0;
  controller.begin(0);

  const float hotTemp = settings.liveTargetC + settings.coolOnDeltaC + 2.0f;
  controller.update(0, settings, true, hotTemp);
  TEST_ASSERT_TRUE(controller.pumpOn());

  const float coldTemp = settings.liveTargetC - settings.heatOnDeltaC - 2.0f;
  controller.update(10000, settings, true, coldTemp);
  TEST_ASSERT_TRUE(controller.pumpOn());
  TEST_ASSERT_FALSE(controller.heaterOn());
}

void test_pump_min_off_blocks_restart() {
  FermentationController controller;
  Settings settings = autoSettings();
  settings.pumpMinOffSeconds = 120;
  settings.pumpMinRunSeconds = 1;
  // Negative nowMs wraps in uint32_t so pump-off elapsed time is already large.
  controller.begin(-200000);

  const float hotTemp = settings.liveTargetC + settings.coolOnDeltaC + 2.0f;
  controller.update(0, settings, true, hotTemp);
  TEST_ASSERT_TRUE(controller.pumpOn());

  controller.update(5000, settings, true, settings.liveTargetC);
  TEST_ASSERT_FALSE(controller.pumpOn());

  const float hotAgain = settings.liveTargetC + settings.coolOnDeltaC + 2.0f;
  controller.update(60000, settings, true, hotAgain);
  TEST_ASSERT_FALSE(controller.pumpOn());

  controller.update(125000, settings, true, hotAgain);
  TEST_ASSERT_TRUE(controller.pumpOn());
}
#endif

#if FERM_DEMO_SENSOR
void test_demo_mode_suppresses_physical_outputs() {
  mock_gpio_reset();
  FermentationController controller;
  Settings settings = autoSettings();
  controller.begin(0);

  const float coldTemp = settings.liveTargetC - settings.heatOnDeltaC - 2.0f;
  controller.update(1000, settings, true, coldTemp);

  TEST_ASSERT_TRUE(controller.heaterOn());
  const int offLevel = MOSFET_ACTIVE_HIGH ? LOW : HIGH;
  TEST_ASSERT_EQUAL(offLevel, mock_gpio[PIN_HEATER_TRIGGER]);
  TEST_ASSERT_EQUAL(offLevel, mock_gpio[PIN_PUMP_TRIGGER]);
}
#endif

void setUp(void) { mock_gpio_reset(); }

void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_interlock_fault_when_both_outputs_requested);
  RUN_TEST(test_sensor_fault_forces_outputs_off);
#if !FERM_DEMO_SENSOR
  RUN_TEST(test_pump_min_run_holds_pump_on);
  RUN_TEST(test_pump_min_off_blocks_restart);
#endif
#if FERM_DEMO_SENSOR
  RUN_TEST(test_demo_mode_suppresses_physical_outputs);
#endif
  return UNITY_END();
}