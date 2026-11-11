// SPEC 11, tests 1-9: frame codec and TextLines.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "frame.h"
#include "test_suites.h"
#include "text_lines.h"

namespace {

const char kModel[] = "model = Z1, 4, 1, 0, Idle\n";

std::vector<uint8_t> make(uint8_t type, const std::string& payload) {
  std::vector<uint8_t> out(payload.size() + frame::OVERHEAD);
  const size_t n = frame::build(type, reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
                                out.data(), out.size());
  out.resize(n);
  return out;
}

// Raw frame with arbitrary len field and payload, CRC computed over the given bytes.
std::vector<uint8_t> raw(uint16_t len, uint8_t type, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> f = {0x86, 0x68, static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len & 0xFF), type};
  f.insert(f.end(), payload.begin(), payload.end());
  const uint16_t crc = frame::crc16(f.data() + 2, f.size() - 2);
  f.push_back(static_cast<uint8_t>(crc >> 8));
  f.push_back(static_cast<uint8_t>(crc & 0xFF));
  f.push_back(0x55);
  f.push_back(0xAA);
  return f;
}

struct Collected {
  std::vector<std::pair<uint8_t, std::string>> frames;
};

void feed_all(frame::Decoder& d, const std::vector<uint8_t>& bytes, Collected& c) {
  d.feed(bytes.data(), bytes.size(), [&](const frame::Frame& f) {
    c.frames.emplace_back(f.type, std::string(reinterpret_cast<const char*>(f.payload), f.n));
  });
}

void append(std::vector<uint8_t>& a, const std::vector<uint8_t>& b) { a.insert(a.end(), b.begin(), b.end()); }

void test_01_crc16_check_value() {
  const char* s = "123456789";
  TEST_ASSERT_EQUAL_HEX16(0x31C3, frame::crc16(reinterpret_cast<const uint8_t*>(s), 9));
}

void test_02_build_matches_real_frame() {
  std::vector<uint8_t> expected = {0x86, 0x68, 0x00, 0x1d, 0x90};
  for (const char* p = kModel; *p; ++p) expected.push_back(static_cast<uint8_t>(*p));
  append(expected, {0xb1, 0x06, 0x55, 0xaa});
  const auto got = make(0x90, kModel);
  TEST_ASSERT_EQUAL_UINT(expected.size(), got.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), got.data(), expected.size());
}

void test_03_build_realtime_and_limit() {
  const auto f = make(0xA1, "?");
  // len = 0x0004, CRC-16/XModem over 00 04 A1 3F = 0x3533 (computed independently).
  const uint8_t expected[10] = {0x86, 0x68, 0x00, 0x04, 0xA1, 0x3F, 0x35, 0x33, 0x55, 0xAA};
  TEST_ASSERT_EQUAL_UINT(10, f.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, f.data(), 10);

  static uint8_t payload[514] = {0};
  static uint8_t out[1024];
  TEST_ASSERT_EQUAL_UINT(0, frame::build(0xA2, payload, 514, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT(513 + frame::OVERHEAD, frame::build(0xA2, payload, 513, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT(0, frame::build(0xA1, payload, 1, out, 9));  // out too small
}

void test_04_bytewise_with_leading_garbage() {
  std::vector<uint8_t> s = {0x00, 0x86, 0x12, 0xFF, 0x55, 0xAA, 0x86};
  append(s, make(0x90, kModel));
  append(s, make(0x81, "<Idle>"));
  frame::Decoder d;
  Collected c;
  for (uint8_t b : s) feed_all(d, {b}, c);
  TEST_ASSERT_EQUAL_UINT(2, c.frames.size());
  TEST_ASSERT_EQUAL_HEX8(0x90, c.frames[0].first);
  TEST_ASSERT_EQUAL_STRING(kModel, c.frames[0].second.c_str());
  TEST_ASSERT_EQUAL_HEX8(0x81, c.frames[1].first);
  TEST_ASSERT_EQUAL_STRING("<Idle>", c.frames[1].second.c_str());
}

void test_05_two_frames_one_feed() {
  std::vector<uint8_t> s = make(0x90, kModel);
  append(s, make(0x81, "<Idle>"));
  frame::Decoder d;
  Collected c;
  feed_all(d, s, c);
  TEST_ASSERT_EQUAL_UINT(2, c.frames.size());
  TEST_ASSERT_EQUAL_HEX8(0x90, c.frames[0].first);
  TEST_ASSERT_EQUAL_STRING(kModel, c.frames[0].second.c_str());
  TEST_ASSERT_EQUAL_HEX8(0x81, c.frames[1].first);
  TEST_ASSERT_EQUAL_STRING("<Idle>", c.frames[1].second.c_str());
}

void test_06_bad_crc_then_valid() {
  auto bad = make(0x90, kModel);
  bad[bad.size() - 3] ^= 0x01;
  auto s = bad;
  append(s, make(0x81, "<Idle>"));
  frame::Decoder d;
  Collected c;
  feed_all(d, s, c);
  TEST_ASSERT_EQUAL_UINT(1, c.frames.size());
  TEST_ASSERT_EQUAL_HEX8(0x81, c.frames[0].first);
  TEST_ASSERT_EQUAL_UINT32(1, d.dropped());
}

void test_07_footer_and_len_limits() {
  const auto good = make(0x81, "<Idle>");
  // Wrong footer.
  {
    auto bad = make(0x90, kModel);
    bad.back() = 0xAB;
    auto s = bad;
    append(s, good);
    frame::Decoder d;
    Collected c;
    feed_all(d, s, c);
    TEST_ASSERT_EQUAL_UINT(1, c.frames.size());
    TEST_ASSERT_EQUAL_STRING("<Idle>", c.frames[0].second.c_str());
  }
  // len field = 2.
  {
    std::vector<uint8_t> s = {0x86, 0x68, 0x00, 0x02, 0x90, 0x00, 0x00, 0x55, 0xAA};
    append(s, good);
    frame::Decoder d;
    Collected c;
    feed_all(d, s, c);
    TEST_ASSERT_EQUAL_UINT(1, c.frames.size());
    TEST_ASSERT_EQUAL_STRING("<Idle>", c.frames[0].second.c_str());
  }
  // len field = 604 (MAX_RX_PAYLOAD + 4).
  {
    auto s = raw(604, 0x90, std::vector<uint8_t>(601, 'a'));
    append(s, good);
    frame::Decoder d;
    Collected c;
    feed_all(d, s, c);
    TEST_ASSERT_EQUAL_UINT(1, c.frames.size());
    TEST_ASSERT_EQUAL_STRING("<Idle>", c.frames[0].second.c_str());
  }
  // len field = 603 with valid CRC: accepted, n = 600.
  {
    auto s = raw(603, 0x90, std::vector<uint8_t>(600, 'b'));
    append(s, good);
    frame::Decoder d;
    Collected c;
    feed_all(d, s, c);
    TEST_ASSERT_EQUAL_UINT(2, c.frames.size());
    TEST_ASSERT_EQUAL_UINT(600, c.frames[0].second.size());
  }
}

void test_08_textlines_split_feeds() {
  TextLines t;
  std::vector<std::string> lines;
  auto cb = [&](const char* l, size_t n) { lines.emplace_back(l, n); };
  const char* a = "ALARM: Hard";
  const char* b = "limit\n";
  t.feed(reinterpret_cast<const uint8_t*>(a), std::strlen(a), cb);
  TEST_ASSERT_EQUAL_UINT(0, lines.size());
  t.feed(reinterpret_cast<const uint8_t*>(b), std::strlen(b), cb);
  TEST_ASSERT_EQUAL_UINT(1, lines.size());
  TEST_ASSERT_EQUAL_STRING("ALARM: Hardlimit", lines[0].c_str());
}

void test_09_textlines_truncate() {
  TextLines t;
  std::vector<std::string> lines;
  std::string s(300, 'x');
  s += '\n';
  t.feed(reinterpret_cast<const uint8_t*>(s.data()), s.size(),
         [&](const char* l, size_t n) { lines.emplace_back(l, n); });
  TEST_ASSERT_EQUAL_UINT(1, lines.size());
  TEST_ASSERT_EQUAL_UINT(256, lines[0].size());
}

}  // namespace

void run_frame_tests() {
  RUN_TEST(test_01_crc16_check_value);
  RUN_TEST(test_02_build_matches_real_frame);
  RUN_TEST(test_03_build_realtime_and_limit);
  RUN_TEST(test_04_bytewise_with_leading_garbage);
  RUN_TEST(test_05_two_frames_one_feed);
  RUN_TEST(test_06_bad_crc_then_valid);
  RUN_TEST(test_07_footer_and_len_limits);
  RUN_TEST(test_08_textlines_split_feeds);
  RUN_TEST(test_09_textlines_truncate);
}
