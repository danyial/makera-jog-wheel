#include "http_parse.h"

#include <cstring>

#include "core_config.h"

namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equals_ci(const char* a, size_t n, const char* b) {
  if (std::strlen(b) != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

HttpParse parse_http_head(const char* buf, size_t n, HttpRequest& out) {
  // End of the header block.
  size_t head = 0;
  for (size_t i = 0; i + 3 < n; ++i) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
      head = i + 4;
      break;
    }
  }
  if (head == 0) return HttpParse::Incomplete;

  out = HttpRequest{};
  out.head_len = head;

  // Request line: METHOD SP target SP version CRLF
  size_t i = 0;
  while (i < head && buf[i] != ' ') ++i;
  if (i == 0 || i >= head) return HttpParse::Bad;
  if (i == 3 && std::memcmp(buf, "GET", 3) == 0) {
    out.method = HttpMethod::Get;
  } else if (i == 4 && std::memcmp(buf, "POST", 4) == 0) {
    out.method = HttpMethod::Post;
  } else {
    out.method = HttpMethod::Other;
  }
  ++i;
  if (i >= head || buf[i] != '/') return HttpParse::Bad;
  size_t p = 0;
  while (i < head && buf[i] != ' ' && buf[i] != '?' && buf[i] != '\r') {
    if (p + 1 >= sizeof(out.path)) return HttpParse::Bad;
    out.path[p++] = buf[i++];
  }
  out.path[p] = '\0';
  while (i < head && buf[i] != '\n') ++i;  // rest of the request line
  ++i;

  // Headers: only Content-Length matters.
  bool have_length = false;
  while (i < head) {
    size_t eol = i;
    while (eol < head && buf[eol] != '\r') ++eol;
    if (eol == i) break;  // blank line
    size_t colon = i;
    while (colon < eol && buf[colon] != ':') ++colon;
    if (colon < eol && equals_ci(buf + i, colon - i, "content-length")) {
      if (have_length) return HttpParse::Bad;  // duplicate: ambiguous body length
      have_length = true;
      size_t v = colon + 1;
      while (v < eol && buf[v] == ' ') ++v;
      if (v == eol) return HttpParse::Bad;
      size_t len = 0;
      for (; v < eol && buf[v] != ' '; ++v) {
        if (buf[v] < '0' || buf[v] > '9') return HttpParse::Bad;
        if (len > 100000000) return HttpParse::Bad;
        len = len * 10 + static_cast<size_t>(buf[v] - '0');
      }
      out.content_length = len;
    }
    i = eol + 2;
  }
  return HttpParse::Ok;
}

int form_value(const char* body, size_t n, const char* key, char* out, size_t cap) {
  const size_t klen = std::strlen(key);
  size_t i = 0;
  while (i < n) {
    size_t end = i;
    while (end < n && body[end] != '&') ++end;
    size_t eq = i;
    while (eq < end && body[eq] != '=') ++eq;
    if (eq - i == klen && std::memcmp(body + i, key, klen) == 0) {
      if (cap == 0) return -2;
      size_t o = 0;
      for (size_t j = eq + 1; j < end; ++j) {
        char c = body[j];
        if (c == '+') {
          c = ' ';
        } else if (c == '%') {
          if (j + 2 >= end) return -2;
          const int hi = hex_digit(body[j + 1]);
          const int lo = hex_digit(body[j + 2]);
          if (hi < 0 || lo < 0 || (hi == 0 && lo == 0)) return -2;  // %00 would cut C strings
          c = static_cast<char>(hi * 16 + lo);
          j += 2;
        }
        if (o + 1 >= cap) return -2;
        out[o++] = c;
      }
      out[o] = '\0';
      return static_cast<int>(o);
    }
    i = end + 1;
  }
  return -1;
}

bool web_conn_expired(uint32_t now_ms, uint32_t since_ms) {
  // Signed difference: the budget is 2 s, a connection never lives anywhere near 2^31 ms.
  return static_cast<int32_t>(now_ms - since_ms) >= static_cast<int32_t>(WEB_REQ_TIMEOUT_MS);
}

bool web_send_expired(uint32_t now_ms, uint32_t since_ms, uint32_t progress_ms, uint32_t max_ms) {
  return web_conn_expired(now_ms, progress_ms) ||
         static_cast<int32_t>(now_ms - since_ms) >= static_cast<int32_t>(max_ms);
}
