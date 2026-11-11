#pragma once
// Web UI in normal mode (SPEC 9.1, ticket #17): read-only status page, /status.json and a
// password-protected settings form. Non-blocking lwIP sockets, two connection slots.
//
// The web layer never talks to the machine: it does not include machine_link.h and has no way
// to send anything there. All state comes through the read-only hooks below.
#include <cstdint>

#include "settings.h"
#include "status_json.h"

namespace webui {

struct Hooks {
  void (*snapshot)(StatusView& out);  // fills a copy of the current state
  bool (*settings_allowed)();          // settings_change_allowed(axis, session) right now
  void (*restart)();                   // after a successful save; does not return
  const settings::Settings& (*config)();  // settings as loaded at boot (equal to NVS: saving restarts)
};

// Opens the listening socket on WEB_PORT and announces WEB_HOSTNAME.local via mDNS.
// Call once WiFi STA has an IP.
void begin(const Hooks& hooks);
bool started();
// One bounded step: at most one accept, and one recv or one send per connection. Never blocks.
void loop(uint32_t now_ms);

}  // namespace webui
