#pragma once
// Axis selection from three momentary buttons (SPEC 8.4).
#include <cstdint>

enum class AxisMode : uint8_t { None, X, Y, XY, Z, A };
enum class Button : uint8_t { X, Y, Z, A };

class AxisSelector {
 public:
  void on_press(Button b, uint32_t now_ms);  // debounced press edge; applies auto-clear first
  void on_activity(uint32_t now_ms);         // call when PCNT counts != 0; applies auto-clear first
  void clear();                              // deselect all
  AxisMode mode(uint32_t now_ms);            // applies auto-clear

 private:
  void expire(uint32_t now_ms);
  AxisMode mode_ = AxisMode::None;
  uint32_t last_activity_ms_ = 0;
};
