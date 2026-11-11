// SPEC 11, tests 110-112: soft power, on/off button as an input (ticket #49).
#include <unity.h>

#include "core_config.h"
#include "power_switch.h"
#include "test_suites.h"

namespace {

constexpr uint32_t D = POWER_SW_DEBOUNCE_MS;

// Normal debouncing from t0: on, short off bounces, a real off exactly once.
void check_debounce(uint32_t t0) {
  PowerSwitch s(D);
  PowerTick r = s.update(t0, true);
  TEST_ASSERT_EQUAL(PowerLevel::Unknown, r.level);
  TEST_ASSERT_EQUAL(PowerLevel::Unknown, s.update(t0 + D - 1, true).level);
  r = s.update(t0 + D, true);
  TEST_ASSERT_EQUAL(PowerLevel::On, r.level);
  TEST_ASSERT_FALSE(r.turned_off);
  // Bounce: off for D - 1, on again; repeated. Never Off.
  uint32_t t = t0 + 2 * D;
  for (int i = 0; i < 5; ++i) {
    r = s.update(t, false);
    TEST_ASSERT_EQUAL(PowerLevel::On, r.level);
    TEST_ASSERT_FALSE(r.turned_off);
    r = s.update(t + D - 1, false);
    TEST_ASSERT_EQUAL(PowerLevel::On, r.level);
    TEST_ASSERT_FALSE(r.turned_off);
    r = s.update(t + D, true);  // contact closed again, restarts the candidate
    TEST_ASSERT_EQUAL(PowerLevel::On, r.level);
    t += 2 * D;
  }
  // Real off: Off after D, turned_off exactly once.
  TEST_ASSERT_FALSE(s.update(t, false).turned_off);
  TEST_ASSERT_FALSE(s.update(t + D - 1, false).turned_off);
  r = s.update(t + D, false);
  TEST_ASSERT_EQUAL(PowerLevel::Off, r.level);
  TEST_ASSERT_TRUE(r.turned_off);
  r = s.update(t + 10 * D, false);
  TEST_ASSERT_EQUAL(PowerLevel::Off, r.level);
  TEST_ASSERT_FALSE(r.turned_off);
  // On again (only matters if the firmware did not sleep): On after D, no off edge.
  s.update(t + 11 * D, true);
  r = s.update(t + 12 * D, true);
  TEST_ASSERT_EQUAL(PowerLevel::On, r.level);
  TEST_ASSERT_FALSE(r.turned_off);
}

void test_110_power_switch_debounce() {
  check_debounce(0);
  check_debounce(0xFFFFFF00u);  // across the millis() overflow
}

// Boot: the first stable level decides; off from the start is an off edge (sleep at once).
void test_111_power_switch_boot() {
  {
    PowerSwitch s(D);
    TEST_ASSERT_FALSE(s.update(1000, false).turned_off);
    TEST_ASSERT_EQUAL(PowerLevel::Unknown, s.update(1000 + D - 1, false).level);
    const PowerTick r = s.update(1000 + D, false);
    TEST_ASSERT_EQUAL(PowerLevel::Off, r.level);
    TEST_ASSERT_TRUE(r.turned_off);
  }
  {
    // Switched on at power-up, contact bounces for a while: Unknown until stable, then On, no off edge.
    PowerSwitch s(D);
    bool raw = false;
    for (uint32_t t = 0; t <= 60; t += 3) {  // last sample at 60: off
      const PowerTick r = s.update(t, raw);
      TEST_ASSERT_EQUAL(PowerLevel::Unknown, r.level);
      TEST_ASSERT_FALSE(r.turned_off);
      raw = !raw;
    }
    s.update(61, true);
    TEST_ASSERT_EQUAL(PowerLevel::Unknown, s.update(61 + D - 1, true).level);
    const PowerTick r = s.update(61 + D, true);
    TEST_ASSERT_EQUAL(PowerLevel::On, r.level);
    TEST_ASSERT_FALSE(r.turned_off);
  }
  {
    // Noise that never holds D: stays Unknown (the firmware then starts normally, 9.2).
    PowerSwitch s(D);
    bool raw = true;
    for (uint32_t t = 0; t < POWER_SW_BOOT_MAX_MS; t += D / 2) {
      const PowerTick r = s.update(t, raw);
      TEST_ASSERT_EQUAL(PowerLevel::Unknown, r.level);
      TEST_ASSERT_FALSE(r.turned_off);
      raw = !raw;
    }
  }
  TEST_ASSERT_TRUE(POWER_SW_BOOT_MAX_MS > POWER_SW_DEBOUNCE_MS);
}

// Wake level for deep sleep.
void test_112_sleep_wake_level() {
  TEST_ASSERT_TRUE(sleep_wake_when_on(SleepReason::SwitchOff, false));
  TEST_ASSERT_TRUE(sleep_wake_when_on(SleepReason::SwitchOff, true));  // on again already: wakes at once
  TEST_ASSERT_FALSE(sleep_wake_when_on(SleepReason::BatteryEmpty, true));  // wait for off, never wake at once
  TEST_ASSERT_TRUE(sleep_wake_when_on(SleepReason::BatteryEmpty, false));
}

}  // namespace

void run_power_switch_tests() {
  RUN_TEST(test_110_power_switch_debounce);
  RUN_TEST(test_111_power_switch_boot);
  RUN_TEST(test_112_sleep_wake_level);
}
