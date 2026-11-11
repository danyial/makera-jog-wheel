// SPEC 11, tests 61-67: web UI in normal mode (ticket #17); test 75: ticket #29; tests 90-91: OTA (ticket #35).
#include <unity.h>

#include <cstring>

#include "core_config.h"
#include "http_parse.h"
#include "settings_rules.h"
#include "status_json.h"
#include "test_suites.h"

namespace {

HttpParse head(const char* s, HttpRequest& r) { return parse_http_head(s, std::strlen(s), r); }

void test_61_http_head() {
  HttpRequest r;
  TEST_ASSERT_EQUAL(HttpParse::Incomplete, head("GET / HTTP/1.1\r\nHost: x\r\n", r));
  TEST_ASSERT_EQUAL(HttpParse::Ok, head("GET /status.json?t=1 HTTP/1.1\r\nHost: x\r\n\r\n", r));
  TEST_ASSERT_EQUAL(HttpMethod::Get, r.method);
  TEST_ASSERT_EQUAL_STRING("/status.json", r.path);
  TEST_ASSERT_EQUAL_UINT32(0, r.content_length);
  const char* post = "POST /settings HTTP/1.1\r\ncontent-LENGTH: 23\r\n\r\nssid=a";
  TEST_ASSERT_EQUAL(HttpParse::Ok, head(post, r));
  TEST_ASSERT_EQUAL(HttpMethod::Post, r.method);
  TEST_ASSERT_EQUAL_STRING("/settings", r.path);
  TEST_ASSERT_EQUAL_UINT32(23, r.content_length);
  TEST_ASSERT_EQUAL_UINT32(std::strlen(post) - 6, r.head_len);
  TEST_ASSERT_EQUAL(HttpParse::Ok, head("PUT / HTTP/1.1\r\n\r\n", r));
  TEST_ASSERT_EQUAL(HttpMethod::Other, r.method);
  TEST_ASSERT_EQUAL(HttpParse::Bad, head("POST / HTTP/1.1\r\nContent-Length: 12a\r\n\r\n", r));
  TEST_ASSERT_EQUAL(HttpParse::Bad, head("POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n", r));
  TEST_ASSERT_EQUAL(HttpParse::Bad, head("GET /0123456789012345678901234567890 HTTP/1.1\r\n\r\n", r));
  TEST_ASSERT_EQUAL(HttpParse::Ok, head("GET /012345678901234567890123456789 HTTP/1.1\r\n\r\n", r));  // 31 chars
  TEST_ASSERT_EQUAL(HttpParse::Bad, head("GET x HTTP/1.1\r\n\r\n", r));
  TEST_ASSERT_EQUAL(HttpParse::Bad, head(" / HTTP/1.1\r\n\r\n", r));
}

void test_62_form_value() {
  const char* b = "ssid=My+Net%21&pass2=x&pass=&axis_a&host=192.0.2.105&bad=%4&zero=%00&hex=%zz";
  const size_t n = std::strlen(b);
  char out[16];
  TEST_ASSERT_EQUAL_INT(7, form_value(b, n, "ssid", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("My Net!", out);
  TEST_ASSERT_EQUAL_INT(0, form_value(b, n, "pass", out, sizeof(out)));  // not confused with pass2
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL_INT(0, form_value(b, n, "axis_a", out, sizeof(out)));  // key without '='
  TEST_ASSERT_EQUAL_INT(-1, form_value(b, n, "port", out, sizeof(out)));
  TEST_ASSERT_EQUAL_INT(-1, form_value(b, n, "pas", out, sizeof(out)));
  TEST_ASSERT_EQUAL_INT(-2, form_value(b, n, "bad", out, sizeof(out)));
  TEST_ASSERT_EQUAL_INT(-2, form_value(b, n, "zero", out, sizeof(out)));
  TEST_ASSERT_EQUAL_INT(-2, form_value(b, n, "hex", out, sizeof(out)));
  TEST_ASSERT_EQUAL_INT(11, form_value(b, n, "host", out, 12));  // exactly fits
  TEST_ASSERT_EQUAL_INT(-2, form_value(b, n, "host", out, 11));  // one too short
  // Body length limits the search: "host" lies beyond n.
  TEST_ASSERT_EQUAL_INT(-1, form_value(b, 10, "host", out, sizeof(out)));
}

void test_63_ipv4_port() {
  TEST_ASSERT_TRUE(valid_ipv4("192.0.2.105"));
  TEST_ASSERT_TRUE(valid_ipv4("0.0.0.0"));
  TEST_ASSERT_TRUE(valid_ipv4("255.255.255.255"));
  TEST_ASSERT_FALSE(valid_ipv4("256.1.1.1"));
  TEST_ASSERT_FALSE(valid_ipv4("1.2.3"));
  TEST_ASSERT_FALSE(valid_ipv4("1.2.3.4."));
  TEST_ASSERT_FALSE(valid_ipv4("1.2.3.4.5"));
  TEST_ASSERT_FALSE(valid_ipv4("1..3.4"));
  TEST_ASSERT_FALSE(valid_ipv4("1.2.3.0004"));
  TEST_ASSERT_FALSE(valid_ipv4(" 1.2.3.4"));
  TEST_ASSERT_FALSE(valid_ipv4(""));
  TEST_ASSERT_FALSE(valid_ipv4(nullptr));
  uint16_t p = 0;
  TEST_ASSERT_TRUE(parse_port("", 2222, p));
  TEST_ASSERT_EQUAL_UINT16(2222, p);
  TEST_ASSERT_TRUE(parse_port("1", 2222, p));
  TEST_ASSERT_EQUAL_UINT16(1, p);
  TEST_ASSERT_TRUE(parse_port("65535", 2222, p));
  TEST_ASSERT_EQUAL_UINT16(65535, p);
  TEST_ASSERT_FALSE(parse_port("0", 2222, p));
  TEST_ASSERT_FALSE(parse_port("65536", 2222, p));
  TEST_ASSERT_FALSE(parse_port("123456", 2222, p));
  TEST_ASSERT_FALSE(parse_port("80a", 2222, p));
  TEST_ASSERT_FALSE(parse_port("-1", 2222, p));
}

void test_64_validate_settings() {
  uint16_t p = 0;
  TEST_ASSERT_EQUAL(SettingsError::None, validate_settings("net", "pw", "192.0.2.2", "", 2222, p));
  TEST_ASSERT_EQUAL_UINT16(2222, p);
  TEST_ASSERT_EQUAL(SettingsError::None, validate_settings("net", "", "192.0.2.2", "8080", 2222, p));
  TEST_ASSERT_EQUAL_UINT16(8080, p);
  TEST_ASSERT_EQUAL(SettingsError::Ssid, validate_settings("", "pw", "192.0.2.2", "", 2222, p));
  TEST_ASSERT_EQUAL(SettingsError::Ssid,
                    validate_settings("123456789012345678901234567890123", "", "192.0.2.2", "", 2222, p));
  TEST_ASSERT_EQUAL(SettingsError::None,
                    validate_settings("12345678901234567890123456789012", "", "192.0.2.2", "", 2222, p));
  char pass64[65];
  std::memset(pass64, 'a', 64);
  pass64[64] = '\0';
  TEST_ASSERT_EQUAL(SettingsError::Pass, validate_settings("net", pass64, "192.0.2.2", "", 2222, p));
  pass64[63] = '\0';
  TEST_ASSERT_EQUAL(SettingsError::None, validate_settings("net", pass64, "192.0.2.2", "", 2222, p));
  TEST_ASSERT_EQUAL(SettingsError::Host, validate_settings("net", "", "makera.local", "", 2222, p));
  TEST_ASSERT_EQUAL(SettingsError::Port, validate_settings("net", "", "192.0.2.2", "0", 2222, p));
  TEST_ASSERT_EQUAL_STRING("Port muss zwischen 1 und 65535 liegen.", settings_error_text(SettingsError::Port));
}

void test_65_settings_change_allowed() {
  TEST_ASSERT_TRUE(settings_change_allowed(AxisMode::None, false));
  TEST_ASSERT_FALSE(settings_change_allowed(AxisMode::None, true));
  const AxisMode selected[] = {AxisMode::X, AxisMode::Y, AxisMode::XY, AxisMode::Z, AxisMode::A};
  for (AxisMode m : selected) {
    TEST_ASSERT_FALSE(settings_change_allowed(m, false));
    TEST_ASSERT_FALSE(settings_change_allowed(m, true));
  }
}

void test_66_auth() {
  TEST_ASSERT_TRUE(secret_equals("12345678", "12345678"));
  TEST_ASSERT_FALSE(secret_equals("12345678", "12345679"));
  TEST_ASSERT_FALSE(secret_equals("12345678", "1234567"));
  TEST_ASSERT_FALSE(secret_equals("1234567", "12345678"));
  TEST_ASSERT_FALSE(secret_equals("", "12345678"));
  TEST_ASSERT_FALSE(secret_equals(nullptr, "12345678"));

  AuthThrottle t;
  TEST_ASSERT_FALSE(t.locked(0));
  t.on_fail(1000);
  TEST_ASSERT_TRUE(t.locked(1000));
  TEST_ASSERT_TRUE(t.locked(1000 + WEB_AUTH_LOCK_MS - 1));
  TEST_ASSERT_FALSE(t.locked(1000 + WEB_AUTH_LOCK_MS));
  AuthThrottle w;  // overflow
  w.on_fail(0xFFFFFF00u);
  TEST_ASSERT_TRUE(w.locked(0xFFFFFF00u + WEB_AUTH_LOCK_MS - 1));
  TEST_ASSERT_FALSE(w.locked(0xFFFFFF00u + WEB_AUTH_LOCK_MS));
}

StatusView sample_view() {
  StatusView v{};
  v.fw = "0.1.0";
  v.uptime_s = 42;
  v.link = "Online";
  v.last_error = "say \"hi\"\n";
  v.wifi = true;
  v.rssi_dbm = -61;
  v.ip = "192.0.2.7";
  v.have_status = true;
  v.status.state = MachineState::Idle;
  v.status.wpos_um[0] = 1000;
  v.status.wpos_um[1] = -5;
  v.status.wpos_um[2] = 123456;
  v.status.wpos_um[3] = 45500;
  v.status.mpos_um[0] = -1000;
  v.status.has_a = false;
  v.status_age_ms = 30;
  v.gate = "Achse wählen";
  v.axis = "keine";
  v.settings_allowed = true;
  v.bat_mv = 3900;
  v.bat_percent = 70;
  v.have_stats = true;
  v.stats.status_ok = 48;
  v.stats.loop_max_ms = 27;
  return v;
}

void test_67_status_json() {
  char out[1024];
  StatusView v = sample_view();
  const size_t n = format_status_json(v, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_UINT32(std::strlen(out), n);
  TEST_ASSERT_EQUAL_CHAR('{', out[0]);
  TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"link\":\"Online\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"last_error\":\"say \\\"hi\\\"\\u000a\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"rssi\":-61"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"state\":\"Idle\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"wpos\":[1.000,-0.005,123.456]"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"mpos\":[-1.000,0.000,0.000]"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"gate\":\"Achse wählen\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"settings_allowed\":true"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"alarm\":null"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"stats\":{\"status_ok\":48,"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"loop_max_ms\":27"));

  // A as 4th value only when the report has it.
  v.status.has_a = true;
  format_status_json(v, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"wpos\":[1.000,-0.005,123.456,45.500]"));

  // No WiFi, no status, no stats.
  v.wifi = false;
  v.have_status = false;
  v.have_stats = false;
  v.alarm = "ALARM: Hard limit";
  format_status_json(v, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"rssi\":null"));
  TEST_ASSERT_NULL(std::strstr(out, "\"wpos\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"stats\":null}"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"alarm\":\"ALARM: Hard limit\""));

  // Buffer too small -> 0, never a truncated object.
  v = sample_view();
  const size_t full = format_status_json(v, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT32(0, format_status_json(v, out, full));
  TEST_ASSERT_EQUAL_UINT32(full, format_status_json(v, out, full + 1));
  TEST_ASSERT_EQUAL_STRING("Hold", machine_state_name(MachineState::Hold));

  // Worst case fits into WEB_JSON_MAX: longest failure reason (47 chars, machine_link error_buf[48])
  // and alarm line (21 chars, UI_LINE_MAX_CHARS) made of control characters (6 bytes each when
  // escaped), longest IP, int32 extremes on all 8 positions including A, all counters at maximum.
  char fail[48];
  std::memset(fail, '\x01', 47);
  fail[47] = '\0';
  char alarm[22];
  std::memset(alarm, '\x1f', 21);
  alarm[21] = '\0';
  StatusView w = sample_view();
  w.last_error = fail;
  w.alarm = alarm;
  w.ip = "255.255.255.255";
  w.link = "WLAN verbindet";
  w.gate = "Alarm, Taste 5 s: Entsperren";  // longest gate text (unlock hint, ticket #41)
  w.axis = "keine";
  w.uptime_s = 0xFFFFFFFFu;
  w.rssi_dbm = -2147483647 - 1;
  w.status.has_a = true;
  w.status.playing = w.status.halted = w.status.inch_mode = true;
  w.status.state = MachineState::Unknown;
  for (int i = 0; i < 4; ++i) {
    w.status.mpos_um[i] = -2147483647 - 1;
    w.status.wpos_um[i] = -2147483647 - 1;
  }
  w.status_age_ms = 0xFFFFFFFFu;
  w.bat_mv = 65535;
  w.bat_percent = 100;
  w.stats = StatsView{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                      0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  static char big[WEB_JSON_MAX];
  const size_t worst = format_status_json(w, big, sizeof(big));
  TEST_ASSERT_TRUE(worst > 0);
  TEST_ASSERT_NOT_NULL(std::strstr(big, "-2147483.648,-2147483.648,-2147483.648,-2147483.648]"));
  TEST_ASSERT_NOT_NULL(std::strstr(big, "\\u0001\\u0001"));
}

// Ticket #29: curl got "Empty reply from server". The exact curl request parses fine, the
// connection was closed by the timeout check: respond() stamped the response with millis(),
// a few ms after the loop's now, and the unsigned now - since wrapped to ~2^32.
void test_75_curl_request_and_conn_timeout() {
  HttpRequest r;
  const char* curl = "GET / HTTP/1.1\r\nHost: 192.0.2.236\r\nUser-Agent: curl/8.7.1\r\nAccept: */*\r\n\r\n";
  TEST_ASSERT_EQUAL(HttpParse::Ok, head(curl, r));
  TEST_ASSERT_EQUAL(HttpMethod::Get, r.method);
  TEST_ASSERT_EQUAL_STRING("/", r.path);
  TEST_ASSERT_EQUAL_UINT32(std::strlen(curl), r.head_len);
  TEST_ASSERT_EQUAL_UINT32(0, r.content_length);
  const char* json =
      "GET /status.json HTTP/1.1\r\nHost: 192.0.2.236\r\nUser-Agent: curl/8.7.1\r\nAccept: */*\r\n\r\n";
  TEST_ASSERT_EQUAL(HttpParse::Ok, head(json, r));
  TEST_ASSERT_EQUAL_STRING("/status.json", r.path);

  const uint32_t t = 1000;
  TEST_ASSERT_FALSE(web_conn_expired(t, t));
  TEST_ASSERT_FALSE(web_conn_expired(t + WEB_REQ_TIMEOUT_MS - 1, t));
  TEST_ASSERT_TRUE(web_conn_expired(t + WEB_REQ_TIMEOUT_MS, t));
  // The #29 case: stamp taken later in the same pass than now -> not expired.
  TEST_ASSERT_FALSE(web_conn_expired(t, t + 3));
  TEST_ASSERT_TRUE(static_cast<uint32_t>(t - (t + 3)) >= WEB_REQ_TIMEOUT_MS);  // the old check closed here
  // Across the millis() overflow.
  const uint32_t w = 0xFFFFFF00u;
  TEST_ASSERT_FALSE(web_conn_expired(w + WEB_REQ_TIMEOUT_MS - 1, w));
  TEST_ASSERT_TRUE(web_conn_expired(w + WEB_REQ_TIMEOUT_MS, w));
}

// Ticket #37: progress-based send budget for the static assets.
void test_92_send_budget() {
  const uint32_t t = 1000;
  // Pages and JSON: max = WEB_REQ_TIMEOUT_MS, progress does not extend it (unchanged 2 s budget).
  TEST_ASSERT_FALSE(web_send_expired(t + WEB_REQ_TIMEOUT_MS - 1, t, t + 1500, WEB_REQ_TIMEOUT_MS));
  TEST_ASSERT_TRUE(web_send_expired(t + WEB_REQ_TIMEOUT_MS, t, t + 1500, WEB_REQ_TIMEOUT_MS));
  // Asset: every partial send restarts the 2 s idle budget ...
  TEST_ASSERT_FALSE(web_send_expired(t + 10000, t, t + 8001, WEB_ASSET_SEND_MAX_MS));
  TEST_ASSERT_TRUE(web_send_expired(t + 10000, t, t + 8000, WEB_ASSET_SEND_MAX_MS));
  TEST_ASSERT_FALSE(web_send_expired(t + WEB_REQ_TIMEOUT_MS - 1, t, t, WEB_ASSET_SEND_MAX_MS));
  TEST_ASSERT_TRUE(web_send_expired(t + WEB_REQ_TIMEOUT_MS, t, t, WEB_ASSET_SEND_MAX_MS));
  // ... but never beyond WEB_ASSET_SEND_MAX_MS in total, even with steady progress.
  TEST_ASSERT_FALSE(web_send_expired(t + WEB_ASSET_SEND_MAX_MS - 1, t, t + WEB_ASSET_SEND_MAX_MS - 1, WEB_ASSET_SEND_MAX_MS));
  TEST_ASSERT_TRUE(web_send_expired(t + WEB_ASSET_SEND_MAX_MS, t, t + WEB_ASSET_SEND_MAX_MS, WEB_ASSET_SEND_MAX_MS));
  // Stamps taken later in the same pass than now (#29) and the millis() overflow.
  TEST_ASSERT_FALSE(web_send_expired(t, t + 3, t + 3, WEB_ASSET_SEND_MAX_MS));
  const uint32_t w = 0xFFFFF000u;
  TEST_ASSERT_FALSE(web_send_expired(w + 20000, w, w + 19000, WEB_ASSET_SEND_MAX_MS));
  TEST_ASSERT_TRUE(web_send_expired(w + WEB_ASSET_SEND_MAX_MS, w, w + WEB_ASSET_SEND_MAX_MS - 1, WEB_ASSET_SEND_MAX_MS));
}

// Ticket #35: OTA only with no axis, no session and no low battery.
void test_90_ota_update_allowed() {
  TEST_ASSERT_TRUE(ota_update_allowed(AxisMode::None, false, false));
  TEST_ASSERT_FALSE(ota_update_allowed(AxisMode::None, true, false));
  TEST_ASSERT_FALSE(ota_update_allowed(AxisMode::None, false, true));
  TEST_ASSERT_FALSE(ota_update_allowed(AxisMode::None, true, true));
  const AxisMode selected[] = {AxisMode::X, AxisMode::Y, AxisMode::XY, AxisMode::Z, AxisMode::A};
  for (AxisMode m : selected) {
    for (int session = 0; session < 2; ++session) {
      for (int warn = 0; warn < 2; ++warn) TEST_ASSERT_FALSE(ota_update_allowed(m, session != 0, warn != 0));
    }
  }
  // Same rule as for saving settings, plus the battery.
  const AxisMode all[] = {AxisMode::None, AxisMode::X, AxisMode::Y, AxisMode::XY, AxisMode::Z, AxisMode::A};
  for (AxisMode m : all) {
    for (int session = 0; session < 2; ++session) {
      TEST_ASSERT_EQUAL(settings_change_allowed(m, session != 0), ota_update_allowed(m, session != 0, false));
    }
  }
}

// Ticket #35: lockout after a wrong OTA password; the web UI keeps its own, shorter lock.
void test_91_ota_auth_lock() {
  AuthThrottle t(OTA_AUTH_LOCK_MS);
  TEST_ASSERT_FALSE(t.locked(0));
  TEST_ASSERT_FALSE(t.locked(123456));
  t.on_fail(1000);
  TEST_ASSERT_TRUE(t.locked(1000));
  TEST_ASSERT_TRUE(t.locked(1000 + WEB_AUTH_LOCK_MS));  // longer than the web lock
  TEST_ASSERT_TRUE(t.locked(1000 + OTA_AUTH_LOCK_MS - 1));
  TEST_ASSERT_FALSE(t.locked(1000 + OTA_AUTH_LOCK_MS));
  t.on_fail(20000);  // a new failure restarts the lock
  TEST_ASSERT_TRUE(t.locked(20000 + OTA_AUTH_LOCK_MS - 1));
  TEST_ASSERT_FALSE(t.locked(20000 + OTA_AUTH_LOCK_MS));
  AuthThrottle w(OTA_AUTH_LOCK_MS);  // overflow between failure and expiry
  w.on_fail(0xFFFFF000u);
  TEST_ASSERT_TRUE(w.locked(0xFFFFF000u + OTA_AUTH_LOCK_MS - 1));
  TEST_ASSERT_FALSE(w.locked(0xFFFFF000u + OTA_AUTH_LOCK_MS));
  AuthThrottle web;  // default stays WEB_AUTH_LOCK_MS
  web.on_fail(1000);
  TEST_ASSERT_FALSE(web.locked(1000 + WEB_AUTH_LOCK_MS));
}

}  // namespace

void run_webui_tests() {
  RUN_TEST(test_61_http_head);
  RUN_TEST(test_62_form_value);
  RUN_TEST(test_63_ipv4_port);
  RUN_TEST(test_64_validate_settings);
  RUN_TEST(test_65_settings_change_allowed);
  RUN_TEST(test_66_auth);
  RUN_TEST(test_67_status_json);
  RUN_TEST(test_75_curl_request_and_conn_timeout);
  RUN_TEST(test_90_ota_update_allowed);
  RUN_TEST(test_91_ota_auth_lock);
  RUN_TEST(test_92_send_budget);
}
