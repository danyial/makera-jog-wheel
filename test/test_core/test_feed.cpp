// SPEC 11, tests 76-89: feed from wheel tempo, bundling, queue limit and the larger window (ticket #32).
#include <unity.h>

#include "feed_rate.h"
#include "jog_planner.h"
#include "test_suites.h"

namespace {

// Z1 Pro rates (z1_pro_rates): X/Y F = ceil(v x 92 x 1000 / (100 x 50000)) = ceil(v x 92 / 5000), limits [8, 400]
// (0.4 ... 20 mm/s); Z ceil(v x 920 / 16667), [12, 299] (0.2 ... 4.98 mm/s); A ceil(v x 92 / 6000), [20, 500].
void test_76_feed_milli() {
  const MachineRates r = z1_pro_rates();
  TEST_ASSERT_EQUAL_INT32(8, feed_milli(0, AxisMode::X, r));           // never <= 0: lower limit 0.4 mm/s
  TEST_ASSERT_EQUAL_INT32(8, feed_milli(1, AxisMode::X, r));
  TEST_ASSERT_EQUAL_INT32(8, feed_milli(434, AxisMode::X, r));         // 7.986 -> 8
  TEST_ASSERT_EQUAL_INT32(9, feed_milli(435, AxisMode::X, r));         // 8.004 -> 9 (first value above the floor)
  TEST_ASSERT_EQUAL_INT32(58, feed_milli(3100, AxisMode::X, r));       // 57.04 rounded up
  TEST_ASSERT_EQUAL_INT32(184, feed_milli(10000, AxisMode::X, r));     // 10 mm/s x 0.92 / 50 mm/s
  TEST_ASSERT_EQUAL_INT32(184, feed_milli(10000, AxisMode::Y, r));
  TEST_ASSERT_EQUAL_INT32(399, feed_milli(21684, AxisMode::X, r));     // 398.986 -> 399
  TEST_ASSERT_EQUAL_INT32(400, feed_milli(21685, AxisMode::X, r));     // 399.004 -> 400
  TEST_ASSERT_EQUAL_INT32(400, feed_milli(21740, AxisMode::X, r));     // 400.016 -> capped: 0.4 x 50 = 20 mm/s
  TEST_ASSERT_EQUAL_INT32(261, feed_milli(10000, AxisMode::XY, r));    // path speed x sqrt(2): 260.18 rounded up
  TEST_ASSERT_EQUAL_INT32(399, feed_milli(15335, AxisMode::XY, r));    // 398.99 -> 399
  TEST_ASSERT_EQUAL_INT32(400, feed_milli(15336, AxisMode::XY, r));    // capped: path 20 mm/s as before #38
  TEST_ASSERT_EQUAL_INT32(12, feed_milli(217, AxisMode::Z, r));        // 11.98 -> 12 (floor 0.2 mm/s)
  TEST_ASSERT_EQUAL_INT32(13, feed_milli(218, AxisMode::Z, r));        // 12.03 -> 13
  TEST_ASSERT_EQUAL_INT32(111, feed_milli(2000, AxisMode::Z, r));      // 2 mm/s x 0.92 / 16.667 mm/s
  TEST_ASSERT_EQUAL_INT32(298, feed_milli(5398, AxisMode::Z, r));      // 297.99 -> 298
  TEST_ASSERT_EQUAL_INT32(299, feed_milli(5399, AxisMode::Z, r));      // capped: 0.299 x 16.667 = 4.98 mm/s <= 5
  TEST_ASSERT_EQUAL_INT32(20, feed_milli(1304, AxisMode::A, r));       // 19.995 -> 20 (floor 1.2 deg/s)
  TEST_ASSERT_EQUAL_INT32(21, feed_milli(1305, AxisMode::A, r));
  TEST_ASSERT_EQUAL_INT32(154, feed_milli(10000, AxisMode::A, r));     // 10 deg/s x 0.92 / 60 deg/s
  TEST_ASSERT_EQUAL_INT32(499, feed_milli(32543, AxisMode::A, r));
  TEST_ASSERT_EQUAL_INT32(500, feed_milli(32544, AxisMode::A, r));     // capped: 0.5 x 60 = 30 deg/s
  TEST_ASSERT_EQUAL_INT32(400, feed_milli(UINT32_MAX, AxisMode::XY, r));  // no overflow
  TEST_ASSERT_EQUAL_INT32(299, feed_milli(UINT32_MAX, AxisMode::Z, r));
  TEST_ASSERT_EQUAL_INT32(500, feed_milli(UINT32_MAX, AxisMode::A, r));
  TEST_ASSERT_EQUAL_INT32(0, feed_milli(10000, AxisMode::None, r));    // no axis: no F, no command
}

void run_estimator(uint32_t t0) {
  FeedEstimator f;
  TEST_ASSERT_EQUAL_UINT32(5000, f.update(t0 + 0, 1000));  // 1000 um in 200 ms
  TEST_ASSERT_EQUAL_UINT32(5000, f.update(t0 + 20, 0));
  TEST_ASSERT_EQUAL_UINT32(5000, f.update(t0 + 199, 0));
  TEST_ASSERT_EQUAL_UINT32(0, f.update(t0 + 200, 0));      // now - t >= FEED_WINDOW_MS: gone
  TEST_ASSERT_EQUAL_UINT32(500, f.update(t0 + 300, 100));
  TEST_ASSERT_EQUAL_UINT32(1000, f.update(t0 + 400, 100));
  TEST_ASSERT_EQUAL_UINT32(500, f.update(t0 + 500, 0));    // only t0 + 400 left
  f.reset();
  TEST_ASSERT_EQUAL_UINT32(0, f.update(t0 + 520, 0));
}

void test_77_feed_estimator() {
  run_estimator(0);
  run_estimator(0xFFFFFF80u);  // overflow inside the window
  // More entries than FEED_RING inside the window: the oldest are lost, the estimate is too low (never too high).
  FeedEstimator f;
  uint32_t v = 0;
  for (uint32_t t = 0; t < 200; t += 10) v = f.update(t, 100);
  TEST_ASSERT_EQUAL_UINT32(16 * 100 * 5, v);
  // Saturation instead of overflow.
  FeedEstimator g;
  g.update(0, UINT32_MAX);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, g.update(20, UINT32_MAX));
}

MachineStatus idle() {
  MachineStatus s{};
  s.state = MachineState::Idle;
  return s;
}

PlannerTickResult tick(JogPlanner& p, uint32_t t, int32_t det, int32_t step, const MachineStatus& st,
                       AxisMode axis = AxisMode::X, BlockReason gate = BlockReason::None, uint32_t age = 0) {
  return p.tick(t, det, step, axis, gate, &st, age);
}

void test_78_window_z_and_xy_feed() {
  MachineStatus st = idle();
  {
    JogPlanner p{z1_pro_rates()};  // Z window 1000 um
    auto r = tick(p, 0, 10, 100, st, AxisMode::Z);
    TEST_ASSERT_EQUAL_STRING("$J Z1.000 F0.276", r.cmd.text);
    TEST_ASSERT_FALSE(tick(p, 20, 10, 100, st, AxisMode::Z).cmd.valid);
    TEST_ASSERT_FALSE(tick(p, 40, 10, 100, st, AxisMode::Z).cmd.valid);
    r = tick(p, 60, 10, 100, st, AxisMode::Z);
    TEST_ASSERT_FALSE(r.cmd.valid);
    TEST_ASSERT_TRUE(r.clipped);
    TEST_ASSERT_EQUAL_INT32(1000, p.target_um(2));
  }
  {
    JogPlanner p{z1_pro_rates()};  // XY: path speed is sqrt(2) x axis speed
    TEST_ASSERT_EQUAL_STRING("$J X1.000 Y1.000 F0.131", tick(p, 0, 10, 100, st, AxisMode::XY).cmd.text);
  }
}

void test_79_feed_follows_tempo() {
  MachineStatus st = idle();
  JogPlanner p{z1_pro_rates()};
  TEST_ASSERT_EQUAL_STRING("$J X1.000 F0.092", tick(p, 0, 10, 100, st).cmd.text);
  // Slow again after a pause: the old tempo has left FEED_WINDOW_MS, F drops to the lower limit.
  st.mpos_um[0] = 1000;
  auto r = tick(p, 400, 1, 10, st);
  TEST_ASSERT_EQUAL_STRING("$J X0.010 F0.008", r.cmd.text);
  // Detents dropped by the direction lock still count as tempo (the user turns that fast).
  MachineStatus s2 = idle();
  JogPlanner q{z1_pro_rates()};
  tick(q, 0, 10, 100, s2);  // target 1000, dir +1
  r = tick(q, 20, -10, 100, s2);
  TEST_ASSERT_FALSE(r.cmd.valid);
  TEST_ASSERT_TRUE(r.clipped);
  s2.mpos_um[0] = 1000;  // arrived, status 219 ms after the block: nothing queued
  r = tick(q, 219, 10, 100, s2);
  TEST_ASSERT_EQUAL_STRING("$J X1.000 F0.184", r.cmd.text);  // 1000 dropped at t = 20 + 1000 now = 2000 um
}

void test_80_bundling() {
  MachineStatus st = idle();  // mpos frozen at 0: every sent block counts as queued
  JogPlanner p{z1_pro_rates()};
  TEST_ASSERT_EQUAL_STRING("$J X0.030 F0.008", tick(p, 0, 3, 10, st).cmd.text);  // first: at once
  TEST_ASSERT_FALSE(tick(p, 20, 3, 10, st).cmd.valid);
  TEST_ASSERT_FALSE(tick(p, 40, 3, 10, st).cmd.valid);
  auto r = tick(p, 60, 3, 10, st);  // BUNDLE_MS after the first collected detent (t = 20)
  TEST_ASSERT_EQUAL_STRING("$J X0.090 F0.012", r.cmd.text);  // 120 um in 200 ms
  TEST_ASSERT_EQUAL_INT32(120, p.target_um(0));
  // Wheel released: the rest goes out at the latest BUNDLE_MS after its first detent.
  TEST_ASSERT_FALSE(tick(p, 80, 3, 10, st).cmd.valid);
  TEST_ASSERT_FALSE(tick(p, 100, 0, 10, st).cmd.valid);
  r = tick(p, 120, 0, 10, st);
  TEST_ASSERT_EQUAL_STRING("$J X0.030 F0.014", r.cmd.text);  // 150 um in 200 ms
  TEST_ASSERT_EQUAL_INT32(150, p.target_um(0));
}

// Collected detents are never sent after a block, a session end, an axis change, a hold or a reversal.
void test_81_bundle_discard() {
  {
    MachineStatus st = idle();
    JogPlanner p{z1_pro_rates()};  // gate blocks
    tick(p, 0, 3, 10, st);
    tick(p, 20, 3, 10, st);
    TEST_ASSERT_FALSE(tick(p, 40, 0, 10, st, AxisMode::X, BlockReason::MachineBusy).cmd.valid);
    TEST_ASSERT_FALSE(p.session_active());
    TEST_ASSERT_FALSE(tick(p, 60, 0, 10, st).cmd.valid);
    TEST_ASSERT_EQUAL_INT32(30, p.target_um(0));
  }
  {
    MachineStatus st = idle();
    JogPlanner p{z1_pro_rates()};  // axis change
    tick(p, 0, 3, 10, st);
    tick(p, 20, 3, 10, st);
    TEST_ASSERT_FALSE(tick(p, 40, 0, 10, st, AxisMode::XY).cmd.valid);
    TEST_ASSERT_FALSE(p.session_active());
  }
  {
    MachineStatus st = idle();
    JogPlanner p{z1_pro_rates()};  // end_session
    tick(p, 0, 3, 10, st);
    tick(p, 20, 3, 10, st);
    p.end_session(30);
    TEST_ASSERT_FALSE(tick(p, 40, 0, 10, st).cmd.valid);
    TEST_ASSERT_FALSE(tick(p, 60, 0, 10, st).cmd.valid);
  }
  {
    MachineStatus st = idle();
    JogPlanner p{z1_pro_rates()};  // hold on NoData: collected detents are dropped, nothing goes out after the hold
    tick(p, 0, 3, 10, st);
    tick(p, 20, 3, 10, st);
    tick(p, 40, 0, 10, st, AxisMode::X, BlockReason::NoData, 300);
    for (uint32_t t = 60; t <= 300; t += 20) TEST_ASSERT_FALSE(tick(p, t, 0, 10, st).cmd.valid);
    TEST_ASSERT_TRUE(p.session_active());
    TEST_ASSERT_EQUAL_INT32(30, p.target_um(0));
  }
  {
    MachineStatus st = idle();
    JogPlanner p{z1_pro_rates()};  // reversal while collecting: the old direction's detents are dropped
    tick(p, 0, 3, 10, st);  // target 30
    tick(p, 20, 3, 10, st);
    const auto r = tick(p, 40, -3, 10, st);
    TEST_ASSERT_FALSE(r.cmd.valid);
    TEST_ASSERT_TRUE(r.clipped);
    TEST_ASSERT_FALSE(tick(p, 60, 0, 10, st).cmd.valid);
    TEST_ASSERT_FALSE(tick(p, 80, 0, 10, st).cmd.valid);
    TEST_ASSERT_EQUAL_INT32(30, p.target_um(0));
    TEST_ASSERT_EQUAL_INT8(1, p.dir());
  }
}

// At most JOG_MAX_QUEUED blocks may be ahead of the reported mpos.
void test_82_queue_limit() {
  MachineStatus st = idle();  // mpos frozen at 0
  JogPlanner p{z1_pro_rates()};
  int sent = 0;
  // 2 detents x 10 um per tick: every block is > ARRIVE_UM ahead of the frozen mpos.
  for (uint32_t t = 0; t <= 1200; t += 20) {
    const auto r = tick(p, t, 2, 10, st);
    if (r.cmd.valid) ++sent;
    if (t > 660) TEST_ASSERT_FALSE(r.cmd.valid);  // 12th block went out at t = 660
  }
  TEST_ASSERT_EQUAL_INT(12, JOG_MAX_QUEUED);
  TEST_ASSERT_EQUAL_INT(JOG_MAX_QUEUED, sent);
  TEST_ASSERT_EQUAL_INT32(20 + 11 * 60, p.target_um(0));
  st.mpos_um[0] = 20;  // first block done: one slot free, everything collected since t = 680 goes out
  const auto r = tick(p, 1220, 2, 10, st);
  TEST_ASSERT_EQUAL_STRING("$J X0.560 F0.019", r.cmd.text);  // 200 um in 200 ms
}

void test_83_new_session_feed() {
  MachineStatus st = idle();
  {
    JogPlanner p{z1_pro_rates()};  // a new session starts with an empty tempo: lower limit of F for a slow detent
    TEST_ASSERT_EQUAL_STRING("$J X1.000 F0.092", tick(p, 0, 10, 100, st).cmd.text);
    p.end_session(20);
    TEST_ASSERT_EQUAL_STRING("$J X0.010 F0.008", tick(p, 240, 1, 10, st).cmd.text);
  }
  {
    JogPlanner p{z1_pro_rates()};  // axis change ends the session; the new axis starts with an empty tempo
    tick(p, 0, 10, 100, st);
    TEST_ASSERT_FALSE(tick(p, 20, 10, 100, st, AxisMode::Y).cmd.valid);
    TEST_ASSERT_EQUAL_STRING("$J Y0.010 F0.008", tick(p, 240, 1, 10, st, AxisMode::Y).cmd.text);
  }
}

// (e2) with moving mpos and a reversal: old blocks of the other direction never stay as phantoms (review 1),
// and a block sent after a reversal is not taken as done by a status from before it (review 2).
void test_84_queue_count_reversal() {
  MachineStatus st = idle();
  {
    JogPlanner p{z1_pro_rates()};
    tick(p, 0, 3, 10, st);                    // end 30
    for (uint32_t t = 20; t <= 60; t += 20) tick(p, t, 3, 10, st);  // t = 60: end 120
    st.mpos_um[0] = 120;                      // status at t = 300: both blocks done
    TEST_ASSERT_EQUAL_STRING("$J X-0.030 F0.008", tick(p, 300, -3, 10, st).cmd.text);  // reversal, at once
    for (uint32_t t = 320; t <= 340; t += 20) TEST_ASSERT_FALSE(tick(p, t, -3, 10, st).cmd.valid);
    TEST_ASSERT_EQUAL_STRING("$J X-0.090 F0.012", tick(p, 360, -3, 10, st).cmd.text);  // bundled, end 0
    st.mpos_um[0] = 0;  // back behind the old forward ends: the newest block is done, so all are
    TEST_ASSERT_EQUAL_STRING("$J X-0.010 F0.008", tick(p, 600, -1, 10, st).cmd.text);  // nothing queued
  }
  {
    MachineStatus s2 = idle();
    JogPlanner p{z1_pro_rates()};
    tick(p, 0, 46, 10, s2);   // end 460
    s2.mpos_um[0] = 450;      // status at t = 300, real position may be 458 going forward
    TEST_ASSERT_EQUAL_STRING("$J X-0.010 F0.008", tick(p, 300, -1, 10, s2).cmd.text);  // end 450
    // mpos equals the end of the new block, but this status is from before it: still counted.
    TEST_ASSERT_FALSE(tick(p, 320, -1, 10, s2, AxisMode::X, BlockReason::None, 20).cmd.valid);
  }
}

// Session start on a stale Idle while the old blocks are still queued: at most 2 x JOG_MAX_QUEUED = 24 < 32.
void test_85_new_session_after_full_queue() {
  MachineStatus st = idle();  // mpos frozen at 0
  JogPlanner p{z1_pro_rates()};
  int sent = 0;
  for (uint32_t t = 0; t <= 700; t += 20) sent += tick(p, t, 2, 10, st).cmd.valid ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(JOG_MAX_QUEUED, sent);
  p.end_session(720);
  sent = 0;
  for (uint32_t t = 940; t <= 2000; t += 20) sent += tick(p, t, 2, 10, st).cmd.valid ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(JOG_MAX_QUEUED, sent);
  TEST_ASSERT_TRUE(2 * JOG_MAX_QUEUED < 32);
}

void test_86_send_failed_after_bundle() {
  MachineStatus st = idle();
  JogPlanner p{z1_pro_rates()};
  tick(p, 0, 3, 10, st);  // target 30
  tick(p, 20, 3, 10, st);
  tick(p, 40, 3, 10, st);
  TEST_ASSERT_EQUAL_STRING("$J X0.090 F0.012", tick(p, 60, 3, 10, st).cmd.text);
  TEST_ASSERT_EQUAL_INT32(120, p.target_um(0));
  p.notify_send_failed();
  TEST_ASSERT_EQUAL_INT32(30, p.target_um(0));
  TEST_ASSERT_FALSE(p.session_active());
}

// Review 4: after a pause the machine stopped 3 um short of the target (step rounding). The block counts as
// done (within ARRIVE_UM), so a new detent goes out at once instead of being collected and dropped by (g).
void test_87_rest_after_pause() {
  MachineStatus st = idle();
  JogPlanner p{z1_pro_rates()};
  tick(p, 0, 3, 10, st);  // target 30
  st.mpos_um[0] = 27;
  const auto r = tick(p, 1000, 1, 10, st);
  TEST_ASSERT_EQUAL_STRING("$J X0.010 F0.008", r.cmd.text);
  TEST_ASSERT_TRUE(p.session_active());
  TEST_ASSERT_EQUAL_INT32(40, p.target_um(0));
}

// Collected detents are dropped without status and on NoData beyond NODATA_HOLD_MS.
void test_88_rest_without_status() {
  MachineStatus st = idle();
  {
    JogPlanner p{z1_pro_rates()};
    tick(p, 0, 3, 10, st);
    tick(p, 20, 3, 10, st);
    TEST_ASSERT_FALSE(p.tick(40, 0, 10, AxisMode::X, BlockReason::NoData, nullptr, 0).cmd.valid);
    TEST_ASSERT_FALSE(p.session_active());
    TEST_ASSERT_FALSE(tick(p, 60, 0, 10, st).cmd.valid);
    TEST_ASSERT_EQUAL_INT32(30, p.target_um(0));
  }
  {
    JogPlanner p{z1_pro_rates()};
    tick(p, 0, 3, 10, st);
    tick(p, 20, 3, 10, st);
    TEST_ASSERT_FALSE(tick(p, 40, 0, 10, st, AxisMode::X, BlockReason::NoData, NODATA_HOLD_MS + 1).cmd.valid);
    TEST_ASSERT_FALSE(p.session_active());
    TEST_ASSERT_FALSE(tick(p, 60, 0, 10, st).cmd.valid);
    TEST_ASSERT_EQUAL_INT32(30, p.target_um(0));
  }
}

// Bound of the (e2) count: at most one block too low. Twelve blocks of 10 um (ends 10 ... 120) with mpos 0:
// block 1 (10 um left) counts as done once a late status shows it, so 11 are counted while 12 are queued.
// A later status with mpos = 10 drops block 2 (10 um left) too: 10 counted, 11 really queued.
// A block cut by the window below ARRIVE_UM is not sent.
void test_89_queue_count_bound() {
  {
    MachineStatus st = idle();
    JogPlanner p{z1_pro_rates()};
    int sent = 0;
    sent += tick(p, 0, 1, 10, st).cmd.valid ? 1 : 0;  // end 10, at once
    for (uint32_t t = 20; sent < 12; t += 60) {        // collect at t, send at t + 40 (BUNDLE_MS)
      tick(p, t, 1, 10, st);
      tick(p, t + 20, 0, 10, st);
      sent += tick(p, t + 40, 0, 10, st).cmd.valid ? 1 : 0;
    }
    TEST_ASSERT_EQUAL_INT32(120, p.target_um(0));
    tick(p, 700, 0, 10, st);
    TEST_ASSERT_EQUAL_INT(11, p.queued());
    st.mpos_um[0] = 10;
    tick(p, 720, 0, 10, st);
    TEST_ASSERT_EQUAL_INT(10, p.queued());
  }
  {
    MachineStatus st = idle();
    JogPlanner p{z1_pro_rates()};  // window full up to 5 um: the 5 um block is dropped, not sent
    for (uint32_t t = 0; t <= 120; t += 20) tick(p, t, 10, 100, st);  // target 5000 as in test 35
    st.mpos_um[0] = 5;
    tick(p, 140, 1, 100, st);
    tick(p, 160, 0, 100, st);
    const auto r = tick(p, 180, 0, 100, st);
    TEST_ASSERT_FALSE(r.cmd.valid);
    TEST_ASSERT_TRUE(r.clipped);
    TEST_ASSERT_EQUAL_INT32(5000, p.target_um(0));
  }
}

}  // namespace

void run_feed_tests() {
  RUN_TEST(test_76_feed_milli);
  RUN_TEST(test_77_feed_estimator);
  RUN_TEST(test_78_window_z_and_xy_feed);
  RUN_TEST(test_79_feed_follows_tempo);
  RUN_TEST(test_80_bundling);
  RUN_TEST(test_81_bundle_discard);
  RUN_TEST(test_82_queue_limit);
  RUN_TEST(test_83_new_session_feed);
  RUN_TEST(test_84_queue_count_reversal);
  RUN_TEST(test_85_new_session_after_full_queue);
  RUN_TEST(test_86_send_failed_after_bundle);
  RUN_TEST(test_87_rest_after_pause);
  RUN_TEST(test_88_rest_without_status);
  RUN_TEST(test_89_queue_count_bound);
}
