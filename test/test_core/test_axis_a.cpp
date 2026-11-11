// SPEC 11, tests 52-56: optional rotary axis A (ticket #14).
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "axis_select.h"
#include "jog_gate.h"
#include "jog_planner.h"
#include "status.h"
#include "test_suites.h"

namespace {

bool parse(const char* s, MachineStatus& st) { return parse_status(s, std::strlen(s), st); }

void test_52_status_axis_a() {
  MachineStatus s{};
  TEST_ASSERT_TRUE(parse("<Idle|MPos:1,2,3,45.5,0|WPos:1,2,3,45.5,0|C:4,1,0,1>", s));
  TEST_ASSERT_TRUE(s.has_a);
  TEST_ASSERT_EQUAL_INT32(45500, s.mpos_um[3]);
  TEST_ASSERT_EQUAL_INT32(45500, s.wpos_um[3]);
  TEST_ASSERT_TRUE(parse("<Idle|MPos:1,2,3,45.5,0|WPos:1,2,3,45.5,0|C:4,1,1,1>", s));
  TEST_ASSERT_TRUE(s.inch_mode);
  TEST_ASSERT_EQUAL_INT32(25400, s.mpos_um[0]);
  TEST_ASSERT_EQUAL_INT32(45500, s.mpos_um[3]);  // rotary axis is never scaled
  TEST_ASSERT_TRUE(parse("<Idle|MPos:1,2,3|WPos:1,2,3|C:4,1,0,1>", s));
  TEST_ASSERT_FALSE(s.has_a);
  TEST_ASSERT_EQUAL_INT32(0, s.mpos_um[3]);
}

AxisSelector selector_in(AxisMode m) {
  AxisSelector s;
  switch (m) {
    case AxisMode::None: break;
    case AxisMode::X: s.on_press(Button::X, 0); break;
    case AxisMode::Y: s.on_press(Button::Y, 0); break;
    case AxisMode::XY: s.on_press(Button::X, 0); s.on_press(Button::Y, 0); break;
    case AxisMode::Z: s.on_press(Button::Z, 0); break;
    case AxisMode::A: s.on_press(Button::A, 0); break;
  }
  TEST_ASSERT_EQUAL(m, s.mode(0));
  return s;
}

void test_53_axis_select_a() {
  const AxisMode from[6] = {AxisMode::None, AxisMode::X, AxisMode::Y, AxisMode::XY, AxisMode::Z, AxisMode::A};
  const AxisMode on_a[6] = {AxisMode::A, AxisMode::A, AxisMode::A, AxisMode::A, AxisMode::A, AxisMode::None};
  for (int r = 0; r < 6; ++r) {
    AxisSelector s = selector_in(from[r]);
    s.on_press(Button::A, 100);
    char msg[24];
    snprintf(msg, sizeof(msg), "A from row %d", r);
    TEST_ASSERT_EQUAL_MESSAGE(on_a[r], s.mode(100), msg);
  }
  const Button btn[3] = {Button::X, Button::Y, Button::Z};
  const AxisMode want[3] = {AxisMode::X, AxisMode::Y, AxisMode::Z};
  for (int c = 0; c < 3; ++c) {
    AxisSelector s = selector_in(AxisMode::A);
    s.on_press(btn[c], 100);
    TEST_ASSERT_EQUAL(want[c], s.mode(100));
  }
}

GateInputs ok_inputs() {
  GateInputs in{};
  in.rates_known = true;
  in.wifi_ok = true;
  in.tcp_ok = true;
  in.have_status = true;
  in.status.state = MachineState::Idle;
  in.axis = AxisMode::A;
  return in;
}

void test_54_gate_axis_a() {
  GateInputs in = ok_inputs();
  in.status.has_a = false;
  TEST_ASSERT_EQUAL(BlockReason::NoAxisA, evaluate_gate(in));
  in.status.has_a = true;
  TEST_ASSERT_EQUAL(BlockReason::None, evaluate_gate(in));
  in = ok_inputs();
  in.axis = AxisMode::None;
  TEST_ASSERT_EQUAL(BlockReason::AxisNone, evaluate_gate(in));
}

void test_55_planner_axis_a() {
  MachineStatus st{};
  st.state = MachineState::Idle;
  st.has_a = true;
  {
    JogPlanner p{z1_pro_rates()};
    const auto r = p.tick(0, 3, 10, AxisMode::A, BlockReason::None, &st, 0);
    TEST_ASSERT_TRUE(r.cmd.valid);
    TEST_ASSERT_EQUAL_STRING("$J A0.030 F0.020", r.cmd.text);
    TEST_ASSERT_EQUAL_INT32(30, p.target_um(3));
  }
  {
    JogPlanner p{z1_pro_rates()};
    auto r = p.tick(0, 10, 100, AxisMode::A, BlockReason::None, &st, 0);
    TEST_ASSERT_EQUAL_STRING("$J A1.000 F0.077", r.cmd.text);  // 5000 mdeg/s x 0.92 / 60000
    for (uint32_t t = 20; t <= 40; t += 20) {
      TEST_ASSERT_FALSE(p.tick(t, 10, 100, AxisMode::A, BlockReason::None, &st, 0).cmd.valid);  // bundled
    }
    r = p.tick(60, 10, 100, AxisMode::A, BlockReason::None, &st, 0);
    TEST_ASSERT_EQUAL_STRING("$J A1.000 F0.307", r.cmd.text);  // 1000 of 3000 left in the window
    TEST_ASSERT_TRUE(r.clipped);
    for (uint32_t t = 80; t <= 100; t += 20) p.tick(t, 10, 100, AxisMode::A, BlockReason::None, &st, 0);
    r = p.tick(120, 10, 100, AxisMode::A, BlockReason::None, &st, 0);
    TEST_ASSERT_FALSE(r.cmd.valid);
    TEST_ASSERT_TRUE(r.clipped);
    TEST_ASSERT_EQUAL_INT32(2000, p.target_um(3));
  }
}

void test_56_axis_a_ignored_without_report() {
  MachineStatus st{};
  st.state = MachineState::Idle;
  st.has_a = false;
  JogPlanner p{z1_pro_rates()};
  TEST_ASSERT_TRUE(p.tick(0, 3, 10, AxisMode::X, BlockReason::None, &st, 0).cmd.valid);
  MachineStatus s2 = st;
  s2.mpos_um[0] = 30;          // X arrived
  s2.mpos_um[3] = 999999;      // A value meaningless without has_a
  TEST_ASSERT_TRUE(p.session_consistent(s2));
  p.tick(999, 0, 10, AxisMode::X, BlockReason::None, &s2, 0);
  TEST_ASSERT_TRUE(p.session_active());
  p.tick(1000, 0, 10, AxisMode::X, BlockReason::None, &s2, 0);
  TEST_ASSERT_FALSE(p.session_active());  // ARRIVE ignores A
}

}  // namespace

void run_axis_a_tests() {
  RUN_TEST(test_52_status_axis_a);
  RUN_TEST(test_53_axis_select_a);
  RUN_TEST(test_54_gate_axis_a);
  RUN_TEST(test_55_planner_axis_a);
  RUN_TEST(test_56_axis_a_ignored_without_report);
}
