#pragma once
// Feed of $J from the wheel tempo (SPEC 8.7 f, tickets #32, #38).
#include <cstdint>

#include "axis_select.h"
#include "core_config.h"
#include "machine_rates.h"

// Limits of F in thousandths for `axis` with the machine's rates (8.7 f, ticket #38), from the absolute
// speed limits: cap = min(F_MAX_MILLI, floor(JOG_MAX_SPEED x 1000 / rate)), lo = min(cap, max(1,
// ceil(JOG_MIN_SPEED x 1000 / rate))); rate = the slowest moved axis (XY: min of X and Y; the XY limits
// apply to the path). Returns false (lo = cap = 0) if rates_cover(rates, axis) is false.
bool feed_limits(AxisMode axis, const MachineRates& rates, int32_t& lo, int32_t& cap);

// F in thousandths for a tempo of v_um_s per axis (A: millidegrees/s):
// ceil(v x FEED_LEAD_PCT/100 [x sqrt(2) for XY] / rate x 1000), clamped to feed_limits(). Pure function.
// Returns 0 (no command may be built) if the rates do not cover the axis, otherwise always >= 1.
int32_t feed_milli(uint32_t v_um_s, AxisMode axis, const MachineRates& rates);

// Wheel tempo in um/s per axis: sum of want_um of all ticks in the last FEED_WINDOW_MS (including the
// current one), divided by the window. Ring of FEED_RING entries; when it overflows the oldest entries are
// lost, which only lowers the estimate.
class FeedEstimator {
 public:
  // Call exactly once per tick; want_um = |detents| x step_um of this tick (before window and lock).
  uint32_t update(uint32_t now_ms, uint32_t want_um);
  void reset();

 private:
  struct Entry {
    uint32_t t_ms;
    uint32_t um;
  };
  Entry ring_[FEED_RING]{};
  int count_ = 0;  // valid entries
  int head_ = 0;   // next write position
};
