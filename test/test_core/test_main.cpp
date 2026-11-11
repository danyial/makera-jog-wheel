#include <unity.h>

#include "test_suites.h"

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  run_frame_tests();
  run_status_tests();
  run_axis_gate_tests();
  run_step_policy_tests();
  run_planner_tests();
  run_battery_tests();
  run_axis_a_tests();
  run_hold_tests();
  run_webui_tests();
  run_screensaver_tests();
  run_feed_tests();
  run_rates_tests();
  run_unlock_tests();
  run_power_switch_tests();
  run_board_rev_tests();
  return UNITY_END();
}
