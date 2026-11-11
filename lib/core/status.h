#pragma once
// Status report parser (SPEC 8.2, 8.3). Positions are always integer micrometres.
#include <cstddef>
#include <cstdint>

enum class MachineState : uint8_t { Idle, Run, Hold, Pause, Alarm, Home, Tool, Sleep, Wait, Unknown };

struct MachineStatus {
  MachineState state;
  int32_t mpos_um[4];  // X,Y,Z machine position, ALWAYS micrometres; [3] = A in millidegrees
  int32_t wpos_um[4];  // X,Y,Z work position, ALWAYS micrometres; [3] = A in millidegrees
  bool has_a;          // MPos and WPos carry a 4th value (rotary axis A)
  bool inch_mode;      // field C, 3rd value
  bool playing;        // field P present
  bool halted;         // field H present
};

// false = "no status" (malformed, no '<...>', mandatory field MPos, WPos or C missing).
bool parse_status(const char* text, size_t len, MachineStatus& out);
