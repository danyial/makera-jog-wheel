#include "machine_link.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>

#include <cerrno>
#include <cstring>

#include "core_config.h"
#include "fixed_fmt.h"
#include "frame.h"
#include "machine_rates.h"
#include "text_lines.h"
#include "ui_config.h"

namespace machine {
namespace {

Config cfg{};
void (*session_end_cb)(uint32_t) = nullptr;

LinkState st = LinkState::Idle;
bool stopped = false;
uint32_t state_since_ms = 0;
int backoff_idx = 0;
uint32_t backoff_ms = 0;

int sock = -1;
uint32_t last_poll_ms = 0;
frame::Decoder decoder;
TextLines lines;
RateQuery rate_query;  // M203.1 after every connect, re-read while idle (8.8, ticket #38)
bool jog_active = false;

LinkStats stats{};
uint32_t prev_status_rx_ms = 0;
bool have_prev_status = false;

bool have_st = false;
MachineStatus last_status{};
uint32_t status_rx_ms = 0;

char alarm_buf[UI_LINE_MAX_CHARS + 1] = {0};
bool have_alarm = false;
uint32_t alarm_rx_ms = 0;

void enter(LinkState s, uint32_t now) {
  st = s;
  state_since_ms = now;
}

void close_socket() {
  if (sock >= 0) {
    lwip_close(sock);
    sock = -1;
  }
}

void start_wifi(uint32_t now) {
  WiFi.disconnect(false, false);
  WiFi.begin(cfg.ssid, cfg.pass);
  enter(LinkState::WifiConnecting, now);
}

char error_buf[48] = "";

// Every error leads here: close, end the session, back off 1 s, 2 s, 5 s, 5 s, ...
void fail(uint32_t now, const char* reason, int code = 0) {
  if (code != 0) {
    snprintf(error_buf, sizeof(error_buf), "%s %d", reason, code);
  } else {
    snprintf(error_buf, sizeof(error_buf), "%s", reason);
  }
  close_socket();
  have_st = false;
  rate_query.reset();  // re-read on every connection
  if (session_end_cb != nullptr) session_end_cb(now);
  backoff_ms = BACKOFF_MS[backoff_idx < 2 ? backoff_idx : 2];
  if (backoff_idx < 3) ++backoff_idx;
  enter(LinkState::Backoff, now);
}

bool send_frame(uint8_t type, const uint8_t* payload, size_t n) {
  if (sock < 0) return false;
  uint8_t buf[TX_MAX_PAYLOAD + frame::OVERHEAD];
  const size_t len = frame::build(type, payload, n, buf, sizeof(buf));
  if (len == 0) return false;
  const ssize_t w = lwip_send(sock, buf, len, MSG_DONTWAIT);
  return w == static_cast<ssize_t>(len);  // partial write or EAGAIN counts as socket error
}

bool send_poll() {
  const uint8_t q = '?';
  return send_frame(frame::TYPE_REALTIME, &q, 1);
}

bool send_rate_query() {
  return send_frame(frame::TYPE_CONSOLE, reinterpret_cast<const uint8_t*>(RATE_QUERY_TEXT), strlen(RATE_QUERY_TEXT));
}

void log_rates(const MachineRates& r) {
  char x[16], y[16], z[16], a[16];
  format_um(static_cast<int32_t>(r.um_s[0]), x, sizeof(x));
  format_um(static_cast<int32_t>(r.um_s[1]), y, sizeof(y));
  format_um(static_cast<int32_t>(r.um_s[2]), z, sizeof(z));
  format_um(static_cast<int32_t>(r.um_s[3]), a, sizeof(a));
  Serial.printf("Raten: X %s Y %s Z %s mm/s, A %s Grad/s (M203.1)\n", x, y, z, r.a ? a : "unbekannt");
}

bool starts_with_ci(const char* s, size_t n, const char* prefix) {
  const size_t m = strlen(prefix);
  if (n < m) return false;
  for (size_t i = 0; i < m; ++i) {
    if (tolower(static_cast<unsigned char>(s[i])) != tolower(static_cast<unsigned char>(prefix[i]))) return false;
  }
  return true;
}

void on_frame(const frame::Frame& f, uint32_t now) {
  if (f.type == frame::TYPE_STATUS) {
    MachineStatus s;
    if (parse_status(reinterpret_cast<const char*>(f.payload), f.n, s)) {
      ++stats.status_ok;
      if (have_prev_status) {
        const uint32_t gap = static_cast<uint32_t>(now - prev_status_rx_ms);
        if (gap > stats.max_gap_ms) stats.max_gap_ms = gap;
        if (gap > SEND_MAX_AGE_MS) ++stats.gaps_over_limit;
      }
      prev_status_rx_ms = now;
      have_prev_status = true;
      last_status = s;
      have_st = true;
      status_rx_ms = now;
    } else {
      ++stats.status_bad;
    }
  } else if (f.type == frame::TYPE_TEXT) {
    ++stats.text_frames;
    lines.feed(f.payload, f.n, [now](const char* line, size_t n) {
      const bool was_known = rate_query.known();
      const MachineRates before = rate_query.rates();
      const uint32_t fails = rate_query.refresh_failures();
      const RateLine rl = rate_query.on_line(line, n, now);
      if (rl == RateLine::Valid && (!was_known || memcmp(before.um_s, rate_query.rates().um_s, sizeof(before.um_s)) != 0 ||
                                    before.a != rate_query.rates().a)) {
        log_rates(rate_query.rates());  // first read, or a re-read that changed something
      }
      if (rl == RateLine::Invalid && (!was_known || (fails == 0 && rate_query.refresh_failures() == 1))) {
        Serial.printf("Raten: Antwort verworfen: %.80s\n", line);
      }
      if (starts_with_ci(line, n, "ALARM") || starts_with_ci(line, n, "error")) {
        const size_t k = n < static_cast<size_t>(UI_LINE_MAX_CHARS) ? n : UI_LINE_MAX_CHARS;
        memcpy(alarm_buf, line, k);
        alarm_buf[k] = '\0';
        have_alarm = true;
        alarm_rx_ms = now;
        if (session_end_cb != nullptr) session_end_cb(now);
      }
    });
  } else {
    ++stats.other_frames;
  }
}

void start_tcp(uint32_t now) {
  close_socket();
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(cfg.port);
  if (inet_pton(AF_INET, cfg.host, &addr.sin_addr) != 1) {
    fail(now, "Maschinen-IP ungueltig");
    return;
  }
  sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) {
    fail(now, "socket() errno", errno);
    return;
  }
  const int flags = lwip_fcntl(sock, F_GETFL, 0);
  lwip_fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  const int r = lwip_connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (r != 0 && errno != EINPROGRESS) {
    fail(now, "connect() errno", errno);
    return;
  }
  enter(LinkState::TcpConnecting, now);
}

void go_online(uint32_t now) {
  have_prev_status = false;
  const int one = 1;
  lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  decoder = frame::Decoder();
  lines.clear();
  have_st = false;
  rate_query.reset();
  backoff_idx = 0;
  enter(LinkState::Online, now);
  last_poll_ms = now;
  if (!send_poll()) fail(now, "erstes ? nicht gesendet");
}

void poll_connect(uint32_t now) {
  fd_set wfds;
  FD_ZERO(&wfds);
  FD_SET(sock, &wfds);
  timeval tv{0, 0};
  const int r = lwip_select(sock + 1, nullptr, &wfds, nullptr, &tv);
  if (r < 0) {
    fail(now, "select() errno", errno);
    return;
  }
  if (r > 0 && FD_ISSET(sock, &wfds)) {
    int err = 0;
    socklen_t len = sizeof(err);
    lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err == 0) {
      go_online(now);
    } else {
      fail(now, "TCP abgelehnt/unerreichbar, errno", err);
    }
    return;
  }
  if (static_cast<uint32_t>(now - state_since_ms) >= TCP_TIMEOUT_MS) fail(now, "TCP-Timeout 3 s");
}

void read_all(uint32_t now) {
  uint8_t buf[512];
  for (;;) {
    const ssize_t n = lwip_recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
    if (n > 0) {
      decoder.feed(buf, static_cast<size_t>(n), [now](const frame::Frame& f) { on_frame(f, now); });
      if (st != LinkState::Online) return;  // torn down while handling
      continue;
    }
    if (n == 0) {  // peer closed
      fail(now, "Maschine hat Verbindung geschlossen");
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    fail(now, "recv() errno", errno);
    return;
  }
}

}  // namespace

void begin(const Config& c, void (*on_session_end)(uint32_t)) {
  cfg = c;
  session_end_cb = on_session_end;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // reconnects are driven by this state machine
  // Modem sleep holds incoming packets until the next beacon and adds latency to status reports
  // (ticket #18). Costs some battery current while online.
  WiFi.setSleep(false);
  start_wifi(millis());
}

void loop(uint32_t now) {
  if (stopped) return;
  const bool wifi = WiFi.status() == WL_CONNECTED;
  switch (st) {
    case LinkState::Idle:
      start_wifi(now);
      break;
    case LinkState::WifiConnecting:
      if (wifi) {
        start_tcp(now);
      } else if (static_cast<uint32_t>(now - state_since_ms) >= WIFI_TIMEOUT_MS) {
        fail(now, "WLAN-Timeout 15 s");
      }
      break;
    case LinkState::Backoff:
      if (static_cast<uint32_t>(now - state_since_ms) >= backoff_ms) {
        if (wifi) {
          start_tcp(now);
        } else {
          start_wifi(now);
        }
      }
      break;
    case LinkState::TcpConnecting:
      if (!wifi) {
        fail(now, "WLAN verloren");
      } else {
        poll_connect(now);
      }
      break;
    case LinkState::Online:
      if (!wifi) {
        fail(now, "WLAN verloren");
        break;
      }
      read_all(now);
      if (st != LinkState::Online) break;
      if (static_cast<uint32_t>(now - last_poll_ms) >= STATUS_INTERVAL_MS) {
        last_poll_ms = now;
        if (!send_poll()) {
          fail(now, "? nicht gesendet");
          break;
        }
      }
      // Max rates (8.8, ticket #38): after the first status and never in Alarm/HALT, at most one query in flight,
      // retries with backoff, re-read while Idle without session. Independent of the '?' poll and the status
      // timeout below.
      {
        const MachineStatus* rs = have_st ? &last_status : nullptr;
        const bool was_known = rate_query.known();
        const uint32_t fails = rate_query.refresh_failures();
        const bool send = rate_query.poll(now, RateQuery::ready(rs), RateQuery::refresh_allowed(rs, jog_active));
        if (fails == 0 && rate_query.refresh_failures() == 1) {
          Serial.println("Raten: erneutes Lesen ohne Antwort, alte Raten bleiben");
        }
        if (send && !was_known && rate_query.queries() > 1) {
          Serial.printf("Raten: Abfrage %lu\n", (unsigned long)rate_query.queries());
        }
        if (send && !send_rate_query()) {
          fail(now, "M203.1 nicht gesendet");
          break;
        }
      }
      {
        // No status report for LINK_STATUS_TIMEOUT_MS (since Online or since the last one) -> drop.
        const uint32_t ref = have_st ? status_rx_ms : state_since_ms;
        if (static_cast<uint32_t>(now - ref) >= LINK_STATUS_TIMEOUT_MS) fail(now, "kein Statusreport seit 3 s");
      }
      break;
  }
}

LinkState state() { return st; }
bool wifi_ok() { return WiFi.status() == WL_CONNECTED; }
bool tcp_ok() { return st == LinkState::Online; }
bool have_status() { return st == LinkState::Online && have_st; }
const MachineStatus& status() { return last_status; }
uint32_t status_age_ms(uint32_t now) { return static_cast<uint32_t>(now - status_rx_ms); }

const MachineRates& rates() { return rate_query.rates(); }
void set_jog_active(bool active) { jog_active = active; }

int8_t rssi_bars() {
  if (WiFi.status() != WL_CONNECTED) return -1;
  const int r = WiFi.RSSI();
  if (r >= -60) return 3;
  if (r >= -70) return 2;
  if (r >= -80) return 1;
  return 0;
}

bool send_line(const char* text) {
  if (stopped || st != LinkState::Online) return false;
  return send_frame(frame::TYPE_CONSOLE, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

void stop() {
  // Orderly end towards the machine (9.3, 9.5, 9.1): nothing more to send or read, FIN goes out now.
  if (sock >= 0) lwip_shutdown(sock, SHUT_RDWR);
  close_socket();
  have_st = false;
  rate_query.reset();
  st = LinkState::Idle;
  stopped = true;
}

void drop(uint32_t now, const char* reason) {
  if (st == LinkState::Online || st == LinkState::TcpConnecting) fail(now, reason);
}

const char* last_error() { return error_buf; }

LinkStats take_stats() {
  LinkStats out = stats;
  out.dropped = decoder.dropped();
  stats = LinkStats{};
  return out;
}

const char* alarm_line() { return have_alarm ? alarm_buf : nullptr; }
uint32_t alarm_ms() { return alarm_rx_ms; }

}  // namespace machine
