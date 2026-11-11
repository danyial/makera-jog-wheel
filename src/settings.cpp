#include "settings.h"

#include <Preferences.h>
#include <esp_random.h>

#include "settings_rules.h"
#include "ui_config.h"
#include "web_pages.h"

namespace settings {
namespace {
constexpr const char* kNamespace = "handrad";

void save(const Settings& s) {
  Preferences p;
  p.begin(kNamespace, false);
  p.putString("ssid", s.ssid);
  p.putString("pass", s.pass);
  p.putString("host", s.host);
  p.putUShort("port", s.port);
  p.putBool("axis_a", s.axis_a);
  p.putBool("ota", s.ota);
  p.end();
}
}  // namespace

const char kPageHead[] PROGMEM = WEB_PAGE_HEAD;

bool load(Settings& out) {
  Preferences p;
  p.begin(kNamespace, true);
  out.ssid = p.getString("ssid", "");
  out.pass = p.getString("pass", "");
  out.host = p.getString("host", "");
  out.port = p.getUShort("port", UI_DEFAULT_PORT);
  out.axis_a = p.getBool("axis_a", false);
  out.ota = p.getBool("ota", false);
  p.end();
  if (out.port == 0) out.port = UI_DEFAULT_PORT;
  return out.ssid.length() > 0 && out.host.length() > 0;
}

String ap_pass() {
  Preferences p;
  p.begin(kNamespace, false);
  String pw = p.getString("ap_pass", "");
  if (pw.length() != static_cast<unsigned>(AP_PASS_DIGITS)) {
    pw = "";
    for (int i = 0; i < AP_PASS_DIGITS; ++i) pw += static_cast<char>('0' + esp_random() % 10);
    p.putString("ap_pass", pw);
  }
  p.end();
  return pw;
}

bool load_board_rev(uint8_t& out) {
  Preferences p;
  p.begin(kNamespace, true);  // read-only open fails (returns false) while the namespace does not exist yet
  const bool have = p.isKey("board_rev");
  out = have ? p.getUChar("board_rev", 0) : 0;
  p.end();
  return have;
}

void store_board_rev(uint8_t rev) {
  Preferences p;
  p.begin(kNamespace, false);
  p.putUChar("board_rev", rev);
  p.end();
}

const char* save_form(Form f) {
  f.ssid.trim();
  f.host.trim();
  f.port.trim();
  uint16_t port = 0;
  const SettingsError e =
      validate_settings(f.ssid.c_str(), f.pass.c_str(), f.host.c_str(), f.port.c_str(), UI_DEFAULT_PORT, port);
  if (e != SettingsError::None) return settings_error_text(e);
  Settings old;
  load(old);
  Settings s;
  s.ssid = f.ssid;
  s.pass = f.pass;
  s.host = f.host;
  s.port = port;
  s.axis_a = f.axis_a;
  s.ota = f.ota;
  // Empty password field with unchanged SSID keeps the stored password (DECISIONS.md).
  if (s.pass.length() == 0 && s.ssid == old.ssid) s.pass = old.pass;
  save(s);
  return nullptr;
}

String html_escape(const String& in) {
  String o;
  o.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

void append_fields(String& html, const Settings& s, const char* datalist_id) {
  html += "<label>WLAN-SSID</label><input name='ssid' required maxlength='32'";
  if (datalist_id != nullptr) html += String(" list='") + datalist_id + "'";
  html += " value='" + html_escape(s.ssid) + "'>";
  html += "<label>WLAN-Passwort</label><input name='pass' type='password' maxlength='63' placeholder='"
          + String(s.pass.length() > 0 ? "unverändert lassen: leer" : "") + "'>";
  html += "<label>Maschinen-IP</label><input name='host' required pattern='\\d{1,3}(\\.\\d{1,3}){3}' value='"
          + html_escape(s.host) + "'>";
  html += "<label>Port</label><input name='port' type='number' min='1' max='65535' value='" + String(s.port) + "'>";
  html += "<label class='chk'><input type='checkbox' name='axis_a' value='1'"
          + String(s.axis_a ? " checked" : "") + "> Drehachse A vorhanden</label>";
  html += "<label class='chk'><input type='checkbox' name='ota' value='1'"
          + String(s.ota ? " checked" : "") + "> Update über WLAN erlauben</label>"
          "<p class='hint'>Firmware-Update per espota, Passwort = Handrad-Passwort. "
          "Nur ohne gewählte Achse.</p>";
}

}  // namespace settings
