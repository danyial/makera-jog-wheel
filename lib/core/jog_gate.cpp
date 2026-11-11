#include "jog_gate.h"

#include "core_config.h"

BlockReason evaluate_gate(const GateInputs& in) {
  if (in.battery_critical) return BlockReason::BatteryLow;                           // 1
  if (!in.wifi_ok) return BlockReason::NoWifi;                                        // 2
  if (!in.tcp_ok) return BlockReason::NoMachine;                                      // 3
  if (!in.have_status || in.status_age_ms > SEND_MAX_AGE_MS) return BlockReason::NoData;  // 4
  if (in.axis == AxisMode::None) return BlockReason::AxisNone;                        // 5
  if (in.axis == AxisMode::A && !in.status.has_a) return BlockReason::NoAxisA;        // 5a
  if (in.status.playing) return BlockReason::JobRunning;                              // 6
  if (in.status.state == MachineState::Alarm || in.status.halted) return BlockReason::Alarm;  // 7
  if (!in.rates_known) return BlockReason::NoRates;                                   // 7a
  const MachineState st = in.status.state;
  if (in.session_active && (st == MachineState::Idle || st == MachineState::Run) && in.session_consistent) {
    return BlockReason::None;                                                         // 8
  }
  if (!in.session_active && st == MachineState::Idle) return BlockReason::None;       // 9
  return BlockReason::MachineBusy;                                                    // 10
}
