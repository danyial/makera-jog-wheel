#pragma once
// Battery level and critical latch (SPEC 9.3).
#include <cstdint>

class Battery {
 public:
  // mv = battery voltage already scaled x2 (mean of BAT_SAMPLES readings).
  // startup = true for the single reading at boot (one reading < BAT_CRIT_MV is then enough).
  void add_sample(uint16_t mv, bool startup);
  uint8_t percent() const;  // 0..100 in steps of 5
  bool warn() const;        // last reading < BAT_WARN_MV (never while absent)
  bool critical() const { return critical_; }  // latched until restart; never set while absent
  bool absent() const;      // last reading < BAT_ABSENT_LOW_MV or > BAT_ABSENT_HIGH_MV (USB without battery)
  uint16_t last_mv() const { return last_mv_; }

 private:
  uint16_t last_mv_ = 0;
  int low_count_ = 0;
  bool critical_ = false;
};
