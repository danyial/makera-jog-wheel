// SPEC 11, tests 27-50: DetentAccumulator, JogPlanner, fixed_fmt.
#include <unity.h>

#include <cstring>

#include "detents.h"
#include "fixed_fmt.h"
#include "jog_planner.h"
#include "test_suites.h"

namespace {

// --- DetentAccumulator ---

void test_27_detents_remainder_reset() {
  DetentAccumulator d(4);
  TEST_ASSERT_EQUAL_INT32(1, d.take(0, 6));
  TEST_ASSERT_EQUAL_INT32(2, d.remainder());
  for (uint32_t t = 20; t <= 280; t += 20) TEST_ASSERT_EQUAL_INT32(0, d.take(t, 0));
  TEST_ASSERT_EQUAL_INT32(0, d.take(300, 2));
  TEST_ASSERT_EQUAL_INT32(2, d.remainder());

  DetentAccumulator e(4);
  TEST_ASSERT_EQUAL_INT32(1, e.take(0, 6));
  TEST_ASSERT_EQUAL_INT32(1, e.take(20, 2));
  TEST_ASSERT_EQUAL_INT32(0, e.remainder());
}

void test_28_detents_negative() {
  DetentAccumulator d(4);
  TEST_ASSERT_EQUAL_INT32(-1, d.take(0, -6));
  TEST_ASSERT_EQUAL_INT32(-2, d.remainder());
}

// --- JogPlanner fixture ---

struct Rig {
  JogPlanner p{z1_pro_rates()};
  MachineStatus st{};
  uint32_t age = 0;
  BlockReason gate = BlockReason::None;
  AxisMode axis = AxisMode::X;

  Rig() { st.state = MachineState::Idle; }
  void mpos(int32_t x, int32_t y = 0, int32_t z = 0) {
    st.mpos_um[0] = x;
    st.mpos_um[1] = y;
    st.mpos_um[2] = z;
  }
  PlannerTickResult tick(uint32_t t, int32_t det, int32_t step) {
    return p.tick(t, det, step, axis, gate, &st, age);
  }
};

void assert_cmd(const char* want, const PlannerTickResult& r) {
  TEST_ASSERT_TRUE_MESSAGE(r.cmd.valid, "expected a command");
  TEST_ASSERT_EQUAL_STRING(want, r.cmd.text);
}

void assert_no_cmd(const PlannerTickResult& r) { TEST_ASSERT_FALSE_MESSAGE(r.cmd.valid, "expected no command"); }

void test_29_gate_blocked() {
  Rig g;
  g.gate = BlockReason::MachineBusy;
  const auto r = g.tick(0, 3, 10);
  assert_no_cmd(r);
  TEST_ASSERT_FALSE(g.p.session_active());
}

void test_30_first_command() {
  Rig g;
  assert_cmd("$J X0.030 F0.008", g.tick(0, 3, 10));  // slow: lower limit of F (0.4 mm/s)
  TEST_ASSERT_EQUAL_INT8(1, g.p.dir());
  TEST_ASSERT_EQUAL_INT32(30, g.p.target_um(0));
}

void test_31_negative() {
  Rig g;
  assert_cmd("$J X-0.030 F0.008", g.tick(0, -3, 10));
  TEST_ASSERT_EQUAL_INT8(-1, g.p.dir());
}

void test_32_xy_and_z() {
  Rig g;
  g.axis = AxisMode::XY;
  assert_cmd("$J X0.030 Y0.030 F0.008", g.tick(0, 3, 10));
  Rig z;
  z.axis = AxisMode::Z;
  assert_cmd("$J Z0.030 F0.012", z.tick(0, 3, 10));
}

void test_33_start_status_age() {
  Rig g;
  g.age = 151;
  assert_no_cmd(g.tick(0, 3, 10));
  TEST_ASSERT_FALSE(g.p.session_active());
  g.age = 150;
  assert_cmd("$J X0.030 F0.008", g.tick(20, 3, 10));
}

// Continuation once a status received START_GAP_MS after the first block shows it done: nothing queued,
// so the next detents go out at once.
void test_34_continuation() {
  Rig g;
  g.tick(0, 3, 10);
  g.mpos(30);  // first block done, status received at t = 200
  assert_cmd("$J X0.030 F0.008", g.tick(200, 3, 10));
  TEST_ASSERT_EQUAL_INT32(60, g.p.target_um(0));
}

// Frozen mpos, +10 detents x 100 um every tick. F = ceil(0.23 x um in the last 200 ms) for X.
// t=0: first command at once (1000 um -> 0.230). Then bundled: t=60 sends t=20..60 (3000 um, 4000 um in
// 200 ms -> 0.920); t=120 sends 1000 of 3000 (window 5000 full, clipped; 7000 um -> capped 1.000).
void fill_window(Rig& g, int32_t sign) {
  for (uint32_t t = 0; t <= 120; t += 20) g.tick(t, sign * 10, 100);
}

void test_35_window_frozen_mpos() {
  Rig g;
  assert_cmd("$J X1.000 F0.092", g.tick(0, 10, 100));
  assert_no_cmd(g.tick(20, 10, 100));
  assert_no_cmd(g.tick(40, 10, 100));
  auto r = g.tick(60, 10, 100);
  assert_cmd("$J X3.000 F0.368", r);
  TEST_ASSERT_FALSE(r.clipped);
  assert_no_cmd(g.tick(80, 10, 100));
  assert_no_cmd(g.tick(100, 10, 100));
  r = g.tick(120, 10, 100);
  assert_cmd("$J X1.000 F0.400", r);
  TEST_ASSERT_TRUE(r.clipped);
  assert_no_cmd(g.tick(140, 10, 100));
  assert_no_cmd(g.tick(160, 10, 100));
  r = g.tick(180, 10, 100);
  assert_no_cmd(r);
  TEST_ASSERT_TRUE(r.clipped);
  TEST_ASSERT_EQUAL_INT32(5000, g.p.target_um(0));
}

void test_36_window_partial() {
  Rig g;
  fill_window(g, 1);  // target 5000
  g.mpos(50);         // target - mpos = 4950
  assert_no_cmd(g.tick(140, 1, 100));  // bundled: blocks are still queued
  assert_no_cmd(g.tick(160, 0, 100));
  auto r = g.tick(180, 0, 100);
  assert_cmd("$J X0.050 F0.400", r);
  TEST_ASSERT_TRUE(r.clipped);
  // target 5050, mpos 50 -> 5000: nothing left.
  g.tick(200, 1, 100);
  g.tick(220, 0, 100);
  r = g.tick(240, 0, 100);
  assert_no_cmd(r);
  TEST_ASSERT_TRUE(r.clipped);
}

void test_37_window_negative() {
  Rig g;
  fill_window(g, -1);  // target -5000
  g.mpos(-10);         // target - mpos = -4990
  g.tick(140, -10, 10);
  g.tick(160, 0, 10);
  assert_cmd("$J X-0.010 F0.400", g.tick(180, 0, 10));
}

void test_38_window_xy_min() {
  Rig g;
  g.axis = AxisMode::XY;
  fill_window(g, 1);  // targets X 5000, Y 5000
  g.mpos(40, 100);    // X: |5000 - 40| = 4960 -> allowed 40; Y: |5000 - 100| = 4900 -> allowed 100
  g.tick(140, 10, 10);
  g.tick(160, 0, 10);
  const auto r = g.tick(180, 0, 10);
  assert_cmd("$J X0.040 Y0.040 F0.400", r);
  TEST_ASSERT_TRUE(r.clipped);
}

void test_39_direction_lock() {
  Rig g;
  assert_cmd("$J X0.500 F0.046", g.tick(0, 5, 100));  // target 500, dir +1
  auto r = g.tick(20, -3, 10);
  assert_no_cmd(r);
  TEST_ASSERT_TRUE(r.clipped);
  TEST_ASSERT_EQUAL_INT8(1, g.p.dir());
  g.mpos(490);  // target - mpos = 10
  r = g.tick(40, -3, 10);
  assert_no_cmd(r);  // reversed, but the last block may still be queued: bundled
  TEST_ASSERT_EQUAL_INT8(-1, g.p.dir());
  assert_no_cmd(g.tick(60, 0, 10));
  assert_cmd("$J X-0.030 F0.052", g.tick(80, 0, 10));  // 500 + 30 + 30 um in 200 ms
}

void test_40_session_end_idle() {
  Rig g;
  g.tick(0, 3, 10);
  g.mpos(30);
  g.tick(999, 0, 10);
  TEST_ASSERT_TRUE(g.p.session_active());
  g.tick(1000, 0, 10);
  TEST_ASSERT_FALSE(g.p.session_active());
}

void test_41_session_end_max_idle() {
  Rig g;
  g.tick(0, 3, 10);  // target 30, mpos stays 0
  g.tick(1000, 0, 10);
  TEST_ASSERT_TRUE(g.p.session_active());
  g.tick(2999, 0, 10);
  TEST_ASSERT_TRUE(g.p.session_active());
  g.tick(3000, 0, 10);
  TEST_ASSERT_FALSE(g.p.session_active());
}

void test_42_send_failed() {
  Rig g;
  assert_cmd("$J X0.030 F0.008", g.tick(0, 3, 10));
  g.p.notify_send_failed();
  TEST_ASSERT_EQUAL_INT32(0, g.p.target_um(0));
  TEST_ASSERT_FALSE(g.p.session_active());
}

void test_43_axis_change_in_session() {
  Rig g;
  g.tick(0, 3, 10);
  g.axis = AxisMode::XY;
  assert_no_cmd(g.tick(20, 3, 10));
  TEST_ASSERT_FALSE(g.p.session_active());
}

void test_44_one_command_per_tick() {
  Rig g;
  assert_cmd("$J X1.000 F0.092", g.tick(0, 100, 10));
}

void test_45_no_restart_on_old_status() {
  Rig g;
  g.tick(0, 3, 10);
  g.p.end_session(40);
  g.age = 50;
  assert_no_cmd(g.tick(60, 3, 10));
  TEST_ASSERT_FALSE(g.p.session_active());
  g.age = 0;
  assert_cmd("$J X0.030 F0.008", g.tick(300, 3, 10));
}

void test_46_start_gap_boundary() {
  {
    Rig g;
    g.tick(0, 3, 10);
    g.p.end_session(40);
    g.age = 21;
    assert_no_cmd(g.tick(260, 3, 10));
  }
  {
    Rig g;
    g.tick(0, 3, 10);
    g.p.end_session(40);
    g.age = 20;
    assert_cmd("$J X0.030 F0.008", g.tick(260, 3, 10));
  }
}

void test_47_no_lockout_without_session() {
  Rig g;
  for (uint32_t t = 0; t <= 180; t += 20) assert_no_cmd(g.tick(t, 0, 10));
  assert_cmd("$J X0.030 F0.008", g.tick(200, 3, 10));
}

void test_48_session_consistent() {
  Rig g;
  fill_window(g, 1);
  TEST_ASSERT_EQUAL_INT32(5000, g.p.target_um(0));
  MachineStatus s = g.st;
  s.mpos_um[0] = -100;  // distance 5100 = WINDOW_UM + FOREIGN_TOL_UM
  TEST_ASSERT_TRUE(g.p.session_consistent(s));
  s.mpos_um[0] = -101;
  TEST_ASSERT_FALSE(g.p.session_consistent(s));
  JogPlanner fresh{z1_pro_rates()};
  TEST_ASSERT_FALSE(fresh.session_consistent(g.st));
}

// --- fixed_fmt ---

void check_um(int32_t v, const char* want) {
  char buf[16];
  const size_t n = format_um(v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(want, buf);
  TEST_ASSERT_EQUAL_UINT(strlen(want), n);
}

void check_in(int32_t v, const char* want) {
  char buf[16];
  format_inch_e4(v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(want, buf);
}

void test_49_format_um() {
  check_um(0, "0.000");
  check_um(5, "0.005");
  check_um(-5, "-0.005");
  check_um(123456, "123.456");
  check_um(-2000, "-2.000");
  char tiny[5];
  TEST_ASSERT_EQUAL_UINT(0, format_um(-5, tiny, sizeof(tiny)));
}

void test_50_format_inch() {
  check_in(25400, "1.0000");
  check_in(-1270, "-0.0500");
  check_in(127, "0.0050");
  check_in(1, "0.0000");
  check_in(-1, "0.0000");
}

}  // namespace

void run_planner_tests() {
  RUN_TEST(test_27_detents_remainder_reset);
  RUN_TEST(test_28_detents_negative);
  RUN_TEST(test_29_gate_blocked);
  RUN_TEST(test_30_first_command);
  RUN_TEST(test_31_negative);
  RUN_TEST(test_32_xy_and_z);
  RUN_TEST(test_33_start_status_age);
  RUN_TEST(test_34_continuation);
  RUN_TEST(test_35_window_frozen_mpos);
  RUN_TEST(test_36_window_partial);
  RUN_TEST(test_37_window_negative);
  RUN_TEST(test_38_window_xy_min);
  RUN_TEST(test_39_direction_lock);
  RUN_TEST(test_40_session_end_idle);
  RUN_TEST(test_41_session_end_max_idle);
  RUN_TEST(test_42_send_failed);
  RUN_TEST(test_43_axis_change_in_session);
  RUN_TEST(test_44_one_command_per_tick);
  RUN_TEST(test_45_no_restart_on_old_status);
  RUN_TEST(test_46_start_gap_boundary);
  RUN_TEST(test_47_no_lockout_without_session);
  RUN_TEST(test_48_session_consistent);
  RUN_TEST(test_49_format_um);
  RUN_TEST(test_50_format_inch);
}

// SPEC 11, tests 57-60: hold the session on a short NoData (ticket #19).
namespace {

MachineStatus idle_status() {
  MachineStatus s{};
  s.state = MachineState::Idle;
  return s;
}

void test_57_hold_on_nodata() {
  MachineStatus st = idle_status();
  JogPlanner p{z1_pro_rates()};
  TEST_ASSERT_TRUE(p.tick(0, 3, 10, AxisMode::X, BlockReason::None, &st, 0).cmd.valid);
  auto r = p.tick(20, 3, 10, AxisMode::X, BlockReason::NoData, &st, 300);
  TEST_ASSERT_FALSE(r.cmd.valid);
  TEST_ASSERT_TRUE(p.session_active());
  TEST_ASSERT_EQUAL_INT32(30, p.target_um(0));
  r = p.tick(40, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
  TEST_ASSERT_FALSE(r.cmd.valid);  // a1: status not START_GAP_MS after hold start (0 + 200 > 20)
  TEST_ASSERT_TRUE(p.session_active());
  r = p.tick(219, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
  TEST_ASSERT_FALSE(r.cmd.valid);  // 200 > 199
  st.mpos_um[0] = 30;  // status received 220 ms after the first block shows it done: nothing queued, send at once
  r = p.tick(220, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
  TEST_ASSERT_TRUE(r.cmd.valid);   // 200 <= 200
  TEST_ASSERT_EQUAL_STRING("$J X0.030 F0.008", r.cmd.text);  // no tempo measured in the hold
  TEST_ASSERT_EQUAL_INT32(60, p.target_um(0));
}

void test_58_hold_limits() {
  MachineStatus st = idle_status();
  {
    JogPlanner p{z1_pro_rates()};
    p.tick(0, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
    p.tick(20, 3, 10, AxisMode::X, BlockReason::NoData, &st, 1000);
    TEST_ASSERT_TRUE(p.session_active());
  }
  {
    JogPlanner p{z1_pro_rates()};
    p.tick(0, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
    TEST_ASSERT_FALSE(p.tick(20, 3, 10, AxisMode::X, BlockReason::NoData, &st, 1001).cmd.valid);
    TEST_ASSERT_FALSE(p.session_active());
  }
  {
    JogPlanner p{z1_pro_rates()};
    p.tick(0, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
    p.tick(20, 3, 10, AxisMode::X, BlockReason::NoData, nullptr, 0);
    TEST_ASSERT_FALSE(p.session_active());
  }
}

void test_59_hold_only_for_nodata() {
  MachineStatus st = idle_status();
  {
    JogPlanner p{z1_pro_rates()};
    p.tick(0, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
    p.tick(20, 3, 10, AxisMode::X, BlockReason::MachineBusy, &st, 0);
    TEST_ASSERT_FALSE(p.session_active());
  }
  {
    JogPlanner p{z1_pro_rates()};
    p.tick(0, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
    p.tick(20, 3, 10, AxisMode::Y, BlockReason::NoData, &st, 300);
    TEST_ASSERT_FALSE(p.session_active());
  }
}

void test_60_hold_ends_via_g() {
  MachineStatus st = idle_status();  // mpos stays 0, target 30: never arrives
  JogPlanner p{z1_pro_rates()};
  p.tick(0, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
  for (uint32_t t = 20; t < 3000; t += 20) {
    p.tick(t, 0, 10, AxisMode::X, BlockReason::NoData, &st, 300);
    TEST_ASSERT_TRUE(p.session_active());
  }
  p.tick(3000, 0, 10, AxisMode::X, BlockReason::NoData, &st, 300);
  TEST_ASSERT_FALSE(p.session_active());
}

}  // namespace

void run_hold_tests() {
  RUN_TEST(test_57_hold_on_nodata);
  RUN_TEST(test_58_hold_limits);
  RUN_TEST(test_59_hold_only_for_nodata);
  RUN_TEST(test_60_hold_ends_via_g);
}
