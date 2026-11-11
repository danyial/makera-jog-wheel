#pragma once
// Settings validation shared by the captive portal and the web UI, plus the rules for changing
// settings in normal mode (SPEC 9.1, ticket #17).
#include <cstdint>

#include "axis_select.h"
#include "core_config.h"

// Dotted-quad IPv4, four parts 0..255, at most 3 digits each, nothing else.
bool valid_ipv4(const char* s);

// Port field: empty -> default_port; otherwise only digits, 1..65535.
bool parse_port(const char* s, uint16_t default_port, uint16_t& out);

enum class SettingsError : uint8_t { None, Ssid, Pass, Host, Port };

// ssid and host already trimmed. On None, port_out holds the parsed port.
SettingsError validate_settings(const char* ssid, const char* pass, const char* host, const char* port,
                                uint16_t default_port, uint16_t& port_out);
const char* settings_error_text(SettingsError e);

// Settings may change in normal mode only with no axis selected and no jog session (ticket #17).
bool settings_change_allowed(AxisMode axis, bool session_active);

// An OTA update over WiFi (SPEC 9.5, ticket #35) may be accepted only when settings could change
// (no axis selected, no jog session) and the battery is not low: a brownout in the middle of the
// flash write would waste the update (the old firmware stays active, but the user has to repeat it).
// main.cpp lets ArduinoOTA run only in a loop pass where this holds.
bool ota_update_allowed(AxisMode axis, bool session_active, bool battery_warn);

// Compares two NUL-terminated strings without an early exit on the first differing character.
bool secret_equals(const char* a, const char* b);

// After a wrong password, further attempts are refused for lock_ms: WEB_AUTH_LOCK_MS for the web UI,
// OTA_AUTH_LOCK_MS for OTA (ticket #35, the OTA listener stays closed meanwhile).
class AuthThrottle {
 public:
  explicit AuthThrottle(uint32_t lock_ms = WEB_AUTH_LOCK_MS) : lock_ms_(lock_ms) {}
  bool locked(uint32_t now_ms) const;
  void on_fail(uint32_t now_ms);

 private:
  uint32_t lock_ms_;
  bool have_fail_ = false;
  uint32_t last_fail_ms_ = 0;
};
