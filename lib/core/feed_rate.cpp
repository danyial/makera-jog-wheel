#include "feed_rate.h"

static_assert(1000 % FEED_WINDOW_MS == 0, "tempo factor must be an integer");
static_assert(F_MAX_MILLI > 0, "F must never be <= 0");
// The cap of F is >= 1 for every plausible rate: JOG_MAX_SPEED x 1000 >= RATE_MAX for each axis.
static_assert(static_cast<uint64_t>(JOG_MAX_SPEED_UM_S[0]) * 1000 >= RATE_MAX_UM_S[0] &&
                  static_cast<uint64_t>(JOG_MAX_SPEED_UM_S[1]) * 1000 >= RATE_MAX_UM_S[1] &&
                  static_cast<uint64_t>(JOG_MAX_SPEED_UM_S[2]) * 1000 >= RATE_MAX_UM_S[2] &&
                  static_cast<uint64_t>(JOG_MAX_SPEED_UM_S[3]) * 1000 >= RATE_MAX_UM_S[3],
              "F cap would be 0 for a plausible rate");
static_assert(RATE_MIN_UM_S[0] > 0 && RATE_MIN_UM_S[1] > 0 && RATE_MIN_UM_S[2] > 0 && RATE_MIN_UM_S[3] > 0,
              "rate must be > 0");

namespace {

uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

// Rate of the slowest moved axis and the absolute speed limits for a mode. false for None.
bool axis_params(AxisMode axis, const MachineRates& r, uint32_t& rate, uint32_t& vmax, uint32_t& vmin) {
  int a = 0;
  switch (axis) {
    case AxisMode::X: a = 0; break;
    case AxisMode::Y: a = 1; break;
    case AxisMode::XY:
      // The firmware uses the slower of both axes and applies F to the path speed.
      rate = min_u32(r.um_s[0], r.um_s[1]);
      vmax = min_u32(JOG_MAX_SPEED_UM_S[0], JOG_MAX_SPEED_UM_S[1]);
      vmin = min_u32(JOG_MIN_SPEED_UM_S[0], JOG_MIN_SPEED_UM_S[1]);
      return true;
    case AxisMode::Z: a = 2; break;
    case AxisMode::A: a = 3; break;
    default: return false;
  }
  rate = r.um_s[a];
  vmax = JOG_MAX_SPEED_UM_S[a];
  vmin = JOG_MIN_SPEED_UM_S[a];
  return true;
}

// Limits for a known rate; false if the cap would be 0 (excluded for plausible rates by the static_assert).
bool limits(uint32_t rate, uint32_t vmax, uint32_t vmin, int32_t& lo, int32_t& cap) {
  if (rate == 0) return false;
  // Cap rounded down, floor rounded up: F x rate never exceeds JOG_MAX_SPEED.
  uint64_t c = static_cast<uint64_t>(vmax) * 1000 / rate;
  if (c > static_cast<uint64_t>(F_MAX_MILLI)) c = F_MAX_MILLI;
  if (c < 1) return false;
  uint64_t l = (static_cast<uint64_t>(vmin) * 1000 + rate - 1) / rate;
  if (l < 1) l = 1;
  if (l > c) l = c;
  lo = static_cast<int32_t>(l);
  cap = static_cast<int32_t>(c);
  return true;
}

}  // namespace

bool feed_limits(AxisMode axis, const MachineRates& rates, int32_t& lo, int32_t& cap) {
  lo = 0;
  cap = 0;
  uint32_t rate = 0, vmax = 0, vmin = 0;
  if (!rates_cover(rates, axis) || !axis_params(axis, rates, rate, vmax, vmin)) return false;
  if (!limits(rate, vmax, vmin, lo, cap)) {
    lo = 0;
    cap = 0;
    return false;
  }
  return true;
}

int32_t feed_milli(uint32_t v_um_s, AxisMode axis, const MachineRates& rates) {
  int32_t lo = 0;
  int32_t cap = 0;
  uint32_t rate = 0, vmax = 0, vmin = 0;
  if (!rates_cover(rates, axis) || !axis_params(axis, rates, rate, vmax, vmin)) return 0;
  if (!limits(rate, vmax, vmin, lo, cap)) return 0;
  const uint64_t path = axis == AxisMode::XY ? FEED_XY_PATH_PERMILLE : 1000;
  // F_milli = v x lead/100 x path/1000 / rate x 1000 = v x lead x path / (100 x rate), rounded up.
  // Max 2^32 x FEED_LEAD_PCT x 1414 < 2^63 for FEED_LEAD_PCT < 1.5e6: fits in 64 bit.
  const uint64_t num = static_cast<uint64_t>(v_um_s) * FEED_LEAD_PCT * path;
  const uint64_t den = static_cast<uint64_t>(rate) * 100;
  const uint64_t f = (num + den - 1) / den;
  if (f >= static_cast<uint64_t>(cap)) return cap;
  if (f < static_cast<uint64_t>(lo)) return lo;
  return static_cast<int32_t>(f);
}

void FeedEstimator::reset() {
  count_ = 0;
  head_ = 0;
}

uint32_t FeedEstimator::update(uint32_t now_ms, uint32_t want_um) {
  if (want_um > 0) {
    ring_[head_] = Entry{now_ms, want_um};
    head_ = (head_ + 1) % FEED_RING;
    if (count_ < FEED_RING) ++count_;
  }
  uint64_t sum = 0;
  // Newest first; entries are in time order, so the first expired one ends the window. Dropping it and all
  // older ones keeps an ancient entry from looking fresh again when the 32-bit clock wraps.
  for (int i = 0; i < count_; ++i) {
    const Entry& e = ring_[(head_ - 1 - i + FEED_RING) % FEED_RING];
    if (static_cast<uint32_t>(now_ms - e.t_ms) >= FEED_WINDOW_MS) {
      count_ = i;
      break;
    }
    sum += e.um;
  }
  const uint64_t v = sum * (1000 / FEED_WINDOW_MS);
  return v > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(v);
}
