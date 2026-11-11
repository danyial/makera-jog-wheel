#pragma once
// Buffers 0x90 text payloads into lines (SPEC 8.2). Lines longer than TEXT_LINE_MAX are cut.
#include <cstddef>
#include <cstdint>

#include "core_config.h"

class TextLines {
 public:
  // Calls on_line(const char* line, size_t len) for every completed line (without '\n', '\r').
  // line is NUL-terminated.
  template <class F>
  void feed(const uint8_t* p, size_t n, F on_line) {
    for (size_t i = 0; i < n; ++i) {
      const char c = static_cast<char>(p[i]);
      if (c == '\n') {
        line_[len_] = '\0';
        on_line(static_cast<const char*>(line_), len_);
        len_ = 0;
      } else if (c != '\r' && len_ < TEXT_LINE_MAX) {
        line_[len_++] = c;
      }
    }
  }
  void clear() { len_ = 0; }

 private:
  char line_[TEXT_LINE_MAX + 1];
  size_t len_ = 0;
};
