#include "status.h"

#include <cstring>

namespace {

struct Span {
  const char* p;
  size_t n;
  bool equals(const char* s) const { return std::strlen(s) == n && std::memcmp(p, s, n) == 0; }
};

MachineState parse_state(Span s) {
  // "Hold:0", "Door:1" etc. carry a sub-state after ':'.
  for (size_t i = 0; i < s.n; ++i) {
    if (s.p[i] == ':') {
      s.n = i;
      break;
    }
  }
  if (s.equals("Idle")) return MachineState::Idle;
  if (s.equals("Run")) return MachineState::Run;
  if (s.equals("Hold")) return MachineState::Hold;
  if (s.equals("Pause")) return MachineState::Pause;
  if (s.equals("Alarm")) return MachineState::Alarm;
  if (s.equals("Home")) return MachineState::Home;
  if (s.equals("Tool")) return MachineState::Tool;
  if (s.equals("Sleep")) return MachineState::Sleep;
  if (s.equals("Wait")) return MachineState::Wait;
  return MachineState::Unknown;
}

// Parses one decimal number exactly (no float) as mantissa / 10^frac_digits.
constexpr int kMaxIntDigits = 6;
constexpr int kMaxFracDigits = 8;

bool parse_decimal(Span s, bool& neg, int64_t& mant, int64_t& scale) {
  size_t i = 0;
  while (i < s.n && s.p[i] == ' ') ++i;
  neg = false;
  if (i < s.n && (s.p[i] == '-' || s.p[i] == '+')) {
    neg = s.p[i] == '-';
    ++i;
  }
  mant = 0;
  scale = 1;
  int int_digits = 0;
  int frac_digits = 0;
  bool any = false;
  while (i < s.n && s.p[i] >= '0' && s.p[i] <= '9') {
    if (++int_digits > kMaxIntDigits) return false;
    mant = mant * 10 + (s.p[i] - '0');
    any = true;
    ++i;
  }
  if (i < s.n && s.p[i] == '.') {
    ++i;
    while (i < s.n && s.p[i] >= '0' && s.p[i] <= '9') {
      if (frac_digits < kMaxFracDigits) {  // further digits are below 1e-8 and irrelevant
        mant = mant * 10 + (s.p[i] - '0');
        scale *= 10;
        ++frac_digits;
      }
      any = true;
      ++i;
    }
  }
  while (i < s.n && s.p[i] == ' ') ++i;
  return any && i == s.n;
}

// value (in the reported unit) -> micrometres, rounded half away from zero.
bool to_um(Span s, int64_t factor, int32_t& out) {
  bool neg;
  int64_t mant, scale;
  if (!parse_decimal(s, neg, mant, scale)) return false;
  const int64_t um = (mant * factor + scale / 2) / scale;
  if (um > INT32_MAX) return false;
  out = static_cast<int32_t>(neg ? -um : um);
  return true;
}

// Splits values at ',' into at most max parts; returns the number of parts.
size_t split(Span v, Span* parts, size_t max) {
  size_t count = 0;
  size_t start = 0;
  for (size_t i = 0; i <= v.n && count < max; ++i) {
    if (i == v.n || v.p[i] == ',') {
      parts[count++] = Span{v.p + start, i - start};
      start = i + 1;
    }
  }
  return count;
}

// X, Y, Z in the active unit (factor); a 4th value is the rotary axis A in degrees, never scaled
// by inch mode (SPEC 8.3). Returns false on malformed input; has_a reports a valid 4th value.
bool parse_axes(Span v, int64_t factor, int32_t out[4], bool& has_a) {
  Span parts[4];
  const size_t n = split(v, parts, 4);
  if (n < 3) return false;
  for (int a = 0; a < 3; ++a) {
    if (!to_um(parts[a], factor, out[a])) return false;
  }
  out[3] = 0;
  has_a = n >= 4 && to_um(parts[3], 1000, out[3]);
  if (!has_a) out[3] = 0;
  return true;
}

}  // namespace

bool parse_status(const char* text, size_t len, MachineStatus& out) {
  if (text == nullptr) return false;
  size_t b = 0;
  while (b < len && (text[b] == ' ' || text[b] == '\r' || text[b] == '\n')) ++b;
  if (b >= len || text[b] != '<') return false;
  size_t e = b + 1;
  while (e < len && text[e] != '>') ++e;
  if (e >= len) return false;  // no closing '>'

  // Split body at '|': first part is the state, then key:values fields.
  Span body{text + b + 1, e - b - 1};
  Span mpos{}, wpos{}, c{};
  bool have_mpos = false, have_wpos = false, have_c = false, playing = false, halted = false;
  Span state{};
  size_t start = 0;
  bool first = true;
  for (size_t i = 0; i <= body.n; ++i) {
    if (i != body.n && body.p[i] != '|') continue;
    Span part{body.p + start, i - start};
    start = i + 1;
    if (first) {
      state = part;
      first = false;
      continue;
    }
    size_t colon = 0;
    while (colon < part.n && part.p[colon] != ':') ++colon;
    if (colon == part.n) continue;  // not a key:value field; ignore
    Span key{part.p, colon};
    Span val{part.p + colon + 1, part.n - colon - 1};
    if (key.equals("MPos")) {
      mpos = val;
      have_mpos = true;
    } else if (key.equals("WPos")) {
      wpos = val;
      have_wpos = true;
    } else if (key.equals("C")) {
      c = val;
      have_c = true;
    } else if (key.equals("P")) {
      playing = true;
    } else if (key.equals("H")) {
      halted = true;
    }
  }
  if (state.n == 0 || !have_mpos || !have_wpos || !have_c) return false;

  Span cparts[3];
  if (split(c, cparts, 3) < 3) return false;
  bool neg;
  int64_t mant, scale;
  if (!parse_decimal(cparts[2], neg, mant, scale)) return false;
  const bool inch = mant != 0;

  MachineStatus s{};
  s.state = parse_state(state);
  s.inch_mode = inch;
  s.playing = playing;
  s.halted = halted;
  const int64_t factor = inch ? 25400 : 1000;
  bool a_m = false, a_w = false;
  if (!parse_axes(mpos, factor, s.mpos_um, a_m)) return false;
  if (!parse_axes(wpos, factor, s.wpos_um, a_w)) return false;
  s.has_a = a_m && a_w;
  if (!s.has_a) {
    s.mpos_um[3] = 0;
    s.wpos_um[3] = 0;
  }
  out = s;
  return true;
}
