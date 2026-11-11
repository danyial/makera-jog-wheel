#include "machine_rates.h"

static_assert(RATE_RETRY_MS[0] > 0 && RATE_QUERY_TIMEOUT_MS > 0, "retry and timeout must be > 0");

bool rates_cover(const MachineRates& r, AxisMode axis) {
  switch (axis) {
    case AxisMode::X:
    case AxisMode::Y:
    case AxisMode::XY:
    case AxisMode::Z: return r.xyz;
    case AxisMode::A: return r.a;
    default: return false;
  }
}

namespace {

bool is_space(char c) { return c == ' ' || c == '\t'; }

// Non-negative decimal "123" or "16.6667" (the %g output of plausible rates has no sign and no exponent).
// Writes ceil(value x 1000). At most 7 integer and 6 fractional digits.
bool parse_milli_ceil(const char* s, size_t n, size_t& i, uint64_t& out) {
  uint64_t ip = 0;
  int id = 0;
  while (i < n && s[i] >= '0' && s[i] <= '9') {
    if (++id > 7) return false;
    ip = ip * 10 + static_cast<uint64_t>(s[i] - '0');
    ++i;
  }
  uint64_t fp = 0;
  uint64_t scale = 1;
  int fd = 0;
  if (i < n && s[i] == '.') {
    ++i;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
      if (++fd > 6) return false;
      fp = fp * 10 + static_cast<uint64_t>(s[i] - '0');
      scale *= 10;
      ++i;
    }
  }
  if (id + fd == 0) return false;
  if (i < n && !is_space(s[i])) return false;  // e.g. "1e+06", "50,0", "50x"
  out = ip * 1000 + (fp * 1000 + scale - 1) / scale;
  return true;
}

}  // namespace

RateLine parse_rate_line(const char* s, size_t n, MachineRates& out) {
  size_t i = 0;
  while (i < n && is_space(s[i])) ++i;
  if (i + 1 >= n || s[i] != 'X' || s[i + 1] != ':') return RateLine::NotRates;
  MachineRates r{};
  bool seen[4] = {false, false, false, false};
  while (i < n) {
    const char letter = s[i];
    if (i + 1 >= n || s[i + 1] != ':') return RateLine::Invalid;
    i += 2;
    while (i < n && is_space(s[i])) ++i;
    uint64_t v = 0;
    if (!parse_milli_ceil(s, n, i, v)) return RateLine::Invalid;
    int a = -1;
    switch (letter) {
      case 'X': a = 0; break;
      case 'Y': a = 1; break;
      case 'Z': a = 2; break;
      case 'A': a = 3; break;
      case 'B':
      case 'C': break;  // other actuators (Z1 Pro: B = tool changer), not jogged
      default: return RateLine::Invalid;  // also "S:" of plain M203 (cartesian speeds)
    }
    if (a >= 0) {
      if (seen[a] || v < RATE_MIN_UM_S[a] || v > RATE_MAX_UM_S[a]) return RateLine::Invalid;
      seen[a] = true;
      r.um_s[a] = static_cast<uint32_t>(v);
    }
    while (i < n && is_space(s[i])) ++i;
  }
  if (!seen[0] || !seen[1] || !seen[2]) return RateLine::Invalid;
  r.xyz = true;
  r.a = seen[3];
  out = r;
  return RateLine::Valid;
}

bool RateQuery::ready(const MachineStatus* status) {
  return status != nullptr && status->state != MachineState::Alarm && !status->halted;
}

bool RateQuery::refresh_allowed(const MachineStatus* status, bool jog_active) {
  return ready(status) && status->state == MachineState::Idle && !jog_active;
}

void RateQuery::reset() {
  phase_ = Phase::Start;
  retry_idx_ = 0;
  queries_ = 0;
  refresh_failures_ = 0;
  rates_ = MachineRates{};
}

void RateQuery::retry_later(uint32_t now_ms) {
  phase_ = Phase::Wait;
  since_ms_ = now_ms;
  wait_ms_ = RATE_RETRY_MS[retry_idx_ < 2 ? retry_idx_ : 2];
  if (retry_idx_ < 2) ++retry_idx_;
}

void RateQuery::refresh_failed(uint32_t now_ms) {
  phase_ = Phase::Done;  // the old, valid rates stay
  since_ms_ = now_ms;
  ++refresh_failures_;
}

bool RateQuery::poll(uint32_t now_ms, bool ready, bool refresh_ok) {
  switch (phase_) {
    case Phase::Done:
      if (!refresh_ok || static_cast<uint32_t>(now_ms - since_ms_) < RATE_REFRESH_MS) return false;
      phase_ = Phase::Refresh;
      since_ms_ = now_ms;
      ++queries_;
      return true;
    case Phase::Refresh:
      if (static_cast<uint32_t>(now_ms - since_ms_) >= RATE_QUERY_TIMEOUT_MS) refresh_failed(now_ms);
      return false;
    case Phase::InFlight:
      if (static_cast<uint32_t>(now_ms - since_ms_) >= RATE_QUERY_TIMEOUT_MS) retry_later(now_ms);
      return false;
    case Phase::Wait:
      if (!ready) {  // e.g. alarm: ask again as soon as the machine may be asked
        phase_ = Phase::Start;
        retry_idx_ = 0;
        return false;
      }
      if (static_cast<uint32_t>(now_ms - since_ms_) < wait_ms_) return false;
      break;
    case Phase::Start: break;
  }
  if (!ready) return false;
  phase_ = Phase::InFlight;
  since_ms_ = now_ms;
  ++queries_;
  return true;
}

RateLine RateQuery::on_line(const char* line, size_t n, uint32_t now_ms) {
  if (phase_ == Phase::Done) return RateLine::NotRates;
  MachineRates r{};
  const RateLine res = parse_rate_line(line, n, r);
  if (phase_ == Phase::Refresh) {
    if (res == RateLine::Valid) {
      rates_ = r;
      phase_ = Phase::Done;
      since_ms_ = now_ms;
      refresh_failures_ = 0;
    } else if (res == RateLine::Invalid) {
      refresh_failed(now_ms);
    }
    return res;
  }
  if (res == RateLine::Valid) {
    rates_ = r;
    phase_ = Phase::Done;
    since_ms_ = now_ms;
  } else if (res == RateLine::Invalid && phase_ == Phase::InFlight) {
    retry_later(now_ms);
  }
  return res;
}
