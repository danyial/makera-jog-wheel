#include "webui.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <lwip/sockets.h>

#include <cerrno>

#include "core_config.h"
#include "http_parse.h"
#include "settings.h"
#include "settings_rules.h"
#include "web_assets.h"
#include "web_pages.h"

namespace webui {
namespace {

// Constant part of the status page (web_pages.h), sent straight from flash: no heap copy (ticket #36).
const char kStatusTail[] = WEB_STATUS_TAIL;

enum class Phase : uint8_t { Free, Reading, Writing };

Hooks hooks{};
bool running = false;
// Time of the current loop pass (the now_ms passed to loop()). Every connection timestamp comes
// from here, never from millis(): the timeout check compares against now_ms, and a later millis()
// in the same pass would lie in the future of now_ms (#29).
uint32_t pass_ms = 0;
uint32_t last_log_ms = 0;
bool have_log = false;
uint32_t log_suppressed = 0;
int listen_fd = -1;
uint32_t listen_retry_ms = 0;

// WEB_SLOTS slots, so that an idle speculative connection of a browser cannot stall the polling.
struct Conn {
  int fd = -1;
  Phase phase = Phase::Free;
  uint32_t since_ms = 0;
  uint32_t progress_ms = 0;                 // last pass in which send() took bytes (Writing)
  uint32_t send_max_ms = WEB_REQ_TIMEOUT_MS;  // absolute send budget (#37: longer for assets)
  char req[WEB_REQ_MAX + 1];
  size_t n = 0;
  String resp;
  const char* tail = nullptr;  // constant flash data sent after resp (status page), or nullptr
  size_t tail_len = 0;
  size_t sent = 0;
};

size_t resp_total(const Conn& c) { return c.resp.length() + c.tail_len; }
Conn conns[WEB_SLOTS];
Conn* cur = &conns[0];  // connection being serviced
AuthThrottle throttle;
String ap_pw;

bool open_listener() {
  const int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return false;
  const int one = 1;
  lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(WEB_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (lwip_bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || lwip_listen(fd, WEB_LISTEN_BACKLOG) != 0) {
    lwip_close(fd);
    return false;
  }
  lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  listen_fd = fd;
  return true;
}

void close_client() {
  if (cur->fd >= 0) {
    lwip_close(cur->fd);
    cur->fd = -1;
  }
  cur->phase = Phase::Free;
  cur->n = 0;
  cur->resp = String();
  cur->tail = nullptr;
  cur->tail_len = 0;
  cur->sent = 0;
}

// Serial line for a request that fails or is refused, at most one per WEB_LOG_MIN_MS (#29).
void log_drop(const char* reason, int code) {
  if (have_log && static_cast<uint32_t>(pass_ms - last_log_ms) < WEB_LOG_MIN_MS) {
    ++log_suppressed;
    return;
  }
  Serial.printf("Web: Anfrage verworfen (%s %d)", reason, code);
  if (log_suppressed > 0) Serial.printf(", %lu weitere unterdrueckt", static_cast<unsigned long>(log_suppressed));
  Serial.println();
  have_log = true;
  last_log_ms = pass_ms;
  log_suppressed = 0;
}

void drop_client(const char* reason, int code) {
  log_drop(reason, code);
  close_client();
}

// Response = status line, headers, body; then tail_len bytes from tail (flash, not copied).
// extra = further header lines, each ending in "\r\n" (default: no caching).
void respond_with_tail(int code, const char* reason, const char* type, const String& body, const char* tail,
                       size_t tail_len, const char* extra = "Cache-Control: no-store\r\n",
                       uint32_t send_max_ms = WEB_REQ_TIMEOUT_MS) {
  cur->resp = String();
  cur->resp.reserve(body.length() + 240);  // head incl. the asset headers
  cur->resp += "HTTP/1.1 ";
  cur->resp += code;
  cur->resp += ' ';
  cur->resp += reason;
  cur->resp += "\r\nContent-Type: ";
  cur->resp += type;
  cur->resp += "\r\nContent-Length: ";
  cur->resp += static_cast<unsigned>(body.length() + tail_len);
  cur->resp += "\r\n";
  cur->resp += extra;
  cur->resp += "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
  cur->resp += body;
  cur->tail = tail;
  cur->tail_len = tail != nullptr ? tail_len : 0;
  cur->sent = 0;
  cur->phase = Phase::Writing;
  cur->since_ms = pass_ms;  // the response gets its own send budget (web_send_expired)
  cur->progress_ms = pass_ms;
  cur->send_max_ms = send_max_ms;
}

void respond(int code, const char* reason, const char* type, const String& body) {
  respond_with_tail(code, reason, type, body, nullptr, 0);
}

// Complete HTML page with the common head and a back link.
void respond_page(int code, const char* reason, const String& content) {
  String html = FPSTR(settings::kPageHead);
  html += content;
  html += "<p class='foot'><a href='/'>Status</a> \xc2\xb7 <a href='/settings'>Einstellungen</a></p></main></body></html>";
  respond(code, reason, "text/html; charset=utf-8", html);
}

void send_status_page() {
  char start[320];
  const int n = web_status_start(start, sizeof(start));
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(start)) {
    respond(500, "Internal Server Error", "text/plain", "page start too large");
    return;
  }
  respond_with_tail(200, "OK", "text/html; charset=utf-8", String(start), kStatusTail, sizeof(kStatusTail) - 1);
}

// Static asset (#37): gzip bytes straight from flash, versioned path, cached for a year. Up to about
// 170 KB, so the send budget is progress-based (WEB_REQ_TIMEOUT_MS without progress, at most
// WEB_ASSET_SEND_MAX_MS in total). Sent gzip regardless of Accept-Encoding: every browser that runs
// the 3D view (ES modules, WebGL) accepts it.
void send_asset(const WebAsset& a) {
  respond_with_tail(200, "OK", "text/javascript; charset=utf-8", String(), reinterpret_cast<const char*>(a.gz),
                    a.len, "Content-Encoding: gzip\r\nCache-Control: public, max-age=31536000, immutable\r\n",
                    WEB_ASSET_SEND_MAX_MS);
}

void send_status_json() {
  StatusView v{};
  hooks.snapshot(v);
  char buf[WEB_JSON_MAX];
  const size_t n = format_status_json(v, buf, sizeof(buf));
  if (n == 0) {
    respond(500, "Internal Server Error", "text/plain", "status too large");
    return;
  }
  respond(200, "OK", "application/json", String(buf));
}

void send_settings_form(int code, const char* reason, const char* error) {
  const settings::Settings& s = hooks.config();  // loaded once at boot by main.cpp, no NVS read here
  String html;
  if (error != nullptr) html += "<p class='err'>" + settings::html_escape(error) + "</p>";
  if (!hooks.settings_allowed()) {
    html += "<p class='err'>Speichern ist nur möglich, wenn keine Achse gewählt ist und das Handrad nicht fährt.</p>";
  }
  html += "<form method='post' action='/settings'>";
  settings::append_fields(html, s, nullptr);
  html += "<label>Handrad-Passwort</label><input name='auth' type='password' required>"
          "<p class='hint'>Das Passwort des Einrichtungs-WLANs (AP-Passwort). Es steht auf dem Display, "
          "wenn das Handrad mit gehaltener X-Taste startet.</p>"
          "<button type='submit'>Speichern und neu starten</button></form>";
  String page = FPSTR(settings::kPageHead);
  page += html;
  page += "<p class='foot'><a href='/'>Status</a></p></main></body></html>";
  respond(code, reason, "text/html; charset=utf-8", page);
}

// Writes as much of the response (resp, then tail) as the socket takes right now. Returns false on a
// socket error.
bool send_some() {
  const size_t head = cur->resp.length();
  if (cur->sent >= resp_total(*cur)) return true;
  const char* p = cur->sent < head ? cur->resp.c_str() + cur->sent : cur->tail + (cur->sent - head);
  const size_t left = cur->sent < head ? head - cur->sent : resp_total(*cur) - cur->sent;
  const ssize_t w = lwip_send(cur->fd, p, left, MSG_DONTWAIT);
  if (w > 0) {
    cur->sent += static_cast<size_t>(w);
    cur->progress_ms = pass_ms;
    return true;
  }
  if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
  log_drop("send() errno", w < 0 ? errno : 0);
  return false;
}

// Copies one form field; false if it does not fit or is badly encoded (missing = empty).
bool field(const char* body, size_t n, const char* key, char* buf, size_t cap, String& out) {
  const int r = form_value(body, n, key, buf, cap);
  if (r == -2) return false;
  out = r >= 0 ? String(buf) : String();
  return true;
}

void handle_save(uint32_t now, const char* body, size_t n) {
  if (throttle.locked(now)) {
    respond_page(429, "Too Many Requests", "<p class='err'>Zu viele Versuche. Bitte kurz warten.</p>");
    return;
  }
  char buf[WEB_FIELD_MAX];
  String auth;
  if (!field(body, n, "auth", buf, sizeof(buf), auth) || !secret_equals(auth.c_str(), ap_pw.c_str())) {
    throttle.on_fail(now);
    send_settings_form(403, "Forbidden", "Handrad-Passwort falsch. Nichts gespeichert.");
    return;
  }
  // Checked in the same loop pass that saves and restarts: no tick can select an axis in between.
  if (!hooks.settings_allowed()) {
    respond_page(409, "Conflict",
                 "<p class='err'>Nicht gespeichert: Eine Achse ist gewählt oder das Handrad fährt. "
                 "Achse abwählen (Taster der gewählten Achse erneut drücken, bis alle LEDs aus sind, oder 60 s "
                 "nicht drehen) und noch einmal speichern.</p>");
    return;
  }
  settings::Form f;
  if (!field(body, n, "ssid", buf, sizeof(buf), f.ssid) || !field(body, n, "pass", buf, sizeof(buf), f.pass) ||
      !field(body, n, "host", buf, sizeof(buf), f.host) || !field(body, n, "port", buf, sizeof(buf), f.port)) {
    send_settings_form(400, "Bad Request", "Eingabe zu lang oder ungültig kodiert. Nichts gespeichert.");
    return;
  }
  f.axis_a = form_value(body, n, "axis_a", buf, sizeof(buf)) >= 0;
  f.ota = form_value(body, n, "ota", buf, sizeof(buf)) >= 0;
  const char* err = settings::save_form(f);
  if (err != nullptr) {
    send_settings_form(400, "Bad Request", err);
    return;
  }
  Serial.println("Web: Einstellungen gespeichert, Neustart");
  respond_page(200, "OK", "<p>Gespeichert. Das Handrad startet neu.</p>");
  // The response is a few hundred bytes and fits into the socket buffer in one send.
  send_some();
  close_client();
  hooks.restart();
}

void handle_request(uint32_t now, const HttpRequest& r) {
  const char* body = cur->req + r.head_len;
  const size_t n = r.content_length;
  const bool get = r.method == HttpMethod::Get;
  const bool post = r.method == HttpMethod::Post;
  if (strcmp(r.path, "/") == 0 && get) return send_status_page();
  if (strcmp(r.path, "/status.json") == 0 && get) return send_status_json();
  if (strcmp(r.path, "/settings") == 0 && get) return send_settings_form(200, "OK", nullptr);
  if (strcmp(r.path, "/settings") == 0 && post) return handle_save(now, body, n);
  for (const WebAsset& a : kWebAssets) {
    if (strcmp(r.path, a.path) == 0) {
      if (get) return send_asset(a);
      return respond(405, "Method Not Allowed", "text/plain", "method not allowed");
    }
  }
  if (strcmp(r.path, "/") == 0 || strcmp(r.path, "/status.json") == 0 || strcmp(r.path, "/settings") == 0) {
    return respond(405, "Method Not Allowed", "text/plain", "method not allowed");
  }
  respond(404, "Not Found", "text/plain", "not found");
}

void step_read(uint32_t now) {
  const ssize_t got = lwip_recv(cur->fd, cur->req + cur->n, WEB_REQ_MAX - cur->n, MSG_DONTWAIT);
  if (got == 0) {  // peer closed; without any byte that is a browser's idle speculative connection
    if (cur->n > 0) return drop_client("Gegenstelle schloss vor Ende der Anfrage, Byte", static_cast<int>(cur->n));
    return close_client();
  }
  if (got < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) drop_client("recv() errno", errno);
    return;
  }
  cur->n += static_cast<size_t>(got);
  cur->req[cur->n] = '\0';
  HttpRequest r;
  const HttpParse p = parse_http_head(cur->req, cur->n, r);
  if (p == HttpParse::Bad) {
    log_drop("Kopf ungueltig, Antwort", 400);
    return respond(400, "Bad Request", "text/plain", "bad request");
  }
  if (p == HttpParse::Incomplete) {
    if (cur->n >= WEB_REQ_MAX) {
      log_drop("Kopf zu lang, Antwort", 413);
      respond(413, "Payload Too Large", "text/plain", "request too large");
    }
    return;
  }
  if (r.head_len + r.content_length > WEB_REQ_MAX) {
    log_drop("Body zu lang, Antwort", 413);
    return respond(413, "Payload Too Large", "text/plain", "request too large");
  }
  if (cur->n < r.head_len + r.content_length) return;  // body still arriving
  handle_request(now, r);
}

}  // namespace

void begin(const Hooks& h) {
  hooks = h;
  ap_pw = settings::ap_pass();
  running = true;
  if (!open_listener()) Serial.println("Web: Port 80 nicht geoeffnet, neuer Versuch in 1 s");
  if (MDNS.begin(WEB_HOSTNAME)) {
    MDNS.addService("http", "tcp", WEB_PORT);
  } else {
    Serial.println("Web: mDNS nicht gestartet");
  }
}

bool started() { return running; }

void loop(uint32_t now) {
  if (!running) return;
  pass_ms = now;
  if (listen_fd < 0 && static_cast<uint32_t>(now - listen_retry_ms) >= WEB_LISTEN_RETRY_MS) {
    listen_retry_ms = now;
    open_listener();
  }
  // At most one accept per pass, into a free slot.
  for (Conn& c : conns) {
    if (listen_fd < 0) break;
    if (c.phase != Phase::Free) continue;
    const int fd = lwip_accept(listen_fd, nullptr, nullptr);
    if (fd < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        log_drop("accept() errno, Port 80 neu in 1 s", errno);
        lwip_close(listen_fd);  // reopened after 1 s
        listen_fd = -1;
        listen_retry_ms = now;
      }
    } else {
      lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
      c.fd = fd;
      c.n = 0;
      c.since_ms = now;
      c.phase = Phase::Reading;
    }
    break;
  }
  // Per slot at most one recv or one send.
  for (Conn& c : conns) {
    cur = &c;
    switch (c.phase) {
      case Phase::Free:
        continue;
      case Phase::Reading:
        step_read(now);
        break;
      case Phase::Writing:
        if (!send_some() || c.sent >= resp_total(c)) close_client();
        break;
    }
    if (c.phase == Phase::Free) continue;
    if (c.phase == Phase::Writing ? !web_send_expired(now, c.since_ms, c.progress_ms, c.send_max_ms)
                                  : !web_conn_expired(now, c.since_ms)) {
      continue;
    }
    if (c.phase == Phase::Writing) {
      drop_client("Zeitlimit beim Senden, Byte gesendet", static_cast<int>(c.sent));
    } else if (c.n > 0) {
      drop_client("Zeitlimit beim Lesen, Byte", static_cast<int>(c.n));
    } else {
      close_client();  // idle speculative connection: normal, not logged
    }
  }
}

}  // namespace webui
