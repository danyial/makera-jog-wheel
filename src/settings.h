#pragma once
// Settings in NVS (SPEC 9.1), shared by the captive portal and the web UI in normal mode.
#include <Arduino.h>

namespace settings {

struct Settings {
  String ssid;
  String pass;
  String host;
  uint16_t port;
  bool axis_a;  // optional rotary axis A present (ticket #14)
  bool ota;     // OTA update over WiFi allowed (ticket #35), default off
};

// Loads ssid/pass/host/port. Returns true when credentials are complete (ssid and host set).
bool load(Settings& out);
// AP password, generated (AP_PASS_DIGITS random digits) and stored on first use.
String ap_pass();

// Board revision guard (SPEC 9.2, ticket #49): NVS key board_rev. Returns false if it is not stored.
bool load_board_rev(uint8_t& out);
void store_board_rev(uint8_t rev);

// Raw form fields as submitted (not yet trimmed or validated).
struct Form {
  String ssid;
  String pass;
  String host;
  String port;
  bool axis_a;
  bool ota;
};

// Validates the form (lib/core settings_rules), keeps the stored WiFi password for an empty
// password field with unchanged SSID (DECISIONS.md) and saves to NVS.
// Returns nullptr when saved, otherwise the error text; nothing is saved then.
const char* save_form(Form f);

// Common page start (doctype, viewport, shared style, header "Handrad", opens <main>), in flash.
// Pages end with "</main></body></html>". Look defined in web_pages.h (ticket #36).
extern const char kPageHead[];

// HTML-escapes text for attribute values and element content.
String html_escape(const String& in);

// Appends the shared input fields (SSID, WiFi password, machine IP, port, axis A, OTA) prefilled with s.
// The stored WiFi password is never sent. datalist_id names a <datalist> for the SSID or nullptr.
void append_fields(String& html, const Settings& s, const char* datalist_id);

}  // namespace settings
