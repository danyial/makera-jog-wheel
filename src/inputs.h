#pragma once
// Axis buttons and board button, polled and debounced (SPEC 7.3, 8.4, 9.2 step 2).
#include <cstdint>

namespace inputs {

enum Id : uint8_t { BTN_X = 0, BTN_Y = 1, BTN_Z = 2, BTN_BOARD = 3, BTN_A = 4, COUNT = 5 };

// Configures the pins and samples all inputs for BOOT_DEBOUNCE_MS. The result initializes the
// debouncers, so a button held at power-on produces no press edge.
void begin();
// State determined by begin().
bool held_at_boot(Id id);
// Polls all inputs. Returns a bitmask (1 << Id) of debounced press edges since the last poll.
uint8_t poll(uint32_t now_ms);
bool raw_pressed(Id id);     // last raw sample
bool stable_pressed(Id id);  // debounced state

// On/off button (soft power, SPEC 9.3, ticket #49), configured by begin(). Raw level, true = on
// (contact closed). Debounced by PowerSwitch (lib/core) in main.cpp, not here.
bool power_raw_on();

}  // namespace inputs
