// SPEC 11, tests 100-107: unlock after Alarm/HALT from the handwheel (ticket #41).
#include <unity.h>

#include <cstring>

#include "axis_select.h"
#include "core_config.h"
#include "detents.h"
#include "frame.h"
#include "jog_gate.h"
#include "jog_planner.h"
#include "machine_rates.h"
#include "step_policy.h"
#include "test_suites.h"
#include "text_lines.h"
#include "unlock.h"

namespace {

constexpr uint8_t BX = 1u << static_cast<uint8_t>(Button::X);
constexpr uint8_t BY = 1u << static_cast<uint8_t>(Button::Y);
constexpr uint8_t BZ = 1u << static_cast<uint8_t>(Button::Z);
constexpr uint8_t BA = 1u << static_cast<uint8_t>(Button::A);

// Machine in Alarm, fresh status, everything allowed, no button.
UnlockInputs alarm_in() {
  UnlockInputs in{};
  in.normal_mode = true;
  in.online = true;
  in.have_status = true;
  in.status.state = MachineState::Alarm;
  in.status.halted = true;
  in.status_age_ms = 20;
  in.ota_updating = false;
  in.hint_visible = true;
  return in;
}

// Press edge of `bit` at t0 (level pressed from t0 on).
UnlockTick press(UnlockRequest& u, uint32_t t, UnlockInputs in, uint8_t bit) {
  in.pressed = bit;
  in.edges = bit;
  return u.tick(t, in);
}

UnlockTick hold(UnlockRequest& u, uint32_t t, UnlockInputs in, uint8_t bit) {
  in.pressed = bit;
  in.edges = 0;
  return u.tick(t, in);
}

UnlockTick idle(UnlockRequest& u, uint32_t t, const UnlockInputs& in) { return u.tick(t, in); }

// Holds `bit` from t0 in 20 ms ticks up to t_end (inclusive), returns the number of sends.
int hold_until(UnlockRequest& u, uint32_t t0, uint32_t t_end, const UnlockInputs& in, uint8_t bit) {
  int sends = press(u, t0, in, bit).send ? 1 : 0;
  for (uint32_t t = t0 + 20; static_cast<uint32_t>(t_end - t) < 0x80000000u; t += 20) {
    if (hold(u, t, in, bit).send) ++sends;
  }
  return sends;
}

void run_hold_time(uint32_t t0) {
  const UnlockInputs in = alarm_in();
  UnlockRequest u;
  TEST_ASSERT_EQUAL(UnlockView::Hint, idle(u, t0 - 20, in).view);
  UnlockTick r = press(u, t0, in, BX);
  TEST_ASSERT_FALSE(r.send);
  TEST_ASSERT_EQUAL(UnlockView::Holding, r.view);
  TEST_ASSERT_EQUAL_UINT16(0, r.permille);
  r = hold(u, t0 + 2500, in, BX);
  TEST_ASSERT_EQUAL(UnlockView::Holding, r.view);
  TEST_ASSERT_EQUAL_UINT16(500, r.permille);
  r = hold(u, t0 + UNLOCK_HOLD_MS - 1, in, BX);  // < 5 s: nothing
  TEST_ASSERT_FALSE(r.send);
  TEST_ASSERT_EQUAL_UINT16(999, r.permille);
  r = hold(u, t0 + UNLOCK_HOLD_MS, in, BX);  // exactly 5 s: once
  TEST_ASSERT_TRUE(r.send);
  TEST_ASSERT_EQUAL(UnlockView::Sent, r.view);
  TEST_ASSERT_NULL(r.aborted);
  // Holding on: never again, "Entsperren..." while the machine still says Alarm, at most UNLOCK_SHOW_MS.
  for (uint32_t t = t0 + UNLOCK_HOLD_MS + 20; t != t0 + UNLOCK_HOLD_MS + 20000; t += 20) {
    r = hold(u, t, in, BX);
    TEST_ASSERT_FALSE(r.send);
    TEST_ASSERT_EQUAL(static_cast<uint32_t>(t - t0 - UNLOCK_HOLD_MS) < UNLOCK_SHOW_MS ? UnlockView::Sent
                                                                                      : UnlockView::Hint,
                      r.view);
  }
}

// 100. Hold < 5 s sends nothing; exactly 5 s sends once; holding on never sends again.
void test_100_unlock_hold_time() {
  TEST_ASSERT_EQUAL_STRING("$X", UNLOCK_TEXT);
  TEST_ASSERT_EQUAL_UINT32(5000, UNLOCK_HOLD_MS);
  run_hold_time(1000);
  run_hold_time(0xFFFFF000u);  // across the millis() wrap
  // Released at 4999 ms: nothing, and the time does not carry over to the next press.
  const UnlockInputs in = alarm_in();
  UnlockRequest u;
  TEST_ASSERT_EQUAL_INT(0, hold_until(u, 0, UNLOCK_HOLD_MS - 20, in, BY));
  UnlockTick r = idle(u, UNLOCK_HOLD_MS, in);
  TEST_ASSERT_EQUAL_STRING("losgelassen", r.aborted);
  TEST_ASSERT_EQUAL(UnlockView::Hint, r.view);
  TEST_ASSERT_FALSE(press(u, UNLOCK_HOLD_MS + 20, in, BY).send);
  TEST_ASSERT_FALSE(hold(u, UNLOCK_HOLD_MS + 20 + UNLOCK_HOLD_MS - 1, in, BY).send);
  TEST_ASSERT_TRUE(hold(u, UNLOCK_HOLD_MS + 20 + UNLOCK_HOLD_MS, in, BY).send);
}

// 101. Release and hold again sends again (one $X per hold, at most one per UNLOCK_MIN_INTERVAL_MS).
void test_101_unlock_again() {
  static_assert(UNLOCK_HOLD_MS >= UNLOCK_MIN_INTERVAL_MS, "a new hold always ends after the interval");
  const UnlockInputs in = alarm_in();
  UnlockRequest u;
  TEST_ASSERT_EQUAL_INT(1, hold_until(u, 0, 6000, in, BX));
  idle(u, 6020, in);  // released
  TEST_ASSERT_EQUAL(UnlockView::Hint, idle(u, 8000, in).view);
  // New press right after the "Entsperren..." phase: again after a full UNLOCK_HOLD_MS, never earlier.
  TEST_ASSERT_EQUAL_INT(0, hold_until(u, 8020, 8020 + UNLOCK_HOLD_MS - 20, in, BZ));
  const UnlockTick r = hold(u, 8020 + UNLOCK_HOLD_MS, in, BZ);
  TEST_ASSERT_TRUE(r.send);
  const uint32_t first_send = UNLOCK_HOLD_MS;  // pressed at 0
  TEST_ASSERT_TRUE((8020 + UNLOCK_HOLD_MS) - first_send >= UNLOCK_MIN_INTERVAL_MS);
  // While "Entsperren..." is shown, a press does not start a hold.
  UnlockRequest v;
  TEST_ASSERT_EQUAL_INT(1, hold_until(v, 0, UNLOCK_HOLD_MS, in, BX));
  idle(v, UNLOCK_HOLD_MS + 20, in);
  TEST_ASSERT_EQUAL(UnlockView::Sent, press(v, UNLOCK_HOLD_MS + 40, in, BY).view);
  for (uint32_t t = UNLOCK_HOLD_MS + 60; t <= 3 * UNLOCK_HOLD_MS; t += 20) TEST_ASSERT_FALSE(hold(v, t, in, BY).send);
  // Button A counts like X, Y, Z when main.cpp passes it (axis A enabled).
  UnlockRequest w;
  TEST_ASSERT_EQUAL_INT(1, hold_until(w, 0, UNLOCK_HOLD_MS, in, BA));
}

// 102. A button pressed before the alarm (level only, no edge) never counts; release and a new press do. A press
// while the hint is not visible (alarm line has priority, screensaver swallowed the edge) does not count either.
void test_102_unlock_needs_new_press() {
  UnlockInputs in = alarm_in();
  in.status.state = MachineState::Idle;
  in.status.halted = false;
  UnlockRequest u;
  press(u, 0, in, BX);  // pressed while Idle (selects the axis, no unlock)
  in = alarm_in();
  for (uint32_t t = 20; t <= 3 * UNLOCK_HOLD_MS; t += 20) {
    const UnlockTick r = hold(u, t, in, BX);
    TEST_ASSERT_FALSE(r.send);
    TEST_ASSERT_EQUAL(UnlockView::Hint, r.view);
  }
  idle(u, 3 * UNLOCK_HOLD_MS + 20, in);  // released
  TEST_ASSERT_EQUAL_INT(1, hold_until(u, 3 * UNLOCK_HOLD_MS + 40, 4 * UNLOCK_HOLD_MS + 40, in, BX));

  UnlockRequest v;
  UnlockInputs hidden = alarm_in();
  hidden.hint_visible = false;  // e.g. the alarm line is shown for ALARM_SHOW_MS
  press(v, 0, hidden, BX);
  for (uint32_t t = 20; t <= 2 * UNLOCK_HOLD_MS; t += 20) TEST_ASSERT_FALSE(hold(v, t, alarm_in(), BX).send);
  // Screensaver: main.cpp passes edges = 0 for a swallowed press; the level alone never starts.
  UnlockRequest w;
  UnlockInputs swallowed = alarm_in();
  swallowed.pressed = BX;
  swallowed.edges = 0;
  for (uint32_t t = 0; t <= 2 * UNLOCK_HOLD_MS; t += 20) TEST_ASSERT_FALSE(w.tick(t, swallowed).send);
}

void expect_blocked(UnlockInputs in, const char* why) {
  TEST_ASSERT_EQUAL_STRING(why, UnlockRequest::blocked(in));
  UnlockRequest u;
  UnlockTick r = press(u, 0, in, BX);
  TEST_ASSERT_EQUAL(UnlockView::None, r.view);
  for (uint32_t t = 20; t <= 2 * UNLOCK_HOLD_MS; t += 20) {
    r = hold(u, t, in, BX);
    TEST_ASSERT_FALSE(r.send);
    TEST_ASSERT_EQUAL(UnlockView::None, r.view);
  }
}

// 103. No send without all conditions: offline, no status, stale status, no alarm, OTA, other AppMode.
void test_103_unlock_conditions() {
  UnlockInputs in = alarm_in();
  TEST_ASSERT_NULL(UnlockRequest::blocked(in));
  in.status_age_ms = SEND_MAX_AGE_MS;  // same freshness limit as the gate (8.5 rule 4)
  TEST_ASSERT_NULL(UnlockRequest::blocked(in));
  in = alarm_in();
  in.status.halted = false;  // state Alarm alone
  TEST_ASSERT_NULL(UnlockRequest::blocked(in));
  in = alarm_in();
  in.status.state = MachineState::Idle;  // halted alone (H: field)
  TEST_ASSERT_NULL(UnlockRequest::blocked(in));

  in = alarm_in();
  in.online = false;
  expect_blocked(in, "keine Verbindung");
  in = alarm_in();
  in.have_status = false;
  expect_blocked(in, "kein frischer Status");
  in = alarm_in();
  in.status_age_ms = SEND_MAX_AGE_MS + 1;
  expect_blocked(in, "kein frischer Status");
  in = alarm_in();
  in.status.state = MachineState::Idle;
  in.status.halted = false;
  expect_blocked(in, "kein Alarm");
  in.status.state = MachineState::Run;
  expect_blocked(in, "kein Alarm");
  in = alarm_in();
  in.ota_updating = true;
  expect_blocked(in, "OTA");
  in = alarm_in();
  in.normal_mode = false;
  expect_blocked(in, "Betriebsart");
}

// 104. A condition breaks during the hold: aborted with the reason, the time starts over only with a new press.
// (A stale status alone does not abort: test 108. A new alarm/error line: test 109.)
void test_104_unlock_condition_breaks() {
  struct Case {
    void (*breaks)(UnlockInputs&);
    const char* why;
  };
  static const Case kCases[] = {
      {[](UnlockInputs& i) { i.online = false; }, "keine Verbindung"},
      {[](UnlockInputs& i) { i.have_status = false; }, "kein Status"},
      {[](UnlockInputs& i) { i.status.state = MachineState::Idle; i.status.halted = false; }, "kein Alarm"},
      {[](UnlockInputs& i) { i.ota_updating = true; }, "OTA"},
      {[](UnlockInputs& i) { i.normal_mode = false; }, "Betriebsart"},
      {[](UnlockInputs& i) { i.new_message = true; }, "neue Meldung"},
  };
  for (const Case& c : kCases) {
    const UnlockInputs good = alarm_in();
    UnlockInputs bad = good;
    c.breaks(bad);
    UnlockRequest u;
    TEST_ASSERT_EQUAL_INT(0, hold_until(u, 0, 4000, good, BX));
    const UnlockTick r = hold(u, 4020, bad, BX);  // one tick without the condition
    TEST_ASSERT_EQUAL_STRING(c.why, r.aborted);
    TEST_ASSERT_FALSE(r.send);
    // Condition back, button still held: no send, also long after 5 s from the first press.
    for (uint32_t t = 4040; t <= 4040 + 2 * UNLOCK_HOLD_MS; t += 20) {
      const UnlockTick k = hold(u, t, good, BX);
      TEST_ASSERT_FALSE(k.send);
      TEST_ASSERT_NULL(k.aborted);
    }
    idle(u, 20000, good);
    TEST_ASSERT_EQUAL_INT(1, hold_until(u, 20020, 20020 + UNLOCK_HOLD_MS, good, BX));
  }
  // The hold belongs to its button: X released while Y is held -> aborted.
  const UnlockInputs in = alarm_in();
  UnlockRequest u;
  press(u, 0, in, BX);
  hold(u, 1000, in, BX | BY);
  const UnlockTick r = hold(u, 1020, in, BY);
  TEST_ASSERT_EQUAL_STRING("losgelassen", r.aborted);
  for (uint32_t t = 1040; t <= 2 * UNLOCK_HOLD_MS; t += 20) TEST_ASSERT_FALSE(hold(u, t, in, BY).send);
}

// 105. "Entsperren..." ends with the first status without Alarm, at the latest after UNLOCK_SHOW_MS, or on link loss.
void test_105_unlock_sent_view() {
  UnlockInputs in = alarm_in();
  UnlockRequest u;
  TEST_ASSERT_EQUAL_INT(1, hold_until(u, 0, UNLOCK_HOLD_MS, in, BX));
  TEST_ASSERT_EQUAL(UnlockView::Sent, idle(u, UNLOCK_HOLD_MS + 100, in).view);
  in.status.state = MachineState::Idle;
  in.status.halted = false;
  TEST_ASSERT_EQUAL(UnlockView::None, idle(u, UNLOCK_HOLD_MS + 120, in).view);  // unlocked: back to the gate text
  UnlockRequest v;
  in = alarm_in();
  TEST_ASSERT_EQUAL_INT(1, hold_until(v, 0, UNLOCK_HOLD_MS, in, BX));
  TEST_ASSERT_EQUAL(UnlockView::Sent, idle(v, UNLOCK_HOLD_MS + UNLOCK_SHOW_MS - 1, in).view);
  TEST_ASSERT_EQUAL(UnlockView::Hint, idle(v, UNLOCK_HOLD_MS + UNLOCK_SHOW_MS, in).view);  // still Alarm
  UnlockRequest w;
  TEST_ASSERT_EQUAL_INT(1, hold_until(w, 0, UNLOCK_HOLD_MS, in, BX));
  in.online = false;
  TEST_ASSERT_EQUAL(UnlockView::None, idle(w, UNLOCK_HOLD_MS + 20, in).view);
}

// Main loop steps 1-7 of SPEC 8.7/8.10 for one tick, as main.cpp does them (axis fixed to X).
struct Loop {
  DetentAccumulator acc{COUNTS_PER_DETENT};
  StepPolicy step;
  JogPlanner planner{z1_pro_rates()};
  UnlockRequest unlock;
  MachineStatus st{};
  uint32_t rx_ms = 0;  // receive time of st
  int commands = 0;
  int unlocks = 0;
  PlannerTickResult last{};

  void status(uint32_t t, MachineState s, bool halted) {
    st.state = s;
    st.halted = halted;
    rx_ms = t;
  }
  void tick(uint32_t now, int32_t counts, uint8_t pressed, uint8_t edges) {
    const int32_t det = acc.take(now, counts);
    const int32_t step_um = step.update(now, static_cast<uint32_t>(det < 0 ? -det : det));
    const uint32_t age = now - rx_ms;
    GateInputs g{};
    g.wifi_ok = g.tcp_ok = g.have_status = true;
    g.status = st;
    g.status_age_ms = age;
    g.axis = AxisMode::X;
    g.rates_known = true;
    g.session_active = planner.session_active();
    g.session_consistent = planner.session_consistent(st);
    const BlockReason gate = evaluate_gate(g);
    last = planner.tick(now, det, step_um, AxisMode::X, gate, &st, age);
    if (gate != BlockReason::None) acc.clear();
    if (last.cmd.valid) ++commands;
    UnlockInputs in{};
    in.normal_mode = in.online = in.have_status = in.hint_visible = true;
    in.status = st;
    in.status_age_ms = age;
    in.pressed = pressed;
    in.edges = edges;
    if (unlock.tick(now, in).send) {
      ++unlocks;
      planner.end_session(now);  // as main.cpp: a session may start only on a status received after $X
      acc.clear();
    }
  }
};

// 106. Nothing moves by itself after the unlock: detents turned during the alarm and during the hold are dropped;
// without new turning no command; a new session starts only on an Idle status received START_GAP_MS after $X.
void test_106_unlock_no_motion() {
  Loop l;
  l.status(0, MachineState::Idle, false);
  l.tick(20, 0, 0, 0);
  l.status(100, MachineState::Alarm, true);
  // Alarm: the wheel is turned while the status is refreshed, then the button is held 5 s and turned on.
  uint32_t t = 120;
  for (; t < 1000; t += 20) {
    if (t % 100 == 0) l.status(t, MachineState::Alarm, true);
    l.tick(t, 4 * COUNTS_PER_DETENT + 1, 0, 0);
  }
  const uint32_t pressed_at = t;
  for (; l.unlocks == 0; t += 20) {
    if (t % 100 == 0) l.status(t, MachineState::Alarm, true);
    l.tick(t, 4 * COUNTS_PER_DETENT + 1, BX, t == pressed_at ? BX : 0);
    TEST_ASSERT_TRUE(t - pressed_at <= UNLOCK_HOLD_MS);
  }
  const uint32_t sent = t - 20;
  TEST_ASSERT_EQUAL_UINT32(pressed_at + UNLOCK_HOLD_MS, sent);
  TEST_ASSERT_EQUAL_INT(0, l.commands);
  TEST_ASSERT_EQUAL_INT32(0, l.acc.remainder());
  // The machine answers with Idle (position reset from the motors). No turning: no command, ever.
  for (; t < sent + 5000; t += 20) {
    if (t % 100 == 0) l.status(t, MachineState::Idle, false);
    l.tick(t, 0, BX, 0);
  }
  TEST_ASSERT_EQUAL_INT(0, l.commands);
  TEST_ASSERT_FALSE(l.planner.session_active());
  TEST_ASSERT_EQUAL_INT(1, l.unlocks);

  // Turning right after $X on a stale Idle (received before START_GAP_MS after the send): no session.
  Loop m;
  m.status(0, MachineState::Alarm, true);
  t = 20;
  m.tick(t, 0, BX, BX);
  for (t = 40; m.unlocks == 0; t += 20) {
    if (t % 100 == 0) m.status(t, MachineState::Alarm, true);
    m.tick(t, 0, BX, 0);
  }
  const uint32_t s2 = t - 20;
  m.status(s2 + 50, MachineState::Idle, false);  // first Idle 50 ms after $X
  for (t = s2 + 60; t < s2 + START_GAP_MS; t += 20) {
    m.tick(t, t == s2 + 60 ? COUNTS_PER_DETENT : 0, 0, 0);  // one detent right after the unlock
    TEST_ASSERT_FALSE(m.last.cmd.valid);
  }
  TEST_ASSERT_FALSE(m.planner.session_active());
  // A status received START_GAP_MS after $X: the new turning (not an old one) starts a session.
  m.status(s2 + START_GAP_MS, MachineState::Idle, false);
  m.tick(s2 + START_GAP_MS + 20, COUNTS_PER_DETENT, 0, 0);
  TEST_ASSERT_TRUE(m.last.cmd.valid);
  TEST_ASSERT_EQUAL_STRING("$J X0.010 F0.008", m.last.cmd.text);
}

// 107. The firmware's answer to $X ("[Caution: Unlocked]\nok\n", one printf = one 0x90 frame, SimpleShell.cpp
// Z. 350) is no rate answer for RateQuery in any phase and no alarm/error line; $X goes out as 0xA2 without '\n'.
void test_107_unlock_answer_lines() {
  uint8_t out[32];
  const size_t n = frame::build(frame::TYPE_CONSOLE, reinterpret_cast<const uint8_t*>(UNLOCK_TEXT),
                                std::strlen(UNLOCK_TEXT), out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT32(2 + frame::OVERHEAD, n);
  TEST_ASSERT_EQUAL_UINT8(0xA2, out[4]);
  TEST_ASSERT_EQUAL_UINT8('$', out[5]);
  TEST_ASSERT_EQUAL_UINT8('X', out[6]);

  const char* answer = "[Caution: Unlocked]\nok\n";
  uint8_t stream[64];
  const size_t len = frame::build(frame::TYPE_TEXT, reinterpret_cast<const uint8_t*>(answer), std::strlen(answer),
                                  stream, sizeof(stream));
  const char* kRates = " X: 50  Y: 50  Z: 16.6667  A: 30 ";
  for (int phase = 0; phase < 4; ++phase) {
    RateQuery q;
    q.reset();
    if (phase >= 1) TEST_ASSERT_TRUE(q.poll(0, true));  // 1: in flight
    if (phase >= 2) {
      TEST_ASSERT_EQUAL(RateLine::Valid, q.on_line(kRates, std::strlen(kRates), 10));  // 2: known
    }
    if (phase == 3) TEST_ASSERT_TRUE(q.poll(10 + RATE_REFRESH_MS, true, true));  // 3: re-read in flight
    const uint32_t queries = q.queries();
    frame::Decoder dec;
    TextLines lines;
    int n_lines = 0;
    dec.feed(stream, len, [&](const frame::Frame& f) {
      lines.feed(f.payload, f.n, [&](const char* line, size_t k) {
        ++n_lines;
        TEST_ASSERT_EQUAL(RateLine::NotRates, q.on_line(line, k, 20 + RATE_REFRESH_MS));
        TEST_ASSERT_FALSE(std::strncmp(line, "ALARM", 5) == 0 || std::strncmp(line, "error", 5) == 0);
      });
    });
    TEST_ASSERT_EQUAL_INT(2, n_lines);
    TEST_ASSERT_EQUAL_UINT32(queries, q.queries());
    TEST_ASSERT_EQUAL_UINT32(0, q.refresh_failures());
    TEST_ASSERT_EQUAL(phase >= 2, q.known());
    if (phase >= 2) TEST_ASSERT_EQUAL_UINT32(30000, q.rates().um_s[3]);
    if (phase == 1) {
      // Still in flight: the timeout and the retry schedule are unchanged.
      TEST_ASSERT_FALSE(q.poll(999, true));
      TEST_ASSERT_FALSE(q.poll(1000, true));
      TEST_ASSERT_TRUE(q.poll(2000, true));
    }
    if (phase == 3) {
      // Re-read still in flight: a valid answer after the $X answer is taken.
      TEST_ASSERT_EQUAL(RateLine::Valid, q.on_line(kRates, std::strlen(kRates), 30 + RATE_REFRESH_MS));
      TEST_ASSERT_EQUAL_UINT32(0, q.refresh_failures());
    }
  }
}

UnlockInputs stale(UnlockInputs in) {
  in.status_age_ms = SEND_MAX_AGE_MS + 1;
  return in;
}

// 108. Status gaps during the hold (WiFi stall): a stale status does not abort; the send needs a fresh status with
// Alarm. Stale at 5 s -> waits and sends with the next fresh Alarm status (button still held). A fresh status
// without Alarm aborts, a stale one without Alarm does not (but never sends).
void test_108_unlock_status_gap() {
  const UnlockInputs good = alarm_in();
  {
    UnlockRequest u;  // gap in the middle
    press(u, 0, good, BX);
    for (uint32_t t = 20; t < UNLOCK_HOLD_MS; t += 20) {
      const UnlockTick r = hold(u, t, (t >= 2000 && t < 2600) ? stale(good) : good, BX);
      TEST_ASSERT_FALSE(r.send);
      TEST_ASSERT_NULL(r.aborted);
      TEST_ASSERT_EQUAL(UnlockView::Holding, r.view);
    }
    TEST_ASSERT_TRUE(hold(u, UNLOCK_HOLD_MS, good, BX).send);
  }
  {
    UnlockRequest u;  // stale when the 5 s are reached: wait, then send with the next fresh Alarm status
    press(u, 0, good, BX);
    for (uint32_t t = 20; t < 4800; t += 20) hold(u, t, good, BX);
    for (uint32_t t = 4800; t < 5600; t += 20) {
      const UnlockTick r = hold(u, t, stale(good), BX);
      TEST_ASSERT_FALSE(r.send);
      TEST_ASSERT_NULL(r.aborted);
      TEST_ASSERT_EQUAL(UnlockView::Holding, r.view);
      if (t >= UNLOCK_HOLD_MS) TEST_ASSERT_EQUAL_UINT16(999, r.permille);  // full only with the send
    }
    TEST_ASSERT_TRUE(hold(u, 5600, good, BX).send);
  }
  {
    UnlockRequest u;  // waiting on a stale status, then the link is lost or a new alarm/error line arrives: aborted
    for (int k = 0; k < 2; ++k) {
      press(u, 0, good, BX);
      for (uint32_t t = 20; t < 5400; t += 20) TEST_ASSERT_FALSE(hold(u, t, t >= 4800 ? stale(good) : good, BX).send);
      UnlockInputs bad = stale(good);
      if (k == 0) bad.online = false;
      if (k == 1) bad.new_message = true;
      const UnlockTick r = hold(u, 5400, bad, BX);
      TEST_ASSERT_FALSE(r.send);
      TEST_ASSERT_EQUAL_STRING(k == 0 ? "keine Verbindung" : "neue Meldung", r.aborted);
      for (uint32_t t = 5420; t < 12000; t += 20) TEST_ASSERT_FALSE(hold(u, t, good, BX).send);  // fresh again
      idle(u, 12000, good);
      u = UnlockRequest{};
    }
  }
  {
    UnlockRequest u;  // released while waiting on a stale status: nothing
    press(u, 0, good, BX);
    for (uint32_t t = 20; t < 5200; t += 20) hold(u, t, t >= 4800 ? stale(good) : good, BX);
    TEST_ASSERT_EQUAL_STRING("losgelassen", idle(u, 5200, stale(good)).aborted);
    for (uint32_t t = 5220; t < 8000; t += 20) TEST_ASSERT_FALSE(hold(u, t, good, BX).send);
  }
  {
    UnlockRequest u;  // the machine left Alarm: stale Idle does not abort and never sends, fresh Idle aborts
    UnlockInputs idle_in = good;
    idle_in.status.state = MachineState::Idle;
    idle_in.status.halted = false;
    press(u, 0, good, BX);
    for (uint32_t t = 20; t < 4000; t += 20) hold(u, t, good, BX);
    for (uint32_t t = 4000; t < 6000; t += 20) {
      const UnlockTick r = hold(u, t, stale(idle_in), BX);
      TEST_ASSERT_FALSE(r.send);
      TEST_ASSERT_NULL(r.aborted);
    }
    const UnlockTick r = hold(u, 6000, idle_in, BX);
    TEST_ASSERT_FALSE(r.send);
    TEST_ASSERT_EQUAL_STRING("kein Alarm", r.aborted);
  }
  {
    UnlockRequest u;  // a stale status never starts a hold (blocked at the edge)
    press(u, 0, stale(good), BX);
    for (uint32_t t = 20; t <= 2 * UNLOCK_HOLD_MS; t += 20) TEST_ASSERT_FALSE(hold(u, t, good, BX).send);
  }
}

// 109. A new alarm/error line during the hold aborts it ("neue Meldung"); only a new press counts again.
void test_109_unlock_new_message() {
  const UnlockInputs good = alarm_in();
  UnlockInputs msg = good;
  msg.new_message = true;
  UnlockRequest u;
  press(u, 0, good, BX);
  for (uint32_t t = 20; t < 3000; t += 20) hold(u, t, good, BX);
  UnlockTick r = hold(u, 3000, msg, BX);
  TEST_ASSERT_EQUAL_STRING("neue Meldung", r.aborted);
  TEST_ASSERT_FALSE(r.send);
  for (uint32_t t = 3020; t <= 3000 + 2 * UNLOCK_HOLD_MS; t += 20) TEST_ASSERT_FALSE(hold(u, t, good, BX).send);
  idle(u, 14000, good);
  TEST_ASSERT_EQUAL_INT(1, hold_until(u, 14020, 14020 + UNLOCK_HOLD_MS, good, BX));
  // Also in the very tick the 5 s are reached: aborted, not sent.
  UnlockRequest v;
  TEST_ASSERT_EQUAL_INT(0, hold_until(v, 0, UNLOCK_HOLD_MS - 20, good, BX));
  r = hold(v, UNLOCK_HOLD_MS, msg, BX);
  TEST_ASSERT_FALSE(r.send);
  TEST_ASSERT_EQUAL_STRING("neue Meldung", r.aborted);
}

}  // namespace

void run_unlock_tests() {
  RUN_TEST(test_100_unlock_hold_time);
  RUN_TEST(test_101_unlock_again);
  RUN_TEST(test_102_unlock_needs_new_press);
  RUN_TEST(test_103_unlock_conditions);
  RUN_TEST(test_104_unlock_condition_breaks);
  RUN_TEST(test_105_unlock_sent_view);
  RUN_TEST(test_106_unlock_no_motion);
  RUN_TEST(test_107_unlock_answer_lines);
  RUN_TEST(test_108_unlock_status_gap);
  RUN_TEST(test_109_unlock_new_message);
}
