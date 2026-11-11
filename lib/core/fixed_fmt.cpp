#include "fixed_fmt.h"

namespace {

// Writes [-]int.frac with frac_digits decimals for |value| = mag / 10^frac_digits.
size_t write_fixed(bool neg, uint64_t mag, int frac_digits, char* out, size_t cap) {
  char tmp[32];
  size_t n = 0;
  for (int i = 0; i < frac_digits; ++i) {
    tmp[n++] = static_cast<char>('0' + mag % 10);
    mag /= 10;
  }
  tmp[n++] = '.';
  do {
    tmp[n++] = static_cast<char>('0' + mag % 10);
    mag /= 10;
  } while (mag > 0);
  if (neg) tmp[n++] = '-';
  if (out == nullptr || cap < n + 1) return 0;
  for (size_t i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
  out[n] = '\0';
  return n;
}

}  // namespace

size_t format_um(int32_t um, char* out, size_t cap) {
  const bool neg = um < 0;
  const uint64_t mag = neg ? static_cast<uint64_t>(-static_cast<int64_t>(um)) : static_cast<uint64_t>(um);
  return write_fixed(neg, mag, 3, out, cap);
}

size_t format_inch_e4(int32_t um, char* out, size_t cap) {
  const bool neg = um < 0;
  const uint64_t mag = neg ? static_cast<uint64_t>(-static_cast<int64_t>(um)) : static_cast<uint64_t>(um);
  // 1e-4 inch = 2.54 um  ->  units = um * 100 / 254, rounded half away from zero.
  const uint64_t units = (mag * 100 + 127) / 254;
  return write_fixed(neg && units != 0, units, 4, out, cap);
}
