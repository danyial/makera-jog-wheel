#pragma once
// WiFi + non-blocking lwIP TCP link to the machine (SPEC 8.8).
#include <cstddef>
#include <cstdint>

#include "machine_rates.h"
#include "status.h"

namespace machine {

enum class LinkState : uint8_t { Idle, WifiConnecting, TcpConnecting, Online, Backoff };

struct Config {
  const char* ssid;
  const char* pass;
  const char* host;
  uint16_t port;
};

// on_session_end(now) is called for every event that must end a jog session:
// leaving Online and alarm/error text lines.
void begin(const Config& cfg, void (*on_session_end)(uint32_t now_ms));
// Drives the state machine, polls '?' every STATUS_INTERVAL_MS, reads and decodes frames.
// Never blocks.
void loop(uint32_t now_ms);

LinkState state();
bool wifi_ok();
bool tcp_ok();                    // Online
bool have_status();               // at least one status since Online
const MachineStatus& status();    // valid only if have_status()
uint32_t status_age_ms(uint32_t now_ms);
int8_t rssi_bars();               // -1 no WiFi, 0..3
// Max rates read with M203.1 on this connection (8.8, ticket #38); all flags false until known, reset on
// every link loss.
const MachineRates& rates();
// Whether a jog session runs (main.cpp after every tick); re-reads of the rates only without one (8.8).
void set_jog_active(bool active);

// Sends a console line (0xA2). Returns false if the frame was not written completely. The caller
// must then call planner.notify_send_failed() and afterwards drop(now) (SPEC 8.7 step 6, 8.8).
bool send_line(const char* text);
// Closes the socket for good and stops all link activity (shutdown). No session-end callback.
void stop();
// Tears the connection down as after any socket error (close, session end, backoff). reason: serial log.
void drop(uint32_t now_ms, const char* reason = "$J nicht vollstaendig gesendet");

// Counters since the last call (serial log, ticket #15).
struct LinkStats {
  uint32_t status_ok;        // parsed 0x81 reports
  uint32_t status_bad;       // 0x81 frames that did not parse
  uint32_t text_frames;      // 0x90
  uint32_t other_frames;     // all other types
  uint32_t max_gap_ms;       // largest gap between two parsed reports
  uint32_t gaps_over_limit;  // gaps > SEND_MAX_AGE_MS
  uint32_t dropped;          // decoder total of rejected frames
};
LinkStats take_stats();

// Reason of the last link failure (serial log).
const char* last_error();

// Last ALARM/error line (shortened) and when it arrived; nullptr if none yet.
const char* alarm_line();
uint32_t alarm_ms();

}  // namespace machine
