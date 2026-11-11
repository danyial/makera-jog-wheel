#include "jog_planner.h"

#include <cstring>

#include "core_config.h"
#include "fixed_fmt.h"

static_assert(STEP_UM[0] >= ARRIVE_UM, "every sent block must be >= ARRIVE_UM (8.7 e2)");

namespace {

// Distance in 64 bit: absurd reported positions must not overflow the 32-bit subtraction.
int64_t dist_um(int32_t a, int32_t b) {
  const int64_t d = static_cast<int64_t>(a) - static_cast<int64_t>(b);
  return d < 0 ? -d : d;
}

// Axes moved by a mode: X=0, Y=1, Z=2, A=3.
int axes_of(AxisMode m, int out[2]) {
  switch (m) {
    case AxisMode::X: out[0] = 0; return 1;
    case AxisMode::Y: out[0] = 1; return 1;
    case AxisMode::XY: out[0] = 0; out[1] = 1; return 2;
    case AxisMode::Z: out[0] = 2; return 1;
    case AxisMode::A: out[0] = 3; return 1;
    default: return 0;
  }
}

// Axes that count for consistency and arrival: A only when the status reports it.
int axis_count(const MachineStatus& s) { return s.has_a ? 4 : 3; }

void append(char* buf, size_t cap, size_t& len, const char* s) {
  while (*s && len + 1 < cap) buf[len++] = *s++;
  buf[len] = '\0';
}

}  // namespace

bool JogPlanner::start_allowed(uint32_t now_ms, const MachineStatus& s, uint32_t age) const {
  if (s.state != MachineState::Idle) return false;
  if (age > START_MAX_AGE_MS) return false;
  // The status must have been received after the last session end, with a gap.
  if (have_end_ && age + START_GAP_MS > static_cast<uint32_t>(now_ms - last_end_ms_)) return false;
  return true;
}

void JogPlanner::finish(uint32_t now_ms) {
  active_ = false;
  in_hold_ = false;
  axis_ = AxisMode::None;
  dir_ = 0;
  have_end_ = true;
  last_end_ms_ = now_ms;
  last_cmd_pending_ = false;
  pending_um_ = 0;  // collected detents never survive the session (8.7 e1)
  feed_.reset();    // a tempo from before must not set F on the next session
  sent_head_ = 0;
  sent_count_ = 0;
  rates_ = next_rates_;  // a re-read during the session applies from the next one
}

void JogPlanner::end_session(uint32_t now_ms) { finish(now_ms); }

void JogPlanner::set_rates(const MachineRates& rates) {
  next_rates_ = rates;
  if (!active_ || !rates_cover(rates, axis_)) rates_ = rates;
}

// (g) Session end while a session is active.
void JogPlanner::check_end(uint32_t now_ms, const MachineStatus& s) {
  if (!active_) return;
  const uint32_t idle = static_cast<uint32_t>(now_ms - last_send_ms_);
  bool arrived = true;
  for (int a = 0; a < axis_count(s); ++a) {
    if (dist_um(target_um_[a], s.mpos_um[a]) > ARRIVE_UM) arrived = false;
  }
  if ((idle >= SESSION_IDLE_MS && arrived) || idle >= SESSION_MAX_IDLE_MS) finish(now_ms);
}

// (e2) The machine executes the blocks in send order, so once a block is done, all older ones are too.
// A block counts as done only if a status received at least START_GAP_MS after it was sent (so its content
// stems from after the machine got the $J, same reasoning as c.3) shows mpos within ARRIVE_UM of its end
// in its own direction (or beyond). The newest such block is dropped together with all older ones.
// Without such a status every block stays counted. Because every sent block is at least ARRIVE_UM long
// (STEP_UM >= ARRIVE_UM, and emit() drops window-clipped blocks below ARRIVE_UM), the count is at most one
// block too low (the newest dropped one, at most ARRIVE_UM left), right after a reversal at most two (plus the
// last block of the old direction): the price of tolerating step rounding of the reported position.
int JogPlanner::queued_blocks(uint32_t now_ms, const MachineStatus& s, uint32_t age) {
  int ax[2];
  if (axes_of(axis_, ax) == 0) return 0;
  for (int k = sent_count_ - 1; k >= 0; --k) {
    const Sent& e = sent_[(sent_head_ + k) % JOG_MAX_QUEUED];
    const bool late = static_cast<uint64_t>(age) + START_GAP_MS <= static_cast<uint32_t>(now_ms - e.sent_ms);
    const int64_t ahead = (static_cast<int64_t>(e.end_um) - s.mpos_um[ax[0]]) * e.dir;
    if (late && ahead <= ARRIVE_UM) {
      sent_head_ = (sent_head_ + k + 1) % JOG_MAX_QUEUED;
      sent_count_ -= k + 1;
      break;
    }
  }
  return sent_count_;
}

void JogPlanner::notify_send_failed() {
  if (!active_) return;
  if (last_cmd_pending_) {
    int ax[2];
    const int n = axes_of(axis_, ax);
    for (int i = 0; i < n; ++i) target_um_[ax[i]] -= dir_ * last_move_um_;
  }
  // Called in the same loop pass as the tick that produced the command (SPEC 8.7 step 6).
  finish(last_tick_ms_);
}

bool JogPlanner::session_consistent(const MachineStatus& s) const {
  if (!active_) return false;
  for (int a = 0; a < axis_count(s); ++a) {
    if (dist_um(target_um_[a], s.mpos_um[a]) > WINDOW_UM[a] + FOREIGN_TOL_UM) return false;
  }
  return true;
}

// (e) Window on the collected amount, (f) command.
void JogPlanner::emit(PlannerTickResult& r, const MachineStatus& s, int32_t want, uint32_t v_um_s) {
  int ax[2];
  const int n_ax = axes_of(axis_, ax);
  int32_t move = want;
  for (int i = 0; i < n_ax; ++i) {
    const int a = ax[i];
    int64_t allowed = WINDOW_UM[a] - dist_um(target_um_[a], s.mpos_um[a]);
    if (allowed < 0) allowed = 0;
    if (allowed < move) move = static_cast<int32_t>(allowed);
  }
  if (move < want) r.clipped = true;  // the excess is discarded
  // A block cut by the window below ARRIVE_UM is dropped too: every sent block stays >= ARRIVE_UM, which keeps
  // the (e2) count at most one block too low.
  if (move <= 0 || (move < want && move < ARRIVE_UM)) return;
  // F in thousandths within feed_limits() (8.7 f); format_um writes value/1000 with exactly 3 decimals, which
  // is the F format. Without rates for the axis there is no F and no command (tick() already ended the
  // session in (a); defence only).
  const int32_t f_milli = feed_milli(v_um_s, axis_, rates_);
  if (f_milli <= 0) return;
  char num[16];
  format_um(dir_ * move, num, sizeof(num));
  size_t len = 0;
  append(r.cmd.text, sizeof(r.cmd.text), len, "$J");
  static const char* const kName[4] = {" X", " Y", " Z", " A"};
  for (int i = 0; i < n_ax; ++i) {
    append(r.cmd.text, sizeof(r.cmd.text), len, kName[ax[i]]);
    append(r.cmd.text, sizeof(r.cmd.text), len, num);
  }
  char fnum[16];
  format_um(f_milli, fnum, sizeof(fnum));
  append(r.cmd.text, sizeof(r.cmd.text), len, " F");
  append(r.cmd.text, sizeof(r.cmd.text), len, fnum);
  r.cmd.valid = true;
  for (int i = 0; i < n_ax; ++i) target_um_[ax[i]] += dir_ * move;
  last_send_ms_ = last_tick_ms_;
  last_cmd_pending_ = true;
  last_move_um_ = move;
  // Remember the block for (e2); the caller only sends while sent_count_ < JOG_MAX_QUEUED.
  if (sent_count_ < JOG_MAX_QUEUED) {
    sent_[(sent_head_ + sent_count_) % JOG_MAX_QUEUED] = Sent{dir_, target_um_[ax[0]], last_tick_ms_};
    ++sent_count_;
  }
}

PlannerTickResult JogPlanner::tick(uint32_t now_ms, int32_t detents, int32_t step_um, AxisMode axis,
                                   BlockReason gate, const MachineStatus* status, uint32_t status_age_ms) {
  PlannerTickResult r{};
  r.cmd.valid = false;
  r.cmd.text[0] = '\0';
  last_cmd_pending_ = false;
  last_tick_ms_ = now_ms;

  // (a0) Short data gap during a session: hold (no command, collected detents discarded), do not end;
  // (g) still applies.
  if (active_ && gate == BlockReason::NoData && status != nullptr && status_age_ms <= NODATA_HOLD_MS &&
      axis == axis_) {
    if (!in_hold_) {
      in_hold_ = true;
      hold_since_ms_ = now_ms;
    }
    pending_um_ = 0;
    check_end(now_ms, *status);
    r.session_active = active_;
    return r;
  }
  // (a1) Resume after a hold only on a status received START_GAP_MS after the hold began, so a
  // report that was delayed in the gap (content from before it) cannot release a command.
  if (active_ && in_hold_ && gate == BlockReason::None && status != nullptr && axis == axis_) {
    if (status_age_ms + START_GAP_MS > static_cast<uint32_t>(now_ms - hold_since_ms_)) {
      pending_um_ = 0;
      check_end(now_ms, *status);
      r.session_active = active_;
      return r;
    }
    in_hold_ = false;
  }

  // (a) Blocked, no status, axis differs from the session's axis, or no machine rates for the axis (8.5 rule
  // 7a, second safeguard) -> end session (discards collected detents), no command.
  if (gate != BlockReason::None || status == nullptr || (active_ && axis != axis_) || !rates_cover(rates_, axis)) {
    if (active_) finish(now_ms);
    r.session_active = active_;
    return r;
  }
  const MachineStatus& s = *status;
  int ax[2];
  const int n_ax = axes_of(axis, ax);

  // want = |detents| x step_um of this tick, saturated.
  const uint32_t abs_det = detents < 0 ? 0u - static_cast<uint32_t>(detents) : static_cast<uint32_t>(detents);
  const uint64_t want64 = step_um > 0 ? static_cast<uint64_t>(abs_det) * static_cast<uint32_t>(step_um) : 0;
  const int32_t want = want64 > INT32_MAX ? INT32_MAX : static_cast<int32_t>(want64);

  uint32_t v_um_s = 0;
  if (detents != 0 && n_ax > 0 && step_um > 0) {  // (b)
    const int8_t sign = detents > 0 ? 1 : -1;
    bool take = true;
    if (!active_) {
      // (c) Session start.
      if (start_allowed(now_ms, s, status_age_ms)) {
        active_ = true;
        axis_ = axis;
        for (int a = 0; a < 4; ++a) target_um_[a] = s.mpos_um[a];
        dir_ = sign;
        last_send_ms_ = now_ms;
        pending_um_ = 0;
        feed_.reset();
        sent_head_ = 0;
        sent_count_ = 0;
      } else {
        take = false;
      }
    } else if (sign != dir_) {
      // (d) Direction lock: collected detents of the old direction are discarded; reverse only when the
      // old direction is worked off on all involved axes.
      if (pending_um_ > 0) {
        pending_um_ = 0;
        r.clipped = true;
      }
      bool done = true;
      for (int i = 0; i < n_ax; ++i) {
        if (dist_um(target_um_[ax[i]], s.mpos_um[ax[i]]) > REVERSAL_EPS_UM) done = false;
      }
      if (done) {
        dir_ = sign;
      } else {
        r.clipped = true;
        take = false;
      }
    }
    // Tempo (8.7 f): every tick of a session, from want before window and lock.
    if (active_) v_um_s = feed_.update(now_ms, static_cast<uint32_t>(want));
    if (take) {
      // (e1) Collect. Never more than the smallest window of the involved axes; the excess is discarded.
      int32_t limit = INT32_MAX;
      for (int i = 0; i < n_ax; ++i) limit = WINDOW_UM[ax[i]] < limit ? WINDOW_UM[ax[i]] : limit;
      if (pending_um_ == 0) pending_since_ms_ = now_ms;
      const int64_t sum = static_cast<int64_t>(pending_um_) + want;
      if (sum > limit) r.clipped = true;
      pending_um_ = sum > limit ? limit : static_cast<int32_t>(sum);
    }
  } else if (active_) {
    v_um_s = feed_.update(now_ms, 0);
  }

  // (e1) Send the collected detents at once while nothing is queued (machine surely idle), otherwise in
  // the tick BUNDLE_MS after the first collected detent. (e2) Never more than JOG_MAX_QUEUED blocks.
  // The count is updated in every tick of a session (it only drops confirmed blocks).
  const int queued = active_ ? queued_blocks(now_ms, s, status_age_ms) : 0;
  if (active_ && pending_um_ > 0) {
    const bool due = queued == 0 || static_cast<uint32_t>(now_ms - pending_since_ms_) >= BUNDLE_MS;
    if (due && queued < JOG_MAX_QUEUED) {
      const int32_t amount = pending_um_;
      pending_um_ = 0;
      emit(r, s, amount, v_um_s);
    }
  }

  check_end(now_ms, s);
  r.session_active = active_;
  return r;
}
