// SPEC 11, tests 93-99: machine max rates read at runtime, F limits from absolute speeds (ticket #38).
#include <unity.h>

#include <cstring>

#include "feed_rate.h"
#include "frame.h"
#include "jog_gate.h"
#include "jog_planner.h"
#include "machine_rates.h"
#include "test_suites.h"
#include "text_lines.h"

namespace {

MachineRates rates_of(uint32_t x, uint32_t y, uint32_t z, uint32_t a, bool has_a = true) {
  MachineRates r;
  r.um_s[0] = x;
  r.um_s[1] = y;
  r.um_s[2] = z;
  r.um_s[3] = a;
  r.xyz = true;
  r.a = has_a;
  return r;
}

void assert_limits(AxisMode axis, const MachineRates& r, int32_t lo, int32_t cap) {
  int32_t l = -1;
  int32_t c = -1;
  TEST_ASSERT_TRUE(feed_limits(axis, r, l, c));
  TEST_ASSERT_EQUAL_INT32(lo, l);
  TEST_ASSERT_EQUAL_INT32(cap, c);
}

// Absolute speed of the cap: F x rate never above JOG_MAX_SPEED (XY: path), whatever the machine reports.
void assert_bounded(AxisMode axis, const MachineRates& r, uint32_t rate, uint32_t vmax, uint32_t vmin) {
  int32_t lo = 0;
  int32_t cap = 0;
  TEST_ASSERT_TRUE(feed_limits(axis, r, lo, cap));
  TEST_ASSERT_TRUE(lo >= 1);
  TEST_ASSERT_TRUE(lo <= cap);
  TEST_ASSERT_TRUE(cap <= F_MAX_MILLI);
  TEST_ASSERT_TRUE(static_cast<uint64_t>(cap) * rate <= static_cast<uint64_t>(vmax) * 1000);
  // The floor keeps the intended minimum speed unless that would exceed the cap.
  if (lo < cap) TEST_ASSERT_TRUE(static_cast<uint64_t>(lo) * rate >= static_cast<uint64_t>(vmin) * 1000);
  TEST_ASSERT_EQUAL_INT32(cap, feed_milli(UINT32_MAX, axis, r));
  TEST_ASSERT_EQUAL_INT32(lo, feed_milli(0, axis, r));
}

void test_93_feed_limits() {
  // Z1 Pro (M203.1 2026-11-11): the absolute limits of #32 stay, F scales down.
  const MachineRates z1 = z1_pro_rates();
  assert_limits(AxisMode::X, z1, 8, 400);
  assert_limits(AxisMode::Y, z1, 8, 400);
  assert_limits(AxisMode::XY, z1, 8, 400);
  assert_limits(AxisMode::Z, z1, 12, 299);  // 5000000 / 16667 = 299.99, rounded down
  assert_limits(AxisMode::A, z1, 20, 500);
  // The rates #32 assumed (configZ1Pro.default 1200/1200/600 mm/min): exactly the old F limits.
  const MachineRates old = rates_of(20000, 20000, 10000, 60000);
  assert_limits(AxisMode::X, old, 20, 1000);
  assert_limits(AxisMode::XY, old, 20, 1000);
  assert_limits(AxisMode::Z, old, 20, 500);
  assert_limits(AxisMode::A, old, 20, 500);
  // Highest plausible rates: the cap shrinks, the speed does not grow.
  const MachineRates hi = rates_of(RATE_MAX_UM_S[0], RATE_MAX_UM_S[1], RATE_MAX_UM_S[2], RATE_MAX_UM_S[3]);
  assert_limits(AxisMode::X, hi, 3, 119);
  assert_limits(AxisMode::Z, hi, 2, 29);
  assert_limits(AxisMode::A, hi, 2, 50);
  // Lowest plausible rates: F never above F_MAX_MILLI; the floor never above the cap (A: 1.2 deg/s > 1 deg/s).
  const MachineRates lo = rates_of(RATE_MIN_UM_S[0], RATE_MIN_UM_S[1], RATE_MIN_UM_S[2], RATE_MIN_UM_S[3]);
  assert_limits(AxisMode::X, lo, 400, 1000);
  assert_limits(AxisMode::Z, lo, 200, 1000);
  assert_limits(AxisMode::A, lo, 1000, 1000);
  // XY uses the slower axis, the limits apply to the path.
  assert_limits(AxisMode::XY, rates_of(50000, 30000, 16667, 60000), 14, 666);
  TEST_ASSERT_EQUAL_INT32(434, feed_milli(10000, AxisMode::XY, rates_of(50000, 30000, 16667, 60000)));  // 433.6
  // Sweep over the whole plausible range of every axis.
  for (uint32_t rate = RATE_MIN_UM_S[0]; rate <= RATE_MAX_UM_S[0]; rate += 997) {
    const MachineRates r = rates_of(rate, rate, rate, rate);
    assert_bounded(AxisMode::X, r, rate, JOG_MAX_SPEED_UM_S[0], JOG_MIN_SPEED_UM_S[0]);
    assert_bounded(AxisMode::Y, r, rate, JOG_MAX_SPEED_UM_S[1], JOG_MIN_SPEED_UM_S[1]);
    assert_bounded(AxisMode::XY, r, rate, JOG_MAX_SPEED_UM_S[0], JOG_MIN_SPEED_UM_S[0]);
    assert_bounded(AxisMode::Z, r, rate, JOG_MAX_SPEED_UM_S[2], JOG_MIN_SPEED_UM_S[2]);
  }
  for (uint32_t rate = RATE_MIN_UM_S[3]; rate <= RATE_MAX_UM_S[3]; rate += 1999) {
    assert_bounded(AxisMode::A, rates_of(50000, 50000, 16667, rate), rate, JOG_MAX_SPEED_UM_S[3],
                   JOG_MIN_SPEED_UM_S[3]);
  }
  // Unknown rates: no limits, no F.
  int32_t l = 7;
  int32_t c = 7;
  const MachineRates none{};
  TEST_ASSERT_FALSE(feed_limits(AxisMode::X, none, l, c));
  TEST_ASSERT_EQUAL_INT32(0, l);
  TEST_ASSERT_EQUAL_INT32(0, c);
  TEST_ASSERT_EQUAL_INT32(0, feed_milli(10000, AxisMode::X, none));
  TEST_ASSERT_EQUAL_INT32(0, feed_milli(10000, AxisMode::XY, none));
  TEST_ASSERT_EQUAL_INT32(0, feed_milli(10000, AxisMode::Z, none));
  const MachineRates no_a = rates_of(50000, 50000, 16667, 60000, false);
  TEST_ASSERT_EQUAL_INT32(0, feed_milli(10000, AxisMode::A, no_a));
  TEST_ASSERT_EQUAL_INT32(184, feed_milli(10000, AxisMode::X, no_a));
  TEST_ASSERT_FALSE(feed_limits(AxisMode::None, z1, l, c));
  TEST_ASSERT_TRUE(rates_cover(z1, AxisMode::XY));
  TEST_ASSERT_FALSE(rates_cover(z1, AxisMode::None));
  TEST_ASSERT_FALSE(rates_cover(no_a, AxisMode::A));
  TEST_ASSERT_TRUE(rates_cover(no_a, AxisMode::Z));
}

RateLine parse(const char* line, MachineRates& out) { return parse_rate_line(line, strlen(line), out); }

void test_94_parse_rate_line() {
  MachineRates r;
  // Exact output of M203.1 at the Z1 Pro (v1.1.3 format " %c: %g ", B = tool changer).
  TEST_ASSERT_EQUAL(RateLine::Valid, parse(" X: 50  Y: 50  Z: 16.6667  A: 60  B: 1.66667 ", r));
  TEST_ASSERT_EQUAL_UINT32(50000, r.um_s[0]);
  TEST_ASSERT_EQUAL_UINT32(50000, r.um_s[1]);
  TEST_ASSERT_EQUAL_UINT32(16667, r.um_s[2]);  // 16666.7 rounded up
  TEST_ASSERT_EQUAL_UINT32(60000, r.um_s[3]);
  TEST_ASSERT_TRUE(r.xyz);
  TEST_ASSERT_TRUE(r.a);
  // Without A (no rotary actuator configured): X, Y, Z known, A not.
  TEST_ASSERT_EQUAL(RateLine::Valid, parse(" X: 20  Y: 30  Z: 10 ", r));
  TEST_ASSERT_EQUAL_UINT32(30000, r.um_s[1]);
  TEST_ASSERT_TRUE(r.xyz);
  TEST_ASSERT_FALSE(r.a);
  TEST_ASSERT_EQUAL_UINT32(0, r.um_s[3]);
  TEST_ASSERT_EQUAL(RateLine::Valid, parse("X:50 Y:50 Z:16.6661", r));  // no blanks, rounding up
  TEST_ASSERT_EQUAL_UINT32(16667, r.um_s[2]);
  TEST_ASSERT_EQUAL(RateLine::Valid, parse("X: 50 Y: 50 Z: 16.666", r));
  TEST_ASSERT_EQUAL_UINT32(16666, r.um_s[2]);
  // Range bounds (rounded value): 1000 ... 166667 um/s, A 1000 ... 600000 mdeg/s.
  TEST_ASSERT_EQUAL(RateLine::Valid, parse("X: 1 Y: 166.667 Z: 0.9991 A: 600", r));
  TEST_ASSERT_EQUAL_UINT32(1000, r.um_s[0]);
  TEST_ASSERT_EQUAL_UINT32(166667, r.um_s[1]);
  TEST_ASSERT_EQUAL_UINT32(1000, r.um_s[2]);  // 999.1 rounded up
  TEST_ASSERT_EQUAL_UINT32(600000, r.um_s[3]);
  // Other output on the shared connection: ignored, out untouched.
  MachineRates keep = z1_pro_rates();
  const char* other[] = {"ok", "", "   ", "!!", "<Idle|MPos:0,0,0|WPos:0,0,0|C:4,1,0,1>",
                         "sd: alpha_max_rate is set to 3000.0", " Y: 50  X: 50  Z: 16.6667 ", "x: 50 y: 50 z: 10",
                         "ALARM: Hard limit"};
  for (const char* line : other) {
    TEST_ASSERT_EQUAL_MESSAGE(RateLine::NotRates, parse(line, keep), line);
  }
  TEST_ASSERT_EQUAL_UINT32(16667, keep.um_s[2]);
  // Starts like an answer but is not a valid one: rejected, out untouched.
  const char* bad[] = {
      " X: 50  Y: 50  Z: 16.6667  S: 50 ",  // plain M203 (cartesian speeds)
      " X: 50  Y: 50 ",                     // Z missing
      " X: 50  Y: 50  Z: 16  X: 50 ",       // duplicate
      " X: 50  Y: 50  Z: 16  A: 60  A: 60 ",
      " X: 5e+01  Y: 50  Z: 16 ",           // exponent
      " X: -50  Y: 50  Z: 16 ",
      " X: abc  Y: 50  Z: 16 ",
      " X: . Y: 50  Z: 16 ",
      " X:",
      " X: 50 Y: 50 Z: 16 junk",
      " X: 50, Y: 50, Z: 16",
      " X: 50  Y: 50  Z: 0 ",               // below RATE_MIN
      " X: 0.999  Y: 50  Z: 16 ",
      " X: 166.6671  Y: 50  Z: 16 ",        // 166668 > RATE_MAX
      " X: 50  Y: 50  Z: 16  A: 600.001 ",
      " X: 50  Y: 50  Z: 16  A: 0.5 ",
      " X: 12345678  Y: 50  Z: 16 ",        // too many digits
      " X: 50.1234567  Y: 50  Z: 16 ",
      " X: 50  Y: 50  Z: 16  B: x ",
      " X: 50  Y: 50  Z: 16  D: 1 ",
  };
  for (const char* line : bad) {
    TEST_ASSERT_EQUAL_MESSAGE(RateLine::Invalid, parse(line, keep), line);
  }
  TEST_ASSERT_EQUAL_UINT32(50000, keep.um_s[0]);
  TEST_ASSERT_EQUAL_UINT32(16667, keep.um_s[2]);
  TEST_ASSERT_TRUE(keep.a);
  // Length is honoured (TextLines hands a NUL-terminated line, but n counts).
  TEST_ASSERT_EQUAL(RateLine::Invalid, parse_rate_line(" X: 50  Y: 50  Z: 16.6667 ", 9, r));
}

const char* const kAnswer = " X: 50  Y: 50  Z: 16.6667  A: 60  B: 1.66667 ";

RateLine feed_line(RateQuery& q, const char* line, uint32_t now) { return q.on_line(line, strlen(line), now); }

void run_query(uint32_t t0) {
  RateQuery q;
  q.reset();
  TEST_ASSERT_FALSE(q.known());
  TEST_ASSERT_FALSE(q.rates().xyz);
  TEST_ASSERT_FALSE(q.poll(t0, false));  // no status yet: no query
  TEST_ASSERT_TRUE(q.poll(t0 + 10, true));
  TEST_ASSERT_EQUAL_UINT32(1, q.queries());
  TEST_ASSERT_FALSE(q.poll(t0 + 20, true));  // one in flight
  TEST_ASSERT_EQUAL(RateLine::NotRates, feed_line(q, "ok", t0 + 30));
  TEST_ASSERT_FALSE(q.poll(t0 + 1009, true));
  TEST_ASSERT_FALSE(q.poll(t0 + 1010, true));  // timeout: wait RATE_RETRY_MS[0]
  TEST_ASSERT_FALSE(q.poll(t0 + 2009, true));
  TEST_ASSERT_TRUE(q.poll(t0 + 2010, true));
  TEST_ASSERT_FALSE(q.poll(t0 + 3010, true));  // timeout: wait 2000
  TEST_ASSERT_FALSE(q.poll(t0 + 5009, true));
  TEST_ASSERT_TRUE(q.poll(t0 + 5010, true));
  TEST_ASSERT_FALSE(q.poll(t0 + 6010, true));  // timeout: wait 5000
  TEST_ASSERT_FALSE(q.poll(t0 + 11009, true));
  TEST_ASSERT_TRUE(q.poll(t0 + 11010, true));
  TEST_ASSERT_FALSE(q.poll(t0 + 12010, true));  // wait stays 5000
  TEST_ASSERT_FALSE(q.poll(t0 + 17009, true));
  TEST_ASSERT_TRUE(q.poll(t0 + 17010, true));
  TEST_ASSERT_EQUAL_UINT32(5, q.queries());
  // Invalid answer while in flight: retry after the next backoff step without waiting for the timeout.
  TEST_ASSERT_EQUAL(RateLine::Invalid, feed_line(q, " X: 50  Y: 50 ", t0 + 17100));
  TEST_ASSERT_FALSE(q.known());
  TEST_ASSERT_FALSE(q.poll(t0 + 22099, true));
  TEST_ASSERT_TRUE(q.poll(t0 + 22100, true));
  // Valid answer: known, never queried again.
  TEST_ASSERT_EQUAL(RateLine::Valid, feed_line(q, kAnswer, t0 + 22150));
  TEST_ASSERT_TRUE(q.known());
  TEST_ASSERT_TRUE(q.rates().xyz);
  TEST_ASSERT_TRUE(q.rates().a);
  TEST_ASSERT_EQUAL_UINT32(16667, q.rates().um_s[2]);
  for (uint32_t t = t0 + 22200; t != t0 + 40200; t += 1000) TEST_ASSERT_FALSE(q.poll(t, true));
  // Later answers (other client, repeated) do not change known rates.
  TEST_ASSERT_EQUAL(RateLine::NotRates, feed_line(q, " X: 100  Y: 100  Z: 100 ", t0 + 40300));
  TEST_ASSERT_EQUAL_UINT32(50000, q.rates().um_s[0]);
  // Reconnect: unknown again, query at once with the first status.
  q.reset();
  TEST_ASSERT_FALSE(q.known());
  TEST_ASSERT_FALSE(q.rates().xyz);
  TEST_ASSERT_FALSE(q.rates().a);
  TEST_ASSERT_EQUAL_UINT32(0, q.queries());
  TEST_ASSERT_TRUE(q.poll(t0 + 40400, true));
}

void test_95_rate_query() {
  TEST_ASSERT_EQUAL_STRING("M203.1", RATE_QUERY_TEXT);  // without arguments: only reports, never sets
  run_query(0);
  run_query(0xFFFFF000u);  // overflow during the retries
  {
    RateQuery q;  // invalid line while waiting: ignored, the schedule stays
    q.reset();
    TEST_ASSERT_TRUE(q.poll(0, true));
    TEST_ASSERT_FALSE(q.poll(1000, true));  // timeout, next query at 2000
    TEST_ASSERT_EQUAL(RateLine::Invalid, feed_line(q, " X: 0  Y: 0  Z: 0 ", 1500));
    TEST_ASSERT_FALSE(q.poll(1999, true));
    TEST_ASSERT_TRUE(q.poll(2000, true));
  }
  {
    RateQuery q;  // late answer to a timed-out query while waiting: taken (it carries the live values)
    q.reset();
    TEST_ASSERT_TRUE(q.poll(0, true));
    TEST_ASSERT_FALSE(q.poll(1000, true));
    TEST_ASSERT_EQUAL(RateLine::Valid, feed_line(q, " X: 50  Y: 50  Z: 16.6667 ", 1200));
    TEST_ASSERT_TRUE(q.known());
    TEST_ASSERT_FALSE(q.rates().a);
    TEST_ASSERT_FALSE(q.poll(2000, true));
  }
  {
    RateQuery q;  // a default-constructed query behaves like reset()
    TEST_ASSERT_FALSE(q.known());
    TEST_ASSERT_TRUE(q.poll(5, true));
  }
}

GateInputs ok_inputs() {
  GateInputs in{};
  in.wifi_ok = true;
  in.tcp_ok = true;
  in.have_status = true;
  in.status.state = MachineState::Idle;
  in.status.has_a = true;
  in.axis = AxisMode::X;
  in.rates_known = true;
  return in;
}

void test_96_gate_and_planner_rates() {
  // Gate rule 7a: after job and alarm, before the state rules; GateInputs{} defaults to unknown (closed).
  GateInputs in = ok_inputs();
  TEST_ASSERT_EQUAL(BlockReason::None, evaluate_gate(in));
  in.rates_known = false;
  TEST_ASSERT_EQUAL(BlockReason::NoRates, evaluate_gate(in));
  in.session_active = true;
  in.session_consistent = true;
  TEST_ASSERT_EQUAL(BlockReason::NoRates, evaluate_gate(in));  // also blocks rule 8
  in = ok_inputs();
  in.rates_known = false;
  in.axis = AxisMode::None;
  TEST_ASSERT_EQUAL(BlockReason::AxisNone, evaluate_gate(in));
  in = ok_inputs();
  in.rates_known = false;
  in.status.playing = true;
  TEST_ASSERT_EQUAL(BlockReason::JobRunning, evaluate_gate(in));
  in = ok_inputs();
  in.rates_known = false;
  in.status.state = MachineState::Alarm;
  TEST_ASSERT_EQUAL(BlockReason::Alarm, evaluate_gate(in));
  in = ok_inputs();
  in.rates_known = false;
  in.have_status = false;
  TEST_ASSERT_EQUAL(BlockReason::NoData, evaluate_gate(in));
  GateInputs empty{};
  empty.wifi_ok = true;
  empty.tcp_ok = true;
  empty.have_status = true;
  empty.status.state = MachineState::Idle;
  empty.axis = AxisMode::X;
  TEST_ASSERT_EQUAL(BlockReason::NoRates, evaluate_gate(empty));

  MachineStatus st{};
  st.state = MachineState::Idle;
  st.has_a = true;
  {
    JogPlanner p;  // no rates: even with gate None nothing is sent, no session starts
    for (uint32_t t = 0; t <= 200; t += 20) {
      const auto r = p.tick(t, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
      TEST_ASSERT_FALSE(r.cmd.valid);
      TEST_ASSERT_FALSE(r.session_active);
    }
    TEST_ASSERT_EQUAL_INT32(0, p.target_um(0));
    p.set_rates(z1_pro_rates());
    const auto r = p.tick(220, 10, 100, AxisMode::X, BlockReason::None, &st, 0);
    TEST_ASSERT_EQUAL_STRING("$J X1.000 F0.092", r.cmd.text);  // 5 mm/s x 0.92 / 50 mm/s
  }
  {
    JogPlanner p{z1_pro_rates()};  // rates lost during a session (defence): session ends, no command
    TEST_ASSERT_TRUE(p.tick(0, 3, 10, AxisMode::X, BlockReason::None, &st, 0).cmd.valid);
    p.set_rates(MachineRates{});
    const auto r = p.tick(20, 3, 10, AxisMode::X, BlockReason::None, &st, 0);
    TEST_ASSERT_FALSE(r.cmd.valid);
    TEST_ASSERT_FALSE(r.session_active);
    TEST_ASSERT_EQUAL_INT32(30, p.target_um(0));
  }
  {
    MachineRates no_a = z1_pro_rates();  // A not reported: A never moves, X does
    no_a.a = false;
    JogPlanner p{no_a};
    TEST_ASSERT_FALSE(p.tick(0, 3, 10, AxisMode::A, BlockReason::None, &st, 0).cmd.valid);
    TEST_ASSERT_EQUAL_INT32(0, p.target_um(3));
    TEST_ASSERT_EQUAL_STRING("$J Z0.030 F0.012", p.tick(1000, 3, 10, AxisMode::Z, BlockReason::None, &st, 0).cmd.text);
  }
  {
    JogPlanner p{rates_of(20000, 20000, 10000, 60000)};  // the rates #32 assumed: the F values of #32
    TEST_ASSERT_EQUAL_STRING("$J X1.000 F0.230", p.tick(0, 10, 100, AxisMode::X, BlockReason::None, &st, 0).cmd.text);
  }
  {
    // Highest plausible rate: 5 mm/s turned -> F0.028 x 166.667 mm/s = 4.67 mm/s (0.92 x 5 rounded up).
    JogPlanner p{rates_of(RATE_MAX_UM_S[0], RATE_MAX_UM_S[1], RATE_MAX_UM_S[2], RATE_MAX_UM_S[3])};
    TEST_ASSERT_EQUAL_STRING("$J X1.000 F0.028", p.tick(0, 10, 100, AxisMode::X, BlockReason::None, &st, 0).cmd.text);
  }
}

// Answer of the real Z1 Pro (firmware 1.1.3.0.1.13, captured 2026-11-11): seven 0x90 frames, one per printf
// of Robot.cpp, then "\r\n" and "ok\r\n" in frames of their own. A is capped at 30 deg/s on this machine.
// Same path as machine_link.cpp: frame::Decoder -> TextLines -> RateQuery::on_line, here with a status
// frame between the fragments and once fed byte by byte.
void run_real_answer(bool bytewise, bool with_status) {
  static const char* const kFrags[7] = {" X: 50 ", " Y: 50 ", " Z: 16.6667 ", " A: 30 ", " B: 1.66667 ", "\r\n", "ok\r\n"};
  uint8_t stream[512];
  size_t len = 0;
  for (int i = 0; i < 7; ++i) {
    len += frame::build(frame::TYPE_TEXT, reinterpret_cast<const uint8_t*>(kFrags[i]), strlen(kFrags[i]),
                        stream + len, sizeof(stream) - len);
    if (with_status && i == 2) {
      const char* st = "<Idle|MPos:0,0,0,0,0|WPos:0,0,0,0,0|C:4,1,0,1>";
      len += frame::build(frame::TYPE_STATUS, reinterpret_cast<const uint8_t*>(st), strlen(st), stream + len,
                          sizeof(stream) - len);
    }
  }
  frame::Decoder dec;
  TextLines lines;
  RateQuery q;
  q.reset();
  TEST_ASSERT_TRUE(q.poll(0, true));
  int n_lines = 0;
  int n_valid = 0;
  int n_status = 0;
  auto on_frame = [&](const frame::Frame& f) {
    if (f.type == frame::TYPE_STATUS) ++n_status;
    if (f.type != frame::TYPE_TEXT) return;
    lines.feed(f.payload, f.n, [&](const char* line, size_t n) {
      ++n_lines;
      if (q.on_line(line, n, 50) == RateLine::Valid) {
        ++n_valid;
        TEST_ASSERT_EQUAL_STRING(" X: 50  Y: 50  Z: 16.6667  A: 30  B: 1.66667 ", line);
      }
    });
  };
  if (bytewise) {
    for (size_t i = 0; i < len; ++i) dec.feed(stream + i, 1, on_frame);
  } else {
    dec.feed(stream, len, on_frame);
  }
  TEST_ASSERT_EQUAL_INT(2, n_lines);  // the rates line and "ok"
  TEST_ASSERT_EQUAL_INT(1, n_valid);
  TEST_ASSERT_EQUAL_INT(with_status ? 1 : 0, n_status);
  TEST_ASSERT_TRUE(q.known());
  TEST_ASSERT_TRUE(q.rates().xyz);
  TEST_ASSERT_TRUE(q.rates().a);
  TEST_ASSERT_EQUAL_UINT32(50000, q.rates().um_s[0]);
  TEST_ASSERT_EQUAL_UINT32(50000, q.rates().um_s[1]);
  TEST_ASSERT_EQUAL_UINT32(16667, q.rates().um_s[2]);  // 16.6667 mm/s rounded up
  TEST_ASSERT_EQUAL_UINT32(30000, q.rates().um_s[3]);
  TEST_ASSERT_FALSE(q.poll(5000, true));
}

void test_97_real_m203_answer() {
  run_real_answer(false, false);
  run_real_answer(true, false);
  run_real_answer(false, true);
  run_real_answer(true, true);
  // A at 30 deg/s: cap = min(1000, 30000 x 1000 / 30000) = F_MAX_MILLI, i.e. still at most 30 deg/s.
  MachineRates r = z1_pro_rates();
  r.um_s[3] = 30000;
  int32_t lo = 0;
  int32_t cap = 0;
  TEST_ASSERT_TRUE(feed_limits(AxisMode::A, r, lo, cap));
  TEST_ASSERT_EQUAL_INT32(40, lo);     // 1.2 deg/s
  TEST_ASSERT_EQUAL_INT32(1000, cap);  // 30 deg/s
  TEST_ASSERT_EQUAL_INT32(307, feed_milli(10000, AxisMode::A, r));  // 10 deg/s x 0.92 / 30 deg/s = 306.7
}

MachineStatus status_in(MachineState st, bool halted = false) {
  MachineStatus s{};
  s.state = st;
  s.halted = halted;
  return s;
}

// Review: in HALT the firmware answers M203.1 with "error:Alarm lock" (grbl mode) or "!!"; querying there would
// overwrite the real alarm line every retry. No query while Alarm/halted, the next one right after the alarm.
void test_98_rate_query_alarm() {
  const MachineStatus idle_st = status_in(MachineState::Idle);
  const MachineStatus run_st = status_in(MachineState::Run);
  const MachineStatus alarm_st = status_in(MachineState::Alarm);
  const MachineStatus halted_st = status_in(MachineState::Idle, true);
  TEST_ASSERT_FALSE(RateQuery::ready(nullptr));
  TEST_ASSERT_TRUE(RateQuery::ready(&idle_st));
  TEST_ASSERT_TRUE(RateQuery::ready(&run_st));
  TEST_ASSERT_FALSE(RateQuery::ready(&alarm_st));
  TEST_ASSERT_FALSE(RateQuery::ready(&halted_st));
  TEST_ASSERT_TRUE(RateQuery::refresh_allowed(&idle_st, false));
  TEST_ASSERT_FALSE(RateQuery::refresh_allowed(&idle_st, true));
  TEST_ASSERT_FALSE(RateQuery::refresh_allowed(&run_st, false));
  TEST_ASSERT_FALSE(RateQuery::refresh_allowed(&alarm_st, false));
  TEST_ASSERT_FALSE(RateQuery::refresh_allowed(&halted_st, false));
  TEST_ASSERT_FALSE(RateQuery::refresh_allowed(nullptr, false));
  {
    RateQuery q;  // connected in Alarm: no query at all, the first one with the first status after it
    q.reset();
    for (uint32_t t = 0; t <= 20000; t += 20) TEST_ASSERT_FALSE(q.poll(t, RateQuery::ready(&alarm_st)));
    TEST_ASSERT_EQUAL_UINT32(0, q.queries());
    TEST_ASSERT_EQUAL(RateLine::NotRates, feed_line(q, "error:Alarm lock", 20010));
    TEST_ASSERT_EQUAL(RateLine::NotRates, feed_line(q, "!!", 20010));
    TEST_ASSERT_FALSE(q.known());
    TEST_ASSERT_TRUE(q.poll(20020, RateQuery::ready(&idle_st)));
  }
  {
    RateQuery q;  // alarm while a query is in flight: no backoff wait after the alarm
    q.reset();
    TEST_ASSERT_TRUE(q.poll(0, true));
    TEST_ASSERT_EQUAL(RateLine::NotRates, feed_line(q, "error:Alarm lock", 30));
    TEST_ASSERT_FALSE(q.poll(1000, false));  // timeout
    TEST_ASSERT_FALSE(q.poll(1020, false));  // not ready: wait dropped
    TEST_ASSERT_FALSE(q.poll(1500, false));
    TEST_ASSERT_TRUE(q.poll(1520, true));    // alarm cleared: at once, not after RATE_RETRY_MS
    TEST_ASSERT_EQUAL_UINT32(2, q.queries());
    TEST_ASSERT_FALSE(q.poll(2520, true));   // timeout again: backoff starts over at RATE_RETRY_MS[0]
    TEST_ASSERT_FALSE(q.poll(3519, true));
    TEST_ASSERT_TRUE(q.poll(3520, true));
  }
}

// Re-read while connected (review): every RATE_REFRESH_MS while Idle without session; a failure keeps the rates.
void run_refresh(uint32_t t0) {
  RateQuery q;
  q.reset();
  TEST_ASSERT_TRUE(q.poll(t0, true, true));
  TEST_ASSERT_EQUAL(RateLine::Valid, feed_line(q, kAnswer, t0 + 100));
  TEST_ASSERT_EQUAL_UINT32(60000, q.rates().um_s[3]);
  TEST_ASSERT_FALSE(q.poll(t0 + 100 + RATE_REFRESH_MS - 1, true, true));
  TEST_ASSERT_FALSE(q.poll(t0 + 100 + RATE_REFRESH_MS, true, false));  // not Idle or a session runs
  TEST_ASSERT_TRUE(q.poll(t0 + 100 + RATE_REFRESH_MS + 20, true, true));
  TEST_ASSERT_EQUAL_UINT32(2, q.queries());
  TEST_ASSERT_TRUE(q.known());  // in flight: the old rates stay valid
  TEST_ASSERT_EQUAL_UINT32(60000, q.rates().um_s[3]);
  TEST_ASSERT_FALSE(q.poll(t0 + 30140, true, true));  // one in flight
  TEST_ASSERT_EQUAL(RateLine::NotRates, feed_line(q, "ok", t0 + 30150));
  TEST_ASSERT_EQUAL(RateLine::Valid, feed_line(q, " X: 50  Y: 50  Z: 16.6667  A: 30 ", t0 + 30200));  // changed
  TEST_ASSERT_EQUAL_UINT32(30000, q.rates().um_s[3]);
  // Garbled re-read: rates and known() stay, counted once.
  TEST_ASSERT_FALSE(q.poll(t0 + 30200 + RATE_REFRESH_MS - 1, true, true));
  TEST_ASSERT_TRUE(q.poll(t0 + 30200 + RATE_REFRESH_MS, true, true));
  TEST_ASSERT_EQUAL(RateLine::Invalid, feed_line(q, " X: 50  Y: 50 ", t0 + 60300));
  TEST_ASSERT_TRUE(q.known());
  TEST_ASSERT_TRUE(q.rates().xyz);
  TEST_ASSERT_EQUAL_UINT32(30000, q.rates().um_s[3]);
  TEST_ASSERT_EQUAL_UINT32(1, q.refresh_failures());
  TEST_ASSERT_EQUAL(RateLine::NotRates, feed_line(q, " X: 99  Y: 99  Z: 99 ", t0 + 60400));  // not in flight
  TEST_ASSERT_EQUAL_UINT32(50000, q.rates().um_s[0]);
  // No answer: timeout keeps the rates, the next re-read RATE_REFRESH_MS later.
  TEST_ASSERT_FALSE(q.poll(t0 + 60300 + RATE_REFRESH_MS - 1, true, true));
  TEST_ASSERT_TRUE(q.poll(t0 + 60300 + RATE_REFRESH_MS, true, true));
  TEST_ASSERT_FALSE(q.poll(t0 + 90300 + RATE_QUERY_TIMEOUT_MS - 1, true, true));
  TEST_ASSERT_FALSE(q.poll(t0 + 90300 + RATE_QUERY_TIMEOUT_MS, true, true));  // timeout
  TEST_ASSERT_TRUE(q.known());
  TEST_ASSERT_EQUAL_UINT32(30000, q.rates().um_s[3]);
  TEST_ASSERT_EQUAL_UINT32(2, q.refresh_failures());
  TEST_ASSERT_TRUE(q.poll(t0 + 91300 + RATE_REFRESH_MS, true, true));
  TEST_ASSERT_EQUAL(RateLine::Valid, feed_line(q, kAnswer, t0 + 121400));
  TEST_ASSERT_EQUAL_UINT32(0, q.refresh_failures());
  TEST_ASSERT_EQUAL_UINT32(60000, q.rates().um_s[3]);
  q.reset();  // link loss: unknown again
  TEST_ASSERT_FALSE(q.known());
  TEST_ASSERT_EQUAL_UINT32(0, q.refresh_failures());
}

void test_99_rate_refresh() {
  run_refresh(0);
  run_refresh(0xFFFF0000u);  // overflow between the re-reads
  // The planner never changes the rates within a session: a re-read applies from the next session.
  MachineStatus st{};
  st.state = MachineState::Idle;
  JogPlanner p{z1_pro_rates()};
  TEST_ASSERT_EQUAL_STRING("$J X1.000 F0.092", p.tick(0, 10, 100, AxisMode::X, BlockReason::None, &st, 0).cmd.text);
  MachineRates slower = z1_pro_rates();
  slower.um_s[0] = 25000;
  p.set_rates(slower);
  TEST_ASSERT_EQUAL_UINT32(50000, p.rates().um_s[0]);
  p.tick(20, 10, 100, AxisMode::X, BlockReason::None, &st, 0);
  p.tick(40, 10, 100, AxisMode::X, BlockReason::None, &st, 0);
  // 20 mm/s turned: still 0.92 x 20 / 50 (with 25 mm/s it would be F0.736).
  TEST_ASSERT_EQUAL_STRING("$J X3.000 F0.368", p.tick(60, 10, 100, AxisMode::X, BlockReason::None, &st, 0).cmd.text);
  p.end_session(80);
  TEST_ASSERT_EQUAL_UINT32(25000, p.rates().um_s[0]);
  TEST_ASSERT_EQUAL_STRING("$J X1.000 F0.184", p.tick(400, 10, 100, AxisMode::X, BlockReason::None, &st, 0).cmd.text);
  // Without a session a change applies at once.
  p.end_session(420);
  p.set_rates(z1_pro_rates());
  TEST_ASSERT_EQUAL_UINT32(50000, p.rates().um_s[0]);
}

}  // namespace

void run_rates_tests() {
  RUN_TEST(test_93_feed_limits);
  RUN_TEST(test_94_parse_rate_line);
  RUN_TEST(test_95_rate_query);
  RUN_TEST(test_96_gate_and_planner_rates);
  RUN_TEST(test_97_real_m203_answer);
  RUN_TEST(test_98_rate_query_alarm);
  RUN_TEST(test_99_rate_refresh);
}
