#include "power_switch.h"

PowerSwitch::PowerSwitch(uint32_t debounce_ms) : debounce_ms_(debounce_ms) {}

PowerTick PowerSwitch::update(uint32_t now_ms, bool raw_on) {
  PowerTick t{level_, false};
  if (!have_sample_ || raw_on != candidate_) {
    have_sample_ = true;
    candidate_ = raw_on;
    since_ms_ = now_ms;
  }
  if (static_cast<uint32_t>(now_ms - since_ms_) >= debounce_ms_) {
    const PowerLevel next = candidate_ ? PowerLevel::On : PowerLevel::Off;
    if (next != level_) {
      t.turned_off = next == PowerLevel::Off;
      level_ = next;
    }
  }
  t.level = level_;
  return t;
}

bool sleep_wake_when_on(SleepReason reason, bool switch_on_now) {
  if (reason == SleepReason::SwitchOff) return true;
  return !switch_on_now;
}
