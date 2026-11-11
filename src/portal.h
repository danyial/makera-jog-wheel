#pragma once
// Captive configuration portal (SPEC 9.1). Settings storage: settings.h.
#include <Arduino.h>

#include "settings.h"

namespace portal {

// Starts AP "Makera-Handrad-XXXX", DNS catch-all and web server. on_diag is called for
// "Diagnose starten" (sets the RTC diag request and restarts).
void begin(void (*on_diag)());
// Serves requests; restarts after PORTAL_TIMEOUT_MS without a page request when credentials exist.
void loop(uint32_t now_ms);
const char* ap_ssid();
const char* ap_password();

}  // namespace portal
