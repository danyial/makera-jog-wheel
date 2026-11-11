#pragma once
// Board revision guard (SPEC 9.2, ticket #49): firmware for one carrier board must not drive the pins of the
// other one. Pure logic, no NVS and no GPIO.
#include <cstdint>

enum class BoardRevAction : uint8_t {
  Store,       // nothing stored yet (first start, erased flash): store the compiled revision, run normally
  Run,         // stored == compiled: run normally
  WrongBoard,  // stored != compiled (or unreadable value): safe mode, drive no output
};

// stored_valid: NVS key board_rev present. stored: its value. compiled: revision of this firmware.
BoardRevAction board_rev_action(bool stored_valid, uint8_t stored, uint8_t compiled);

// Safe mode: a deliberate board change is confirmed by holding the board button for hold_ms without a break.
class BoardRevConfirm {
 public:
  explicit BoardRevConfirm(uint32_t hold_ms);
  // pressed = board button. Counting is armed only after the button has been seen released once, so a button
  // held since power-on never confirms. Returns true exactly once, in the first sample at which the button has
  // been held for hold_ms; releasing restarts. Time differences as (uint32_t)(now - t).
  bool update(uint32_t now_ms, bool pressed);

 private:
  uint32_t hold_ms_;
  bool armed_ = false;
  bool holding_ = false;
  bool fired_ = false;
  uint32_t since_ms_ = 0;
};
