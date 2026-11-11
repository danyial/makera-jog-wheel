// SPEC 11, test 51: Battery.
#include <unity.h>

#include "battery.h"
#include "test_suites.h"

namespace {

uint8_t pct(uint16_t mv) {
  Battery b;
  b.add_sample(mv, false);
  return b.percent();
}

void test_51_battery() {
  TEST_ASSERT_EQUAL_UINT8(0, pct(3300));
  TEST_ASSERT_EQUAL_UINT8(100, pct(4150));
  TEST_ASSERT_EQUAL_UINT8(50, pct(3725));
  {
    Battery b;
    b.add_sample(5000, false);
    TEST_ASSERT_EQUAL_UINT8(100, b.percent());
  }
  TEST_ASSERT_EQUAL_UINT8(0, pct(3000));

  {
    Battery b;
    b.add_sample(3299, false);
    b.add_sample(3299, false);
    TEST_ASSERT_FALSE(b.critical());
    b.add_sample(3299, false);
    TEST_ASSERT_TRUE(b.critical());
    b.add_sample(4000, false);
    TEST_ASSERT_TRUE(b.critical());  // latched
  }
  {
    Battery b;
    b.add_sample(3299, false);
    b.add_sample(3299, false);
    b.add_sample(3500, false);
    b.add_sample(3299, false);
    TEST_ASSERT_FALSE(b.critical());  // not consecutive
  }
  {
    Battery b;
    b.add_sample(3299, true);
    TEST_ASSERT_TRUE(b.critical());
  }
  {
    Battery b;
    b.add_sample(3399, false);
    TEST_ASSERT_TRUE(b.warn());
    b.add_sample(3400, false);
    TEST_ASSERT_FALSE(b.warn());
  }
  const uint16_t absent_mv[2] = {2799, 4401};
  for (uint16_t mv : absent_mv) {
    Battery b;
    b.add_sample(mv, true);
    TEST_ASSERT_TRUE(b.absent());
    TEST_ASSERT_FALSE(b.critical());
    for (int i = 0; i < 5; ++i) b.add_sample(mv, false);
    TEST_ASSERT_FALSE(b.critical());
  }
  const uint16_t present_mv[2] = {2800, 4400};
  for (uint16_t mv : present_mv) {
    Battery b;
    b.add_sample(mv, false);
    TEST_ASSERT_FALSE(b.absent());
  }
}

}  // namespace

void run_battery_tests() { RUN_TEST(test_51_battery); }
