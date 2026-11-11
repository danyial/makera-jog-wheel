#include "step_policy.h"

static_assert(1000 % SPEED_WINDOW_MS == 0, "speed factor must be an integer");

uint8_t StepPolicy::level_for(uint32_t v, uint8_t prev_level) {
  uint8_t l = prev_level > 2 ? 2 : prev_level;
  while (l < 2 && v >= LEVEL_UP_V[l + 1]) ++l;
  while (l > 0 && v < LEVEL_DOWN_V[l]) --l;
  return l;
}

void StepPolicy::reset() {
  count_ = 0;
  head_ = 0;
  level_ = 0;
  have_last_ = false;
  t_last_ = 0;
}

int32_t StepPolicy::update(uint32_t now_ms, uint32_t abs_detents) {
  // Pause check happens BEFORE recording this tick, otherwise the old speed would raise the level again.
  if (have_last_ && static_cast<uint32_t>(now_ms - t_last_) >= STEP_RESET_MS) {
    level_ = 0;
    count_ = 0;
    head_ = 0;
  }
  if (abs_detents > 0) {
    ring_[head_] = Entry{now_ms, abs_detents};
    head_ = (head_ + 1) % SPEED_RING;
    if (count_ < SPEED_RING) ++count_;
    have_last_ = true;
    t_last_ = now_ms;
  }
  uint32_t sum = 0;
  for (int i = 0; i < count_; ++i) {
    const Entry& e = ring_[(head_ - 1 - i + SPEED_RING) % SPEED_RING];
    if (static_cast<uint32_t>(now_ms - e.t_ms) < SPEED_WINDOW_MS) sum += e.detents;
  }
  const uint32_t v = sum * (1000 / SPEED_WINDOW_MS);
  const uint8_t target = level_for(v, level_);
  level_ = target > level_ ? static_cast<uint8_t>(level_ + 1) : target;  // up one level per tick at most
  return STEP_UM[level_];
}
