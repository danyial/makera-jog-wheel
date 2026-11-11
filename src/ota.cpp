#include "ota.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <esp_system.h>

#include "core_config.h"
#include "display.h"
#include "settings_rules.h"
#include "ui_config.h"

namespace ota {
namespace {

Hooks hooks{};
bool running = false;         // begin() done
bool listening = false;       // ArduinoOTA.begin() active (UDP port open)
bool update_started = false;  // onStart seen: from here on the handwheel restarts in any case
bool success = false;         // onEnd seen: image written and verified, boot partition switched
int last_percent = -1;
int last_logged_tens = -1;
// Wrong password: every check costs a full PBKDF2 (10 000 rounds) inside handle(). After a failure the
// listener stays closed for OTA_AUTH_LOCK_MS, so a LAN client cannot stall the loop back to back.
AuthThrottle auth_lock(OTA_AUTH_LOCK_MS);
bool auth_failed = false;  // set by on_error inside handle(), turned into the lock after it returns
// ArduinoOTA.begin() reports a failed UDP bind only via log_e. Probe the port first, retry throttled.
bool have_listen_try = false;
uint32_t last_listen_try_ms = 0;
bool listen_fail_logged = false;

const char* error_text(ota_error_t e) {
  switch (e) {
    case OTA_AUTH_ERROR: return "Passwort falsch";
    case OTA_BEGIN_ERROR: return "Update.begin fehlgeschlagen";
    case OTA_CONNECT_ERROR: return "keine Verbindung zum Rechner";
    case OTA_RECEIVE_ERROR: return "Empfang abgebrochen";
    case OTA_END_ERROR: return "Abschluss/Pruefung fehlgeschlagen";
  }
  return "unbekannt";
}

// Safe initial state after every update that started, successful or not. Without a successful
// Update.end() the boot partition was not switched, so the old firmware boots again.
[[noreturn]] void restart(const char* line) {
  Serial.printf("OTA: Neustart (%s)\n", line);
  display::show_ota(line, -1);
  delay(UI_OTA_RESULT_SHOW_MS);
  esp_restart();
  while (true) {
  }
}

// Called by ArduinoOTA inside handle(), after Update.begin() and before the TCP transfer.
void on_start() {
  update_started = true;
  const bool ok = hooks.allowed();  // re-check before anything else; must hold (checked in this pass)
  hooks.stop();                     // no more $J, machine socket closed in order, in every case
  if (!ok) {
    Serial.println("OTA: abgebrochen, Achse gewaehlt oder Sitzung aktiv");
    restart("Abgebrochen");
  }
  if (ArduinoOTA.getCommand() != U_FLASH) {  // only the firmware, never the filesystem partition
    Serial.println("OTA: abgelehnt, nur Firmware-Updates (kein Dateisystem)");
    restart("Abgelehnt");
  }
  Serial.println("OTA: Update startet, Verbindung zur Maschine getrennt");
  last_percent = -1;
  last_logged_tens = -1;
  display::show_ota("Empfange ...", 0);
}

void on_progress(unsigned int done, unsigned int total) {
  if (total == 0) return;
  const int percent = static_cast<int>(static_cast<uint64_t>(done) * 100u / total);
  if (percent / 10 != last_logged_tens) {
    last_logged_tens = percent / 10;
    Serial.printf("OTA: %d %%\n", percent);
  }
  // Each frame takes about 23 ms over I2C: only on a change and at most DISPLAY_MAX_HZ.
  if (percent != last_percent && display::due(millis())) {
    last_percent = percent;
    display::show_ota("Empfange ...", percent);
  }
}

void on_end() {
  success = true;
  Serial.println("OTA: Update vollstaendig und geprueft");
}

void on_error(ota_error_t e) {
  if (update_started) {
    Serial.printf("OTA: Fehler (%s)\n", error_text(e));
  } else {
    Serial.printf("OTA: abgelehnt (%s)\n", error_text(e));  // before onStart: nothing changed, no restart
    if (e == OTA_AUTH_ERROR) auth_failed = true;
  }
}

// Same bind as ArduinoOTA.begin() (dual stack with CONFIG_LWIP_IPV6). If it fails here, begin() would
// fail too and leave ArduinoOTA uninitialized without telling us. Nothing else binds OTA_PORT, and
// begin() follows directly in the same pass, so the probe result holds.
bool port_free() {
  NetworkUDP probe;
#if CONFIG_LWIP_IPV6
  const bool ok = probe.begin(IN6ADDR_ANY, OTA_PORT);
#else
  const bool ok = probe.begin(OTA_PORT);
#endif
  probe.stop();
  return ok;
}

// Opens the listener; false (retry later) if the UDP port cannot be bound.
bool open_listener(uint32_t now) {
  if (have_listen_try && static_cast<uint32_t>(now - last_listen_try_ms) < OTA_LISTEN_RETRY_MS) return false;
  have_listen_try = true;
  last_listen_try_ms = now;
  if (!port_free()) {
    if (!listen_fail_logged) {
      Serial.printf("OTA: UDP-Port %u nicht geoeffnet, neuer Versuch alle %lu ms\n", static_cast<unsigned>(OTA_PORT),
                    static_cast<unsigned long>(OTA_LISTEN_RETRY_MS));
      listen_fail_logged = true;
    }
    return false;
  }
  ArduinoOTA.begin();
  if (listen_fail_logged) Serial.printf("OTA: UDP-Port %u offen\n", static_cast<unsigned>(OTA_PORT));
  listen_fail_logged = false;
  have_listen_try = false;  // the next reopen (after a block) is not throttled
  return true;
}

}  // namespace

void begin(const char* password, const Hooks& h) {
  hooks = h;
  ArduinoOTA.setPort(OTA_PORT)
      .setHostname(WEB_HOSTNAME)
      .setPassword(password)     // stored as SHA-256; espota authenticates with PBKDF2 challenge/response
      .setMdnsEnabled(false)     // webui owns MDNS.begin(); a second begin() would fail (mdns_init twice)
      .setRebootOnSuccess(false)  // restart is done by step(), after the result is shown
      .onStart(on_start)
      .onProgress(on_progress)
      .onEnd(on_end)
      .onError(on_error);
  MDNS.enableArduino(OTA_PORT, true);  // _arduino._tcp on the existing responder, auth_upload=yes
  running = true;
  Serial.printf("OTA: erlaubt, espota an %s.local Port %u (nur ohne gewaehlte Achse)\n", WEB_HOSTNAME,
                static_cast<unsigned>(OTA_PORT));
}

bool started() { return running; }
bool updating() { return update_started; }

void step(bool allowed_now) {
  if (!running) return;
  // millis(), not the pass time: the lock is set after a handle() that may have taken a second.
  if (auth_lock.locked(millis())) allowed_now = false;
  if (!allowed_now) {
    // Close the UDP port: an invitation or authentication from now on goes unanswered (espota fails),
    // and ArduinoOTA.end() resets its state to IDLE, so nothing half-done carries over.
    if (listening) {
      ArduinoOTA.end();
      listening = false;
    }
    return;
  }
  if (!listening) {
    if (!open_listener(millis())) return;
    listening = true;
  }
  const uint32_t t0 = millis();
  ArduinoOTA.handle();  // may block for the whole transfer, but only in a pass where allowed_now held
  const uint32_t dt = millis() - t0;
  if (update_started) restart(success ? "Fertig, Neustart" : "Fehlgeschlagen");
  if (auth_failed) {
    auth_failed = false;
    auth_lock.on_fail(millis());
    ArduinoOTA.end();  // closed right away; reopened by a later step() once the lock has expired
    listening = false;
    Serial.printf("OTA: nach falschem Passwort %lu ms gesperrt\n", static_cast<unsigned long>(OTA_AUTH_LOCK_MS));
  }
  if (dt > OTA_SLOW_HANDLE_MS) Serial.printf("OTA: handle() dauerte %lu ms\n", static_cast<unsigned long>(dt));
}

}  // namespace ota
