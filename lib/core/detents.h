#pragma once
// PCNT counts -> whole detents (SPEC 8.2, 8.7 step 2).
#include <cstdint>

class DetentAccumulator {
 public:
  explicit DetentAccumulator(int32_t counts_per_detent) : cpd_(counts_per_detent) {}
  // (1) last counts != 0 at least STEP_RESET_MS ago -> remainder = 0; (2) remainder += counts;
  // (3) detents = remainder / cpd (rounds toward 0); remainder -= detents * cpd.
  int32_t take(uint32_t now_ms, int32_t counts);
  void clear() { rest_ = 0; }
  int32_t remainder() const { return rest_; }

 private:
  int32_t cpd_;
  int32_t rest_ = 0;
  bool have_last_ = false;
  uint32_t last_ms_ = 0;
};
