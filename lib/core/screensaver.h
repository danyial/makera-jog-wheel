#pragma once
// Screensaver against OLED burn-in (SPEC 10.4, ticket #28). Pure state logic, no drawing.
#include <cstdint>

enum class ScreenState : uint8_t { On, Saver, Off };

struct ScreenTick {
  ScreenState state;
  bool swallow;  // discard this tick's press edges and encoder counts (asleep, waking, or wake window)
  bool woke;     // an input woke the display in this tick
  bool move;     // Saver: draw the logo at a new position now (on entering Saver, then every SAVER_MOVE_MS)
};

class ScreenSaver {
 public:
  // Called once per tick, before the inputs reach AxisSelector and the planner.
  //   press:     a button press edge in this tick
  //   counts:    encoder counts of this tick
  //   busy:      an axis is selected or a jog session is active -> always On, never swallowed
  //   attention: a state change that needs the user's eye (link lost, battery warning, alarm)
  // While On, any press or counts != 0 is input. While Saver/Off, a press wakes; counts wake only once
  // |sum| >= WAKE_MIN_COUNTS (the sum restarts after STEP_RESET_MS without counts), so a single noise
  // count does not wake. Everything is swallowed while asleep, in the waking tick and for WAKE_SWALLOW_MS
  // after it. Attention wakes without swallowing. Input and attention restart the idle timer.
  ScreenTick feed(uint32_t now_ms, bool press, int32_t counts, bool busy, bool attention);
  ScreenState state() const { return state_; }

 private:
  ScreenState state_ = ScreenState::On;
  bool started_ = false;
  uint32_t last_ms_ = 0;       // last input or attention
  uint32_t last_move_ms_ = 0;  // last logo move in Saver
  bool in_window_ = false;     // wake window active
  uint32_t wake_ms_ = 0;       // when an input woke the display
  int32_t sleep_counts_ = 0;   // counts summed while asleep
  uint32_t sleep_count_ms_ = 0;
};
