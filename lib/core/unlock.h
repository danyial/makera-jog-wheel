#pragma once
// Unlock after Alarm/HALT from the handwheel (SPEC 8.10, ticket #41): holding an axis button for UNLOCK_HOLD_MS
// while the machine reports Alarm sends one "$X". Pure decision logic; main.cpp wires inputs and the link.
#include <cstdint>

#include "status.h"

// Everything the decision needs, evaluated in every tick.
struct UnlockInputs {
  bool normal_mode;        // AppMode Normal (not portal, diagnosis, shutdown)
  bool online;             // MachineLink Online
  bool have_status;        // a status since Online
  MachineStatus status;    // valid only if have_status
  uint32_t status_age_ms;  // now - receive time of the status
  bool ota_updating;       // an OTA transfer has started
  bool hint_visible;       // the display shows the main screen and no alarm/error line has priority (10.1, 10.4)
  bool new_message;        // a new ALARM/error line arrived since the last tick (8.8)
  uint8_t pressed;         // debounced level, bit (1 << Button) for every button that counts (A only if enabled)
  uint8_t edges;           // press edges of this tick after the screensaver (swallowed edges are 0), same bits
};

enum class UnlockView : uint8_t { None, Hint, Holding, Sent };

struct UnlockTick {
  bool send;             // send UNLOCK_TEXT now
  UnlockView view;       // what the display shows
  uint16_t permille;     // Holding: progress 0..1000
  const char* aborted;   // a running hold was cancelled in this tick: reason (serial log), else nullptr
};

class UnlockRequest {
 public:
  // The machine reports Alarm or HALT on a fresh status (SEND_MAX_AGE_MS, as the gate, 8.5 rule 4) and an unlock
  // may be sent: normal mode, Online, no OTA. nullptr if so, otherwise the first reason why not.
  static const char* blocked(const UnlockInputs& in);
  // Once per tick. A hold starts only with a press edge while blocked() == nullptr, the hint is visible and no
  // "Entsperren..." is shown; it belongs to that button and runs while its level stays pressed. It is aborted on
  // release, link loss, no status, OTA, another AppMode, a new alarm/error line or a FRESH status without Alarm;
  // a stale status alone (WiFi stall) does not abort it. send is true once, in a tick with held >= UNLOCK_HOLD_MS,
  // blocked() == nullptr (fresh status with Alarm) and at least UNLOCK_MIN_INTERVAL_MS after the last send; with a
  // stale status at that point it waits. The hold is then spent, a new one needs release and a new press.
  UnlockTick tick(uint32_t now_ms, const UnlockInputs& in);

 private:
  bool holding_ = false;
  uint8_t bit_ = 0;            // button of the running hold
  uint32_t hold_since_ms_ = 0;
  bool have_sent_ = false;
  uint32_t sent_ms_ = 0;
  bool showing_sent_ = false;  // "Entsperren..." until the status leaves Alarm or UNLOCK_SHOW_MS passed
};
