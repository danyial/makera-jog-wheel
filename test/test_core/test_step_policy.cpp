// SPEC 11, tests 22-26: StepPolicy.
#include <unity.h>

#include "step_policy.h"
#include "test_suites.h"

namespace {

void test_22_level_for() {
  struct C { uint32_t v; uint8_t prev; uint8_t want; };
  const C cases[] = {{2, 0, 0},  {6, 0, 0},  {8, 0, 1},  {20, 0, 2}, {20, 1, 2}, {14, 2, 2},
                     {11, 2, 1}, {6, 2, 1},  {3, 2, 0},  {3, 1, 0},  {5, 1, 1}};
  for (const C& c : cases) TEST_ASSERT_EQUAL_UINT8(c.want, StepPolicy::level_for(c.v, c.prev));
}

void run_23(uint32_t o) {
  StepPolicy p;
  TEST_ASSERT_EQUAL_INT32(10, p.update(o + 0, 0));
  TEST_ASSERT_EQUAL_INT32(10, p.update(o + 20, 1));
  TEST_ASSERT_EQUAL_INT32(10, p.update(o + 40, 1));
  TEST_ASSERT_EQUAL_INT32(10, p.update(o + 60, 1));
  TEST_ASSERT_EQUAL_INT32(50, p.update(o + 80, 1));
  TEST_ASSERT_EQUAL_UINT8(1, p.level());
}

void run_25(uint32_t o) {
  StepPolicy p;
  TEST_ASSERT_EQUAL_INT32(50, p.update(o + 0, 10));
  TEST_ASSERT_EQUAL_INT32(100, p.update(o + 20, 1));
  TEST_ASSERT_EQUAL_INT32(100, p.update(o + 319, 0));
  TEST_ASSERT_EQUAL_INT32(10, p.update(o + 320, 0));
  TEST_ASSERT_EQUAL_INT32(10, p.update(o + 340, 1));
}

void test_23_update_ramp() { run_23(0); }

void test_24_update_one_level_per_tick() {
  StepPolicy p;
  TEST_ASSERT_EQUAL_INT32(50, p.update(0, 10));
  TEST_ASSERT_EQUAL_INT32(100, p.update(20, 1));
}

void test_25_update_pause_reset() { run_25(0); }

void test_26_update_wraparound() {
  run_23(0xFFFFFFE0u);
  run_25(0xFFFFFFE0u);
}

}  // namespace

void run_step_policy_tests() {
  RUN_TEST(test_22_level_for);
  RUN_TEST(test_23_update_ramp);
  RUN_TEST(test_24_update_one_level_per_tick);
  RUN_TEST(test_25_update_pause_reset);
  RUN_TEST(test_26_update_wraparound);
}
