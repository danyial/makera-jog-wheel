// SPEC 11, tests 113-114: board revision guard (ticket #49).
#include <unity.h>

#include "board_rev.h"
#include "core_config.h"
#include "test_suites.h"

namespace {

void test_113_board_rev_action() {
  TEST_ASSERT_EQUAL(BoardRevAction::Store, board_rev_action(false, 0, 2));
  TEST_ASSERT_EQUAL(BoardRevAction::Store, board_rev_action(false, 1, 2));  // value ignored without the key
  TEST_ASSERT_EQUAL(BoardRevAction::Run, board_rev_action(true, 1, 1));
  TEST_ASSERT_EQUAL(BoardRevAction::Run, board_rev_action(true, 2, 2));
  TEST_ASSERT_EQUAL(BoardRevAction::WrongBoard, board_rev_action(true, 2, 1));
  TEST_ASSERT_EQUAL(BoardRevAction::WrongBoard, board_rev_action(true, 1, 2));
  TEST_ASSERT_EQUAL(BoardRevAction::WrongBoard, board_rev_action(true, 0, 2));    // unreadable value: safe
  TEST_ASSERT_EQUAL(BoardRevAction::WrongBoard, board_rev_action(true, 255, 2));
}

void check_confirm(uint32_t t0) {
  constexpr uint32_t H = BOARD_REV_CONFIRM_MS;
  BoardRevConfirm c(H);
  TEST_ASSERT_FALSE(c.update(t0, false));
  TEST_ASSERT_FALSE(c.update(t0 + 10, true));             // hold starts
  TEST_ASSERT_FALSE(c.update(t0 + 10 + H - 1, true));
  TEST_ASSERT_FALSE(c.update(t0 + 10 + H, false));        // released one ms short: nothing
  TEST_ASSERT_FALSE(c.update(t0 + 20 + H, true));         // new hold
  TEST_ASSERT_FALSE(c.update(t0 + 20 + 2 * H - 1, true));
  TEST_ASSERT_TRUE(c.update(t0 + 20 + 2 * H, true));      // exactly H held
  TEST_ASSERT_FALSE(c.update(t0 + 20 + 3 * H, true));     // only once per hold
  TEST_ASSERT_FALSE(c.update(t0 + 30 + 3 * H, false));
  TEST_ASSERT_FALSE(c.update(t0 + 40 + 3 * H, true));
  TEST_ASSERT_TRUE(c.update(t0 + 40 + 4 * H, true));      // a new hold fires again
}

void test_114_board_rev_confirm() {
  TEST_ASSERT_EQUAL_UINT32(3000, BOARD_REV_CONFIRM_MS);
  check_confirm(0);
  check_confirm(0xFFFFF000u);  // across the millis() overflow
  // Held since power-on: not armed, never confirms, however long; after a release the full time counts.
  BoardRevConfirm c(BOARD_REV_CONFIRM_MS);
  for (uint32_t t = 0; t <= 4 * BOARD_REV_CONFIRM_MS; t += 100) TEST_ASSERT_FALSE(c.update(t, true));
  TEST_ASSERT_FALSE(c.update(4 * BOARD_REV_CONFIRM_MS + 10, false));
  TEST_ASSERT_FALSE(c.update(4 * BOARD_REV_CONFIRM_MS + 20, true));
  TEST_ASSERT_FALSE(c.update(5 * BOARD_REV_CONFIRM_MS + 19, true));
  TEST_ASSERT_TRUE(c.update(5 * BOARD_REV_CONFIRM_MS + 20, true));
}

}  // namespace

void run_board_rev_tests() {
  RUN_TEST(test_113_board_rev_action);
  RUN_TEST(test_114_board_rev_confirm);
}
