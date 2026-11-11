// SPEC 11, tests 68-74: screensaver against OLED burn-in (ticket #28).
#include <unity.h>

#include "core_config.h"
#include "screensaver.h"
#include "test_suites.h"

namespace {

constexpr bool IN = true;
constexpr bool NO = false;

// Shorthands: press edge only, counts only, nothing.
ScreenTick press(ScreenSaver& s, uint32_t t, bool busy = false) { return s.feed(t, IN, 0, busy, NO); }
ScreenTick turn(ScreenSaver& s, uint32_t t, int32_t c) { return s.feed(t, NO, c, NO, NO); }
ScreenTick idle(ScreenSaver& s, uint32_t t) { return s.feed(t, NO, 0, NO, NO); }

void check_timing(uint32_t t0) {
  ScreenSaver s;
  ScreenTick r = press(s, t0);  // last input at t0
  TEST_ASSERT_EQUAL(ScreenState::On, r.state);
  TEST_ASSERT_FALSE(r.swallow);
  TEST_ASSERT_EQUAL(ScreenState::On, idle(s, t0 + SCREENSAVER_MS - 1).state);
  r = idle(s, t0 + SCREENSAVER_MS);
  TEST_ASSERT_EQUAL(ScreenState::Saver, r.state);
  TEST_ASSERT_TRUE(r.move);  // first logo position on entering
  TEST_ASSERT_FALSE(idle(s, t0 + SCREENSAVER_MS + SAVER_MOVE_MS - 1).move);
  r = idle(s, t0 + SCREENSAVER_MS + SAVER_MOVE_MS);
  TEST_ASSERT_EQUAL(ScreenState::Saver, r.state);
  TEST_ASSERT_TRUE(r.move);
  TEST_ASSERT_EQUAL(ScreenState::Saver, idle(s, t0 + DISPLAY_OFF_MS - 1).state);
  r = idle(s, t0 + DISPLAY_OFF_MS);
  TEST_ASSERT_EQUAL(ScreenState::Off, r.state);
  TEST_ASSERT_FALSE(r.move);
}

// Instance asleep: Saver (i = 0) or Off (i = 1), last input at 0. Returns the time it fell asleep.
uint32_t asleep(ScreenSaver& s, int i) {
  const uint32_t at = i == 0 ? SCREENSAVER_MS : DISPLAY_OFF_MS;
  press(s, 0);
  TEST_ASSERT_EQUAL(i == 0 ? ScreenState::Saver : ScreenState::Off, idle(s, at).state);
  return at;
}

// 68. Timing: On until SCREENSAVER_MS, Saver (logo moves every SAVER_MOVE_MS) until DISPLAY_OFF_MS, then Off.
//     Same results across the millis() wrap. Off stays Off even when the idle time wraps after 49.7 days.
void test_68_saver_timing() {
  check_timing(0);
  check_timing(1000);
  check_timing(0xFFFFFFFFu - SCREENSAVER_MS + 1);  // wrap between input and Saver
  check_timing(0xFFFFFFFFu - DISPLAY_OFF_MS + 1);  // wrap between Saver and Off

  ScreenSaver s;
  press(s, 0);
  TEST_ASSERT_EQUAL(ScreenState::Off, idle(s, DISPLAY_OFF_MS).state);
  TEST_ASSERT_EQUAL(ScreenState::Off, idle(s, 0xFFFFFFFFu).state);
  TEST_ASSERT_EQUAL(ScreenState::Off, idle(s, 100).state);  // idle time wrapped to 100 ms

  // A fresh instance starts its idle timer with the first feed.
  ScreenSaver f;
  TEST_ASSERT_EQUAL(ScreenState::On, idle(f, 5000).state);
  TEST_ASSERT_EQUAL(ScreenState::On, idle(f, 5000 + SCREENSAVER_MS - 1).state);
  TEST_ASSERT_EQUAL(ScreenState::Saver, idle(f, 5000 + SCREENSAVER_MS).state);

  // Moves only every SAVER_MOVE_MS over a run of real ticks.
  ScreenSaver m;
  press(m, 0);
  int moves = 0;
  for (uint32_t t = TICK_MS; t <= SCREENSAVER_MS + 3 * SAVER_MOVE_MS; t += TICK_MS) {
    if (idle(m, t).move) ++moves;
  }
  TEST_ASSERT_EQUAL_INT(4, moves);

  // While On, a single count is input and restarts the timer.
  ScreenSaver c;
  press(c, 0);
  turn(c, SCREENSAVER_MS - 1, 1);
  TEST_ASSERT_EQUAL(ScreenState::On, idle(c, 2 * SCREENSAVER_MS - 2).state);
  TEST_ASSERT_EQUAL(ScreenState::Saver, idle(c, 2 * SCREENSAVER_MS - 1).state);
}

// 69. Never Saver/Off while busy (axis selected or jog session); the idle timer still runs from the last input.
void test_69_no_saver_while_busy() {
  ScreenSaver s;
  press(s, 0);
  for (uint32_t t = TICK_MS; t <= 2 * DISPLAY_OFF_MS; t += TICK_MS) {
    const ScreenTick r = s.feed(t, NO, 0, IN, NO);
    TEST_ASSERT_EQUAL(ScreenState::On, r.state);
    TEST_ASSERT_FALSE(r.move);
    TEST_ASSERT_FALSE(r.swallow);
  }
  // Busy ends (e.g. axis auto-clear): idle since the last input is long over -> Off right away.
  TEST_ASSERT_EQUAL(ScreenState::Off, idle(s, 2 * DISPLAY_OFF_MS + TICK_MS).state);

  // Input while busy is never swallowed.
  ScreenSaver b;
  press(b, 0);
  const ScreenTick r = press(b, SCREENSAVER_MS + 1, IN);
  TEST_ASSERT_EQUAL(ScreenState::On, r.state);
  TEST_ASSERT_FALSE(r.swallow);
  // Busy appearing while Saver (not reachable in the firmware) wakes without swallowing.
  ScreenSaver w;
  asleep(w, 0);
  const ScreenTick rb = w.feed(SCREENSAVER_MS + TICK_MS, IN, 0, IN, NO);
  TEST_ASSERT_EQUAL(ScreenState::On, rb.state);
  TEST_ASSERT_FALSE(rb.swallow);
}

// 70. A press in Saver or Off wakes; everything is swallowed in the waking tick and up to WAKE_SWALLOW_MS
//     after it (a second button pressed to wake does nothing either), then inputs act normally.
void test_70_wake_swallows_input() {
  for (int i = 0; i < 2; ++i) {
    ScreenSaver s;
    const uint32_t w = asleep(s, i) + 1000;
    ScreenTick r = press(s, w);
    TEST_ASSERT_EQUAL(ScreenState::On, r.state);
    TEST_ASSERT_TRUE(r.woke);
    TEST_ASSERT_TRUE(r.swallow);
    TEST_ASSERT_FALSE(r.move);
    r = press(s, w + TICK_MS);  // second button 20 ms later: still swallowed
    TEST_ASSERT_TRUE(r.swallow);
    TEST_ASSERT_FALSE(r.woke);
    TEST_ASSERT_TRUE(turn(s, w + WAKE_SWALLOW_MS - 1, 4).swallow);
    r = press(s, w + WAKE_SWALLOW_MS);  // window over
    TEST_ASSERT_EQUAL(ScreenState::On, r.state);
    TEST_ASSERT_FALSE(r.swallow);
    TEST_ASSERT_FALSE(turn(s, w + WAKE_SWALLOW_MS + TICK_MS, 4).swallow);
    // Idle timer restarted by the last input.
    const uint32_t last = w + WAKE_SWALLOW_MS + TICK_MS;
    TEST_ASSERT_EQUAL(ScreenState::On, idle(s, last + SCREENSAVER_MS - 1).state);
    TEST_ASSERT_EQUAL(ScreenState::Saver, idle(s, last + SCREENSAVER_MS).state);
  }

  // Input while On: never swallowed.
  ScreenSaver o;
  press(o, 0);
  TEST_ASSERT_FALSE(press(o, SCREENSAVER_MS - 1).swallow);

  // Input and attention together in Saver: the input woke the display -> swallowed, window open.
  ScreenSaver a;
  asleep(a, 0);
  const ScreenTick r = a.feed(SCREENSAVER_MS + TICK_MS, IN, 0, NO, IN);
  TEST_ASSERT_TRUE(r.woke);
  TEST_ASSERT_TRUE(r.swallow);
  TEST_ASSERT_TRUE(press(a, SCREENSAVER_MS + 2 * TICK_MS).swallow);
}

// 71. Attention wakes from Saver and Off without a wake window: the next input acts normally.
void test_71_attention_wakes() {
  for (int i = 0; i < 2; ++i) {
    ScreenSaver s;
    const uint32_t t = asleep(s, i) + 500;
    ScreenTick r = s.feed(t, NO, 0, NO, IN);
    TEST_ASSERT_EQUAL(ScreenState::On, r.state);
    TEST_ASSERT_FALSE(r.woke);
    r = press(s, t + TICK_MS);
    TEST_ASSERT_FALSE(r.swallow);
    TEST_ASSERT_EQUAL(ScreenState::On, r.state);
  }
  // Attention while On keeps the display on for another SCREENSAVER_MS.
  ScreenSaver o;
  press(o, 0);
  o.feed(SCREENSAVER_MS - 10, NO, 0, NO, IN);
  TEST_ASSERT_EQUAL(ScreenState::On, idle(o, 2 * SCREENSAVER_MS - 11).state);
  TEST_ASSERT_EQUAL(ScreenState::Saver, idle(o, 2 * SCREENSAVER_MS - 10).state);
}

// 72. Wake by turning: from Off, a turn over ten ticks wakes in the first tick; ticks inside WAKE_SWALLOW_MS
//     are swallowed, the rest act normally.
void test_72_wake_by_turn() {
  ScreenSaver s;
  const uint32_t w = asleep(s, 1) + 60;
  int swallowed = 0;
  int woke = 0;
  for (uint32_t t = w; t < w + 10 * TICK_MS; t += TICK_MS) {
    const ScreenTick r = turn(s, t, 4);
    TEST_ASSERT_EQUAL(ScreenState::On, r.state);
    if (r.swallow) ++swallowed;
    if (r.woke) ++woke;
  }
  TEST_ASSERT_EQUAL_INT(1, woke);
  TEST_ASSERT_EQUAL_INT((WAKE_SWALLOW_MS + TICK_MS - 1) / TICK_MS, swallowed);  // 150 ms -> 8 ticks
}

// 73. Encoder noise: a single count while asleep neither wakes nor restarts the timer, and is swallowed.
//     WAKE_MIN_COUNTS counts within STEP_RESET_MS wake (also summed over ticks, either direction);
//     +1/-1 jitter and isolated counts further apart than STEP_RESET_MS never wake.
void test_73_noise_does_not_wake() {
  for (int i = 0; i < 2; ++i) {
    ScreenSaver s;
    const uint32_t t = asleep(s, i) + 1000;
    ScreenTick r = turn(s, t, 1);
    TEST_ASSERT_TRUE(r.state != ScreenState::On);
    TEST_ASSERT_TRUE(r.swallow);
    TEST_ASSERT_FALSE(r.woke);
    r = turn(s, t + TICK_MS, -1);  // jitter
    TEST_ASSERT_TRUE(r.state != ScreenState::On);
    r = turn(s, t + 2 * TICK_MS, 1);
    TEST_ASSERT_TRUE(r.state != ScreenState::On);
    r = turn(s, t + 3 * TICK_MS, 1);  // sum 2 within STEP_RESET_MS -> wakes
    TEST_ASSERT_EQUAL(ScreenState::On, r.state);
    TEST_ASSERT_TRUE(r.woke);
    TEST_ASSERT_TRUE(r.swallow);
  }
  // Isolated single counts, each STEP_RESET_MS apart: never wake, Saver goes on to Off.
  ScreenSaver g;
  asleep(g, 0);
  for (uint32_t t = SCREENSAVER_MS + 100; t < DISPLAY_OFF_MS; t += STEP_RESET_MS) {
    TEST_ASSERT_FALSE(turn(g, t, 1).woke);
  }
  TEST_ASSERT_EQUAL(ScreenState::Off, idle(g, DISPLAY_OFF_MS).state);
  // WAKE_MIN_COUNTS in one tick, negative direction.
  ScreenSaver n;
  asleep(n, 1);
  TEST_ASSERT_TRUE(turn(n, DISPLAY_OFF_MS + 20, -WAKE_MIN_COUNTS).woke);
  // A sub-threshold count before falling asleep does not carry over.
  ScreenSaver c;
  press(c, 0);
  turn(c, 10, 1);  // On: input, timer restarts at 10
  TEST_ASSERT_EQUAL(ScreenState::Saver, idle(c, 10 + SCREENSAVER_MS).state);
  TEST_ASSERT_FALSE(turn(c, 10 + SCREENSAVER_MS + 20, 1).woke);
}

// 74. Swallowed while asleep: ticks with no wake (no input) are flagged swallow, so a sub-threshold count
//     never reaches the accumulator. Busy is never swallowed.
void test_74_swallow_while_asleep() {
  ScreenSaver s;
  asleep(s, 0);
  TEST_ASSERT_TRUE(idle(s, SCREENSAVER_MS + 20).swallow);
  TEST_ASSERT_FALSE(s.feed(SCREENSAVER_MS + 40, NO, 1, IN, NO).swallow);
}

}  // namespace

void run_screensaver_tests() {
  RUN_TEST(test_68_saver_timing);
  RUN_TEST(test_69_no_saver_while_busy);
  RUN_TEST(test_70_wake_swallows_input);
  RUN_TEST(test_71_attention_wakes);
  RUN_TEST(test_72_wake_by_turn);
  RUN_TEST(test_73_noise_does_not_wake);
  RUN_TEST(test_74_swallow_while_asleep);
}
