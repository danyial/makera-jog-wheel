#pragma once
// Jog permission (SPEC 8.5). The single protection against $J during a job.
#include <cstdint>

#include "axis_select.h"
#include "status.h"

enum class BlockReason : uint8_t { None, BatteryLow, NoWifi, NoMachine, NoData,
                                   AxisNone, NoAxisA, JobRunning, Alarm, NoRates, MachineBusy };

struct GateInputs {
  bool battery_critical;    // from Battery (9.3), latched until restart
  bool wifi_ok;             // WiFi connected with IP
  bool tcp_ok;              // MachineLink Online
  bool have_status;         // at least one status since Online
  MachineStatus status;     // last status (valid only if have_status)
  uint32_t status_age_ms;   // now - receive time of last status
  AxisMode axis;
  bool rates_known;         // machine max rates read for every axis of `axis` (rates_cover, 8.8, ticket #38)
  bool session_active;      // from JogPlanner
  bool session_consistent;  // from JogPlanner
};

BlockReason evaluate_gate(const GateInputs& in);
