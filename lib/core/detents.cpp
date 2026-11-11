#include "detents.h"

#include "core_config.h"

int32_t DetentAccumulator::take(uint32_t now_ms, int32_t counts) {
  if (have_last_ && static_cast<uint32_t>(now_ms - last_ms_) >= STEP_RESET_MS) rest_ = 0;
  if (counts != 0) {
    have_last_ = true;
    last_ms_ = now_ms;
  }
  rest_ += counts;
  const int32_t detents = rest_ / cpd_;
  rest_ -= detents * cpd_;
  return detents;
}
