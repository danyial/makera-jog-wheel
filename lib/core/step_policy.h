#pragma once
// Step size from wheel speed (SPEC 8.6).
#include <cstdint>

#include "core_config.h"

class StepPolicy {
 public:
  // Pure function, table 8.6.
  static uint8_t level_for(uint32_t v_detents_per_s, uint8_t prev_level);
  // Call exactly once per tick; abs_detents = |detents of this tick|. Returns um per detent.
  int32_t update(uint32_t now_ms, uint32_t abs_detents);
  uint8_t level() const { return level_; }
  void reset();

 private:
  struct Entry {
    uint32_t t_ms;
    uint32_t detents;
  };
  Entry ring_[SPEED_RING]{};
  int count_ = 0;  // valid entries
  int head_ = 0;   // next write position
  uint8_t level_ = 0;
  bool have_last_ = false;
  uint32_t t_last_ = 0;  // last tick with abs_detents > 0
};
