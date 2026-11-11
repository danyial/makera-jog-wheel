#pragma once
// Jog planner with run-on window and direction lock (SPEC 8.7).
#include <cstdint>

#include "axis_select.h"
#include "core_config.h"
#include "feed_rate.h"
#include "jog_gate.h"
#include "machine_rates.h"
#include "status.h"

struct JogCommand {
  bool valid;
  char text[48];  // e.g. "$J X0.030 Y0.030 F0.460" (F from the wheel tempo, 8.7 f)
};

struct PlannerTickResult {
  JogCommand cmd;       // at most one command per tick
  bool clipped;         // window (5e), direction lock (5d) or bundle limit (5e1) shortened or discarded
  bool session_active;  // state after this tick
};

class JogPlanner {
 public:
  JogPlanner() = default;  // rates unknown: never sends a command until set_rates() covers the axis
  explicit JogPlanner(const MachineRates& rates) : rates_(rates), next_rates_(rates) {}
  // Max rates read from the machine (8.8, ticket #38); main.cpp passes them before every tick. A tick whose
  // axis is not covered is treated like a closed gate (8.7 a), the gate already says NoRates (8.5 rule 7a).
  // During a session a changed rate (re-read, 8.8) is applied only when the session ends, so F never changes
  // its reference within a session; rates that no longer cover the session's axis apply at once (session ends).
  void set_rates(const MachineRates& rates);
  const MachineRates& rates() const { return rates_; }  // for tests: rates in use
  // detents: result of DetentAccumulator::take for this tick (signed).
  // step_um: result of StepPolicy::update for this tick.
  // status may be nullptr (no status); gate is then != None anyway.
  PlannerTickResult tick(uint32_t now_ms, int32_t detents, int32_t step_um, AxisMode axis,
                         BlockReason gate, const MachineStatus* status, uint32_t status_age_ms);
  // MachineLink could not send the command completely. Must be called in the same loop pass as the
  // tick() that produced the command; undoes its target step and ends the session at that tick.
  void notify_send_failed();
  void end_session(uint32_t now_ms);  // mode change, link loss, alarm line; sets last_end_ms, clears tempo
  bool session_active() const { return active_; }
  bool session_consistent(const MachineStatus& s) const;  // false without session
  int32_t target_um(int axis) const { return target_um_[axis]; }  // 0..3 = X, Y, Z, A
  int8_t dir() const { return dir_; }
  int queued() const { return sent_count_; }  // for tests: (e2) count after the last tick

 private:
  void finish(uint32_t now_ms);  // end the active session
  void check_end(uint32_t now_ms, const MachineStatus& s);  // step (g)
  bool start_allowed(uint32_t now_ms, const MachineStatus& s, uint32_t age) const;
  // (e2) Drops blocks a late enough status shows as done, returns the blocks possibly still queued.
  int queued_blocks(uint32_t now_ms, const MachineStatus& s, uint32_t age);
  void emit(PlannerTickResult& r, const MachineStatus& s, int32_t want, uint32_t v_um_s);  // (e)-(f)

  // A sent block, for the conservative queue count (e2): end position on the first moved axis.
  struct Sent {
    int8_t dir;
    int32_t end_um;
    uint32_t sent_ms;
  };

  MachineRates rates_{};       // in use; unknown by default
  MachineRates next_rates_{};  // latest from set_rates(), applied at the session end
  FeedEstimator feed_;  // wheel tempo for F (8.7 f), only during a session
  bool active_ = false;
  AxisMode axis_ = AxisMode::None;  // axis the session started with
  int32_t target_um_[4] = {0, 0, 0, 0};  // X, Y, Z in um; A in millidegrees
  int8_t dir_ = 0;
  uint32_t last_send_ms_ = 0;
  uint32_t last_tick_ms_ = 0;
  bool have_end_ = false;  // false = never ended
  bool in_hold_ = false;   // session paused by a short NoData (8.7 a0)
  uint32_t hold_since_ms_ = 0;
  uint32_t last_end_ms_ = 0;
  // Bundling (8.7 e1): detents collected but not yet sent, always in direction dir_.
  int32_t pending_um_ = 0;
  uint32_t pending_since_ms_ = 0;
  Sent sent_[JOG_MAX_QUEUED] = {};  // FIFO in send order (the machine executes in this order)
  int sent_head_ = 0;               // oldest entry
  int sent_count_ = 0;
  // Undo information for notify_send_failed(): the command of the latest tick.
  bool last_cmd_pending_ = false;
  int32_t last_move_um_ = 0;
};
