#pragma once
void run_frame_tests();
void run_status_tests();
void run_axis_gate_tests();
void run_step_policy_tests();
void run_planner_tests();
void run_battery_tests();
void run_axis_a_tests();
void run_hold_tests();
void run_webui_tests();
void run_screensaver_tests();
void run_feed_tests();
void run_rates_tests();

#include "machine_rates.h"

// Max rates of the Z1 Pro (2026-11-11, firmware 1.1.3.0.1.13): X/Y 3000 mm/min = "50", Z 1000 mm/min =
// "16.6667" as M203.1 reports them, rounded up to um/s; A 3600 deg/min = 60 deg/s as in config.txt (M203.1 reports
// 30 because the firmware caps A, covered by test 97). Test values only: the firmware reads the rates from the
// machine and has no default (SPEC 8.8, ticket #38).
inline MachineRates z1_pro_rates() {
  MachineRates r;
  r.um_s[0] = 50000;
  r.um_s[1] = 50000;
  r.um_s[2] = 16667;
  r.um_s[3] = 60000;
  r.xyz = true;
  r.a = true;
  return r;
}
void run_unlock_tests();
void run_power_switch_tests();
void run_board_rev_tests();
