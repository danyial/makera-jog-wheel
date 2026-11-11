#pragma once
// Read-only status snapshot for the web UI and its JSON form (SPEC 9.1, ticket #17).
#include <cstddef>
#include <cstdint>

#include "status.h"

// Link statistics of the last completed 5 s window (same values as the serial statistics line).
struct StatsView {
  uint32_t status_ok;
  uint32_t status_bad;
  uint32_t text_frames;
  uint32_t other_frames;
  uint32_t max_gap_ms;
  uint32_t gaps_over_limit;
  uint32_t dropped;
  uint32_t loop_max_ms;
  uint32_t loop_mean_us;
  uint32_t web_max_us;
};

struct StatusView {
  const char* fw;          // FW_VERSION
  uint32_t uptime_s;
  const char* link;        // link state text
  const char* last_error;  // reason of the last link failure, "" if none
  bool wifi;
  int32_t rssi_dbm;        // valid only if wifi
  const char* ip;          // own IP, "" without WiFi
  bool have_status;
  MachineStatus status;    // valid only if have_status
  uint32_t status_age_ms;
  const char* gate;        // block reason text (10.1)
  const char* axis;        // axis selection text
  bool axis_a_enabled;
  bool session_active;
  bool settings_allowed;
  uint16_t bat_mv;
  uint8_t bat_percent;
  bool bat_absent;
  bool bat_warn;
  const char* alarm;       // last ALARM/error line or nullptr
  bool have_stats;
  StatsView stats;
};

// Protocol name of the machine state ("Idle", "Run", ...).
const char* machine_state_name(MachineState s);

// Writes the snapshot as one JSON object. Positions in mm (A in degrees) with 3 decimals, always
// from the micrometre values regardless of inch_mode. Strings are JSON-escaped.
// Returns the length (without NUL), 0 if cap is too small.
size_t format_status_json(const StatusView& v, char* out, size_t cap);
