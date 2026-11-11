#include "settings_rules.h"

#include <cstring>

#include "core_config.h"

bool valid_ipv4(const char* s) {
  if (s == nullptr) return false;
  int parts = 0;
  int digits = 0;
  long value = 0;
  for (size_t i = 0;; ++i) {
    const char c = s[i] != '\0' ? s[i] : '.';
    if (c == '.') {
      if (digits == 0 || value > 255) return false;
      ++parts;
      digits = 0;
      value = 0;
      if (s[i] == '\0') break;
    } else if (c >= '0' && c <= '9') {
      if (++digits > 3) return false;
      value = value * 10 + (c - '0');
    } else {
      return false;
    }
  }
  return parts == 4;
}

bool parse_port(const char* s, uint16_t default_port, uint16_t& out) {
  if (s == nullptr || s[0] == '\0') {
    out = default_port;
    return true;
  }
  long v = 0;
  for (size_t i = 0; s[i] != '\0'; ++i) {
    if (s[i] < '0' || s[i] > '9' || i >= 5) return false;
    v = v * 10 + (s[i] - '0');
  }
  if (v < 1 || v > 65535) return false;
  out = static_cast<uint16_t>(v);
  return true;
}

SettingsError validate_settings(const char* ssid, const char* pass, const char* host, const char* port,
                                uint16_t default_port, uint16_t& port_out) {
  const size_t ssid_len = ssid != nullptr ? std::strlen(ssid) : 0;
  if (ssid_len == 0 || ssid_len > 32) return SettingsError::Ssid;
  if (pass != nullptr && std::strlen(pass) > 63) return SettingsError::Pass;
  if (!valid_ipv4(host)) return SettingsError::Host;
  if (!parse_port(port, default_port, port_out)) return SettingsError::Port;
  return SettingsError::None;
}

const char* settings_error_text(SettingsError e) {
  switch (e) {
    case SettingsError::None: return "";
    case SettingsError::Ssid: return "SSID fehlt oder ist zu lang.";
    case SettingsError::Pass: return "Passwort ist zu lang.";
    case SettingsError::Host: return "Maschinen-IP ist keine gültige IPv4-Adresse.";
    case SettingsError::Port: return "Port muss zwischen 1 und 65535 liegen.";
  }
  return "";
}

bool settings_change_allowed(AxisMode axis, bool session_active) {
  return axis == AxisMode::None && !session_active;
}

bool ota_update_allowed(AxisMode axis, bool session_active, bool battery_warn) {
  return settings_change_allowed(axis, session_active) && !battery_warn;
}

bool secret_equals(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  const size_t na = std::strlen(a);
  const size_t nb = std::strlen(b);
  unsigned diff = na == nb ? 0u : 1u;
  for (size_t i = 0; i < na; ++i) {
    diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(i < nb ? b[i] : 0);
  }
  return diff == 0;
}

bool AuthThrottle::locked(uint32_t now_ms) const {
  return have_fail_ && static_cast<uint32_t>(now_ms - last_fail_ms_) < lock_ms_;
}

void AuthThrottle::on_fail(uint32_t now_ms) {
  have_fail_ = true;
  last_fail_ms_ = now_ms;
}
