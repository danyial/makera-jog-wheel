// SPEC 11, tests 15-21: AxisSelector and jog gate.
#include <unity.h>

#include <cstdio>

#include "axis_select.h"
#include "jog_gate.h"
#include "test_suites.h"

namespace {

// Brings a fresh selector into the given state using press edges at t = 0.
AxisSelector selector_in(AxisMode m) {
  AxisSelector s;
  switch (m) {
    case AxisMode::None: break;
    case AxisMode::X: s.on_press(Button::X, 0); break;
    case AxisMode::Y: s.on_press(Button::Y, 0); break;
    case AxisMode::XY: s.on_press(Button::X, 0); s.on_press(Button::Y, 0); break;
    case AxisMode::Z: s.on_press(Button::Z, 0); break;
  }
  TEST_ASSERT_EQUAL(m, s.mode(0));
  return s;
}

void test_15_axis_transitions() {
  const AxisMode from[5] = {AxisMode::None, AxisMode::X, AxisMode::Y, AxisMode::XY, AxisMode::Z};
  // Table 8.4: rows = state before, columns = X, Y, Z pressed.
  const AxisMode to[5][3] = {
      {AxisMode::X, AxisMode::Y, AxisMode::Z},
      {AxisMode::None, AxisMode::XY, AxisMode::Z},
      {AxisMode::XY, AxisMode::None, AxisMode::Z},
      {AxisMode::Y, AxisMode::X, AxisMode::Z},
      {AxisMode::X, AxisMode::Y, AxisMode::None},
  };
  const Button btn[3] = {Button::X, Button::Y, Button::Z};
  for (int r = 0; r < 5; ++r) {
    for (int c = 0; c < 3; ++c) {
      AxisSelector s = selector_in(from[r]);
      s.on_press(btn[c], 100);
      char msg[32];
      snprintf(msg, sizeof(msg), "row %d, button %d", r, c);
      TEST_ASSERT_EQUAL_MESSAGE(to[r][c], s.mode(100), msg);
    }
  }
}

void auto_clear_run(uint32_t t0) {
  {
    AxisSelector s;
    s.on_press(Button::X, t0);
    TEST_ASSERT_EQUAL(AxisMode::X, s.mode(t0 + 59999));
    TEST_ASSERT_EQUAL(AxisMode::None, s.mode(t0 + 60000));
  }
  {
    AxisSelector s;
    s.on_press(Button::X, t0);
    s.on_activity(t0 + 30000);
    TEST_ASSERT_EQUAL(AxisMode::X, s.mode(t0 + 89999));
    TEST_ASSERT_EQUAL(AxisMode::None, s.mode(t0 + 90000));
  }
  {
    AxisSelector s;
    s.on_press(Button::X, t0);
    s.on_press(Button::X, t0 + 60000);  // auto-clear applies first: None -> X
    TEST_ASSERT_EQUAL(AxisMode::X, s.mode(t0 + 60000));
  }
  {
    // Wheel activity after the idle period must not revive the stale selection (DECISIONS.md).
    AxisSelector s;
    s.on_press(Button::X, t0);
    s.on_activity(t0 + 60000);
    TEST_ASSERT_EQUAL(AxisMode::None, s.mode(t0 + 60000));
  }
}

void test_16_axis_auto_clear() {
  auto_clear_run(0);
  auto_clear_run(0xFFFFF000u);  // wraps around 2^32
}

void test_17_axis_clear() {
  AxisSelector s = selector_in(AxisMode::XY);
  s.clear();
  TEST_ASSERT_EQUAL(AxisMode::None, s.mode(10));
  s.on_press(Button::X, 20);
  TEST_ASSERT_EQUAL(AxisMode::X, s.mode(20));
}

GateInputs ok_inputs() {
  GateInputs in{};
  in.rates_known = true;
  in.battery_critical = false;
  in.wifi_ok = true;
  in.tcp_ok = true;
  in.have_status = true;
  in.status = MachineStatus{};
  in.status.state = MachineState::Idle;
  in.status_age_ms = 0;
  in.axis = AxisMode::X;
  in.session_active = false;
  in.session_consistent = false;
  return in;
}

void test_18_gate_each_rule() {
  GateInputs in = ok_inputs();
  in.battery_critical = true;
  TEST_ASSERT_EQUAL(BlockReason::BatteryLow, evaluate_gate(in));  // 1
  in = ok_inputs();
  in.wifi_ok = false;
  TEST_ASSERT_EQUAL(BlockReason::NoWifi, evaluate_gate(in));  // 2
  in = ok_inputs();
  in.tcp_ok = false;
  TEST_ASSERT_EQUAL(BlockReason::NoMachine, evaluate_gate(in));  // 3
  in = ok_inputs();
  in.have_status = false;
  TEST_ASSERT_EQUAL(BlockReason::NoData, evaluate_gate(in));  // 4
  in = ok_inputs();
  in.axis = AxisMode::None;
  TEST_ASSERT_EQUAL(BlockReason::AxisNone, evaluate_gate(in));  // 5
  in = ok_inputs();
  in.status.playing = true;
  TEST_ASSERT_EQUAL(BlockReason::JobRunning, evaluate_gate(in));  // 6
  in = ok_inputs();
  in.status.state = MachineState::Alarm;
  TEST_ASSERT_EQUAL(BlockReason::Alarm, evaluate_gate(in));  // 7
  in = ok_inputs();
  in.status.halted = true;
  TEST_ASSERT_EQUAL(BlockReason::Alarm, evaluate_gate(in));  // 7 (H)
  in = ok_inputs();
  in.session_active = true;
  in.session_consistent = true;
  TEST_ASSERT_EQUAL(BlockReason::None, evaluate_gate(in));  // 8
  in = ok_inputs();
  TEST_ASSERT_EQUAL(BlockReason::None, evaluate_gate(in));  // 9
  in = ok_inputs();
  in.status.state = MachineState::Home;
  TEST_ASSERT_EQUAL(BlockReason::MachineBusy, evaluate_gate(in));  // 10
}

void test_19_gate_order() {
  GateInputs in = ok_inputs();
  in.status.playing = true;
  in.axis = AxisMode::None;
  TEST_ASSERT_EQUAL(BlockReason::AxisNone, evaluate_gate(in));
}

void test_20_gate_status_age() {
  GateInputs in = ok_inputs();
  in.status_age_ms = 250;
  TEST_ASSERT_NOT_EQUAL(BlockReason::NoData, evaluate_gate(in));
  in.status_age_ms = 251;
  TEST_ASSERT_EQUAL(BlockReason::NoData, evaluate_gate(in));
}

void test_21_gate_run_and_hold() {
  GateInputs in = ok_inputs();
  in.status.state = MachineState::Run;
  TEST_ASSERT_EQUAL(BlockReason::MachineBusy, evaluate_gate(in));
  in.session_active = true;
  in.session_consistent = true;
  TEST_ASSERT_EQUAL(BlockReason::None, evaluate_gate(in));
  in.session_consistent = false;
  TEST_ASSERT_EQUAL(BlockReason::MachineBusy, evaluate_gate(in));
  in.status.state = MachineState::Hold;
  in.session_consistent = true;
  TEST_ASSERT_EQUAL(BlockReason::MachineBusy, evaluate_gate(in));
}

}  // namespace

void run_axis_gate_tests() {
  RUN_TEST(test_15_axis_transitions);
  RUN_TEST(test_16_axis_auto_clear);
  RUN_TEST(test_17_axis_clear);
  RUN_TEST(test_18_gate_each_rule);
  RUN_TEST(test_19_gate_order);
  RUN_TEST(test_20_gate_status_age);
  RUN_TEST(test_21_gate_run_and_hold);
}
