#pragma once
// Handwheel via ESP-IDF PCNT (SPEC 7.2, 8.7 step 1).
#include <cstdint>

namespace encoder {

// Switches the wheel supply on, waits ENC_POWER_SETTLE_MS, then starts PCNT and reads the start count.
bool begin();
// Wheel supply off (PIN_ENC_PWR HIGH). Used by shutdown.
void power_off();
// Isolates the A/B pins for deep sleep (shutdown, SPEC 9.3).
void isolate();
// Counts since the previous call, sign corrected by ENC_INVERT. Never clears the hardware counter.
int32_t take_counts();
// Accumulated count since begin(), after ENC_INVERT (diagnosis screen, H-4).
int32_t total();
// Raw GPIO levels of A and B (diagnosis screen).
bool level_a();
bool level_b();

}  // namespace encoder
