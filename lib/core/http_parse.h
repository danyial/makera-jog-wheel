#pragma once
// Minimal HTTP/1.x request parsing for the web UI in normal mode (SPEC 9.1, ticket #17).
// Pure functions on caller-owned buffers; no allocation.
#include <cstddef>
#include <cstdint>

enum class HttpParse : uint8_t { Incomplete, Ok, Bad };
enum class HttpMethod : uint8_t { Get, Post, Other };

struct HttpRequest {
  HttpMethod method;
  char path[32];          // request target up to '?' or ' ', NUL-terminated
  size_t head_len;        // bytes up to and including the blank line
  size_t content_length;  // 0 without Content-Length
};

// Parses the request line and headers in buf[0..n). Incomplete until "\r\n\r\n" arrived.
// Bad: malformed request line, path longer than 31 characters, invalid or duplicate Content-Length.
HttpParse parse_http_head(const char* buf, size_t n, HttpRequest& out);

// Looks up key in an application/x-www-form-urlencoded body and URL-decodes it into out
// ('+' = space, %XX). Returns the decoded length, -1 if the key is missing, -2 if the value
// does not fit into cap - 1 characters or contains an invalid escape (also %00).
int form_value(const char* body, size_t n, const char* key, char* out, size_t cap);

// Per-connection budget (WEB_REQ_TIMEOUT_MS) for reading the request and for sending the response.
// since_ms and now_ms must come from the same clock reading per loop pass; a since_ms that lies
// after now_ms (taken later in the same pass) counts as not expired instead of wrapping (#29).
bool web_conn_expired(uint32_t now_ms, uint32_t since_ms);

// Budget for sending a response (ticket #37). Expired when nothing went out for WEB_REQ_TIMEOUT_MS
// (progress_ms = last pass in which send() took bytes, initially the start) or when the response is
// older than max_ms (WEB_REQ_TIMEOUT_MS for pages and JSON, so they keep the plain 2 s budget;
// WEB_ASSET_SEND_MAX_MS for the large static assets). Same clock rules as web_conn_expired.
bool web_send_expired(uint32_t now_ms, uint32_t since_ms, uint32_t progress_ms, uint32_t max_ms);
