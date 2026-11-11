#pragma once
// Max rates of the machine's actuators, read at runtime with M203.1 (SPEC 8.8 "Maschinenraten", ticket #38).
#include <cstddef>
#include <cstdint>

#include "axis_select.h"
#include "core_config.h"
#include "status.h"

struct MachineRates {
  uint32_t um_s[4] = {0, 0, 0, 0};  // X, Y, Z in um/s, A in millidegrees/s; rounded up
  bool xyz = false;                 // X, Y and Z read and plausible
  bool a = false;                   // A read and plausible (the machine reports A only if configured)
};

// true if the rates of every axis moved by `axis` are known. None: false.
bool rates_cover(const MachineRates& r, AxisMode axis);

enum class RateLine : uint8_t { NotRates, Valid, Invalid };

// Parses one text line of the answer to M203.1 (v1.1.3 Robot.cpp: " X: %g  Y: %g  Z: %g  A: %g  B: %g ", mm/s
// and deg/s). NotRates: the line does not start with "X:" (any other output on the shared connection).
// Invalid: starts with "X:" but has an unknown letter, a duplicate, a bad number, a missing X/Y/Z or a value
// outside [RATE_MIN_UM_S, RATE_MAX_UM_S]. Valid: out is filled (xyz = true; a = true if A was present).
// B and C are parsed but ignored. `out` is only written for Valid.
RateLine parse_rate_line(const char* line, size_t n, MachineRates& out);

// M203.1 queries of one connection (8.8, ticket #38): the first read, at most one query in flight, retries with
// RATE_RETRY_MS; once known, a re-read every RATE_REFRESH_MS while the machine is Idle and no jog session runs.
class RateQuery {
 public:
  // The machine may be asked: a status is there and it is neither in Alarm nor halted. In HALT the firmware answers
  // M203.1 with "error:Alarm lock" (grbl mode) or "!!", which would only overwrite the real alarm line.
  static bool ready(const MachineStatus* status);
  // A re-read may be sent: ready(), state Idle and no jog session.
  static bool refresh_allowed(const MachineStatus* status, bool jog_active);

  // New connection or link loss: rates unknown, the next poll() with ready = true queries at once.
  void reset();
  // Call every loop pass while Online. Returns true when the caller must send RATE_QUERY_TEXT now.
  // First read: a query without a Valid answer for RATE_QUERY_TIMEOUT_MS, or answered Invalid, is followed by a new
  // one after RATE_RETRY_MS[i] (then RATE_RETRY_MS[2] forever); while !ready the wait is dropped, so the next query
  // goes out as soon as the machine may be asked again (e.g. right after an alarm).
  // Known: a re-read RATE_REFRESH_MS after the last valid answer or failed re-read, only with refresh_ok.
  bool poll(uint32_t now_ms, bool ready, bool refresh_ok = false);
  // Every text line received while Online. First read: a Valid line is taken whenever the rates are not yet known
  // (also a late answer to an earlier query: it carries the live values as well). Re-read: only while it is in
  // flight; Valid replaces the rates, Invalid or a timeout keeps the old ones. Otherwise lines are ignored and
  // NotRates is returned.
  RateLine on_line(const char* line, size_t n, uint32_t now_ms);
  bool known() const { return phase_ == Phase::Done || phase_ == Phase::Refresh; }
  const MachineRates& rates() const { return rates_; }  // all flags false until known()
  uint32_t queries() const { return queries_; }          // queries sent since reset()
  uint32_t refresh_failures() const { return refresh_failures_; }  // failed re-reads in a row (log only the first)

 private:
  enum class Phase : uint8_t { Start, InFlight, Wait, Done, Refresh };
  void retry_later(uint32_t now_ms);
  void refresh_failed(uint32_t now_ms);
  Phase phase_ = Phase::Start;
  uint32_t since_ms_ = 0;
  uint32_t wait_ms_ = 0;
  int retry_idx_ = 0;
  uint32_t queries_ = 0;
  uint32_t refresh_failures_ = 0;
  MachineRates rates_{};
};
