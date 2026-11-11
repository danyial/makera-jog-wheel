#pragma once
// Fixed-point formatting without printf/locale; '.' as separator (SPEC 8.2).
#include <cstddef>
#include <cstdint>

// um -> mm with exactly 3 decimals: -5 -> "-0.005". Returns length (without NUL), 0 if cap too small.
size_t format_um(int32_t um, char* out, size_t cap);
// um -> inch with exactly 4 decimals, rounded half away from zero: 25400 -> "1.0000", -1270 -> "-0.0500".
// A result that rounds to 0 has no minus sign: -1 -> "0.0000".
size_t format_inch_e4(int32_t um, char* out, size_t cap);
