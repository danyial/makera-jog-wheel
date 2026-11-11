// SPEC 11, tests 10-14: status parser.
#include <unity.h>

#include <cstring>

#include "status.h"
#include "test_suites.h"

namespace {

bool parse(const char* s, MachineStatus& st) { return parse_status(s, std::strlen(s), st); }

void test_10_real_report() {
  const char* r =
      "<Idle|MPos:-1.0000,-1.0000,-1.0000,0.0000,0.0000|WPos:190.5400,193.4900,73.1641,-90.0000,0.0000|"
      "F:0.0,3000.0,100.0|S:0.0,10000.0,100.0,0,22.8,24.1,0,0,0,0|T:1,1.329,-1|L:0, 0, 0, 0.0,100.0|"
      "C:4,1,0,1|E:0,0,0,25,7613|OTA:0,0>\n";
  MachineStatus s{};
  TEST_ASSERT_TRUE(parse(r, s));
  TEST_ASSERT_EQUAL(MachineState::Idle, s.state);
  const int32_t mpos[3] = {-1000, -1000, -1000};
  const int32_t wpos[3] = {190540, 193490, 73164};
  TEST_ASSERT_EQUAL_INT32_ARRAY(mpos, s.mpos_um, 3);
  TEST_ASSERT_EQUAL_INT32_ARRAY(wpos, s.wpos_um, 3);
  TEST_ASSERT_FALSE(s.inch_mode);
  TEST_ASSERT_FALSE(s.playing);
  TEST_ASSERT_FALSE(s.halted);
}

void test_11_playing_without_newline() {
  MachineStatus s{};
  TEST_ASSERT_TRUE(parse("<Run|MPos:1,2,3,0,0|WPos:1,2,3,0,0|P:120,42,305|C:4,1,0,1>", s));
  TEST_ASSERT_EQUAL(MachineState::Run, s.state);
  TEST_ASSERT_TRUE(s.playing);
  TEST_ASSERT_EQUAL_INT32(1000, s.mpos_um[0]);
}

void test_12_halted() {
  MachineStatus s{};
  TEST_ASSERT_TRUE(parse("<Alarm|MPos:0,0,0,0,0|WPos:0,0,0,0,0|H:3|C:4,1,0,1>", s));
  TEST_ASSERT_EQUAL(MachineState::Alarm, s.state);
  TEST_ASSERT_TRUE(s.halted);
}

void test_13_inch_mode() {
  MachineStatus s{};
  TEST_ASSERT_TRUE(parse("<Idle|MPos:1.0000,0,0,0,0|WPos:0,0,0,0,0|C:4,1,1,1>", s));
  TEST_ASSERT_TRUE(s.inch_mode);
  TEST_ASSERT_EQUAL_INT32(25400, s.mpos_um[0]);
}

void test_14_invalid_reports() {
  MachineStatus s{};
  TEST_ASSERT_FALSE(parse("<Idle|MPos:0,0,0,0,0|WPos:0,0,0,0,0>", s));
  TEST_ASSERT_FALSE(parse("ok", s));
  TEST_ASSERT_FALSE(parse("", s));
  TEST_ASSERT_FALSE(parse("\n", s));
  TEST_ASSERT_FALSE(parse("<Idle", s));
  TEST_ASSERT_FALSE(parse_status(nullptr, 0, s));
  TEST_ASSERT_FALSE(parse("<Idle|WPos:0,0,0|C:4,1,0,1>", s));       // MPos missing
  TEST_ASSERT_FALSE(parse("<Idle|MPos:0,0,0|C:4,1,0,1>", s));       // WPos missing
  TEST_ASSERT_FALSE(parse("<Idle|MPos:0,x,0|WPos:0,0,0|C:4,1,0,1>", s));  // garbage number
  TEST_ASSERT_FALSE(parse("<Idle|MPos:0,0|WPos:0,0,0|C:4,1,0,1>", s));    // too few axes
}

}  // namespace

void run_status_tests() {
  RUN_TEST(test_10_real_report);
  RUN_TEST(test_11_playing_without_newline);
  RUN_TEST(test_12_halted);
  RUN_TEST(test_13_inch_mode);
  RUN_TEST(test_14_invalid_reports);
}
