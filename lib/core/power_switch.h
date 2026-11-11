#pragma once
// Soft power: the latching on/off button is an input (SPEC 9.3, ticket #49). Pure logic, no GPIO.
#include <cstdint>

enum class PowerLevel : uint8_t { Unknown, On, Off };

struct PowerTick {
  PowerLevel level;  // debounced level after this sample
  bool turned_off;   // the debounced level became Off in this sample (from Unknown or On), exactly once
};

class PowerSwitch {
 public:
  explicit PowerSwitch(uint32_t debounce_ms);
  // Feeds one raw sample (true = contact closed = on). A level counts once the raw sample stayed the same for
  // debounce_ms since it last changed; until the first such level, level() is Unknown. Shorter bounces never
  // change the debounced level. Time differences as (uint32_t)(now - t), safe across the millis() overflow.
  PowerTick update(uint32_t now_ms, bool raw_on);
  PowerLevel level() const { return level_; }

 private:
  uint32_t debounce_ms_;
  PowerLevel level_ = PowerLevel::Unknown;
  bool have_sample_ = false;
  bool candidate_ = false;
  uint32_t since_ms_ = 0;
};

// Why the handwheel goes into deep sleep (SPEC 9.3).
enum class SleepReason : uint8_t { SwitchOff, BatteryEmpty };

// Which button level wakes the chip again (ext0, level triggered). true = wake when the button is on.
// SwitchOff: always on "on"; if the button is already on again, the chip wakes at once and starts normally.
// BatteryEmpty: the opposite of the current level, so a battery shutdown with the button still on does not
// wake at once; the user switches off (wake, boot sees off, sleeps waiting for on) and on again, and the
// normal start measures the battery again (critical: back to sleep).
bool sleep_wake_when_on(SleepReason reason, bool switch_on_now);
