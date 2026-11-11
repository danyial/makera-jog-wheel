#include "battery.h"

#include "core_config.h"

bool Battery::absent() const { return last_mv_ < BAT_ABSENT_LOW_MV || last_mv_ > BAT_ABSENT_HIGH_MV; }

void Battery::add_sample(uint16_t mv, bool startup) {
  last_mv_ = mv;
  if (absent()) {
    low_count_ = 0;  // no battery: never critical
    return;
  }
  if (mv < BAT_CRIT_MV) {
    ++low_count_;
    if (startup || low_count_ >= BAT_CRIT_COUNT) critical_ = true;
  } else {
    low_count_ = 0;
  }
}

uint8_t Battery::percent() const {
  if (last_mv_ <= BAT_EMPTY_MV) return 0;
  if (last_mv_ >= BAT_FULL_MV) return 100;
  // Number of 5 % steps, rounded to nearest: (mv - empty) * 20 / (full - empty).
  const uint32_t span = BAT_FULL_MV - BAT_EMPTY_MV;
  const uint32_t steps = ((last_mv_ - BAT_EMPTY_MV) * 20u + span / 2) / span;
  return static_cast<uint8_t>(steps * 5);
}

bool Battery::warn() const { return !absent() && last_mv_ < BAT_WARN_MV; }
