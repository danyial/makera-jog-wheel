#include "axis_select.h"

#include "core_config.h"

void AxisSelector::expire(uint32_t now_ms) {
  if (mode_ != AxisMode::None && static_cast<uint32_t>(now_ms - last_activity_ms_) >= AXIS_IDLE_CLEAR_MS) {
    mode_ = AxisMode::None;
  }
}

void AxisSelector::on_press(Button b, uint32_t now_ms) {
  expire(now_ms);
  last_activity_ms_ = now_ms;
  switch (b) {
    case Button::X:
      switch (mode_) {
        case AxisMode::X:  mode_ = AxisMode::None; break;
        case AxisMode::Y:  mode_ = AxisMode::XY; break;
        case AxisMode::XY: mode_ = AxisMode::Y; break;
        default:           mode_ = AxisMode::X; break;  // None, Z, A
      }
      break;
    case Button::Y:
      switch (mode_) {
        case AxisMode::Y:  mode_ = AxisMode::None; break;
        case AxisMode::X:  mode_ = AxisMode::XY; break;
        case AxisMode::XY: mode_ = AxisMode::X; break;
        default:           mode_ = AxisMode::Y; break;  // None, Z, A
      }
      break;
    case Button::Z:
      mode_ = (mode_ == AxisMode::Z) ? AxisMode::None : AxisMode::Z;
      break;
    case Button::A:
      mode_ = (mode_ == AxisMode::A) ? AxisMode::None : AxisMode::A;
      break;
  }
}

void AxisSelector::on_activity(uint32_t now_ms) {
  // Expire first: turning the wheel after the idle period must not revive a stale selection.
  expire(now_ms);
  last_activity_ms_ = now_ms;
}

void AxisSelector::clear() { mode_ = AxisMode::None; }

AxisMode AxisSelector::mode(uint32_t now_ms) {
  expire(now_ms);
  return mode_;
}
