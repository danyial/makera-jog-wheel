#pragma once
// OTA firmware update over WiFi with ArduinoOTA/espota (SPEC 9.5, ticket #35). Normal mode only,
// only with the setting "Update über WLAN erlauben", password = ap_pass.
//
// ArduinoOTA does the invitation, the password check and the transfer in different handle() calls,
// and the transfer (_runUpdate) blocks inside handle() for the whole upload. The caller therefore
// calls step() in every loop pass, after tick and display, with the acceptance rule evaluated right
// before: while it does not hold, the UDP listener is closed (ArduinoOTA.end()), so no invitation or
// half-finished authentication survives into a later pass, and handle() never runs.
#include <cstdint>

namespace ota {

struct Hooks {
  bool (*allowed)();  // ota_update_allowed(axis, session, battery warn) right now
  void (*stop)();     // end the jog session and close the machine socket for good (no more $J)
};

// Call once WiFi STA has an IP and webui::begin() has started mDNS. Adds the _arduino._tcp service
// to the existing mDNS responder (ArduinoOTA's own MDNS.begin() stays off).
void begin(const char* password, const Hooks& hooks);
bool started();
// An update transfer has started (onStart ran); the handwheel restarts afterwards in any case.
bool updating();

// One loop step. allowed_now is hooks.allowed() evaluated in this pass, after the tick, with nothing
// in between. Opens or closes the listener and runs ArduinoOTA.handle() only while allowed_now holds
// and no wrong-password lock (OTA_AUTH_LOCK_MS) is running. A UDP port that cannot be bound is retried
// every OTA_LISTEN_RETRY_MS.
// Once an update has started, this does not return: success or failure, the handwheel restarts.
void step(bool allowed_now);

}  // namespace ota
