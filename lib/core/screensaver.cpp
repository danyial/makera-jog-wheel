#include "screensaver.h"

#include "core_config.h"

ScreenTick ScreenSaver::feed(uint32_t now_ms, bool press, int32_t counts, bool busy, bool attention) {
  ScreenTick r{ScreenState::On, false, false, false};
  if (!started_) {
    started_ = true;
    last_ms_ = now_ms;
  }
  const bool asleep = state_ != ScreenState::On;

  bool input;
  if (!asleep) {
    input = press || counts != 0;
  } else {
    if (counts != 0) {
      if (sleep_counts_ != 0 && static_cast<uint32_t>(now_ms - sleep_count_ms_) >= STEP_RESET_MS) sleep_counts_ = 0;
      sleep_counts_ += counts;
      sleep_count_ms_ = now_ms;
    }
    const int32_t mag = sleep_counts_ < 0 ? -sleep_counts_ : sleep_counts_;
    input = press || mag >= WAKE_MIN_COUNTS;
  }
  if (input || attention) last_ms_ = now_ms;

  const uint32_t idle = static_cast<uint32_t>(now_ms - last_ms_);
  ScreenState next;
  if (input || attention || busy) {
    next = ScreenState::On;
  } else if (state_ == ScreenState::Off || idle >= DISPLAY_OFF_MS) {
    next = ScreenState::Off;  // Off stays Off until input or attention, also across the millis() wrap
  } else if (state_ == ScreenState::Saver || idle >= SCREENSAVER_MS) {
    next = ScreenState::Saver;
  } else {
    next = ScreenState::On;
  }

  if (asleep && input && !busy) {
    r.woke = true;
    in_window_ = true;
    wake_ms_ = now_ms;
  }
  if (in_window_ && static_cast<uint32_t>(now_ms - wake_ms_) >= WAKE_SWALLOW_MS) in_window_ = false;
  if (busy) in_window_ = false;
  r.swallow = !busy && (asleep || in_window_);
  if (next == ScreenState::On) sleep_counts_ = 0;

  if (next == ScreenState::Saver &&
      (state_ != ScreenState::Saver || static_cast<uint32_t>(now_ms - last_move_ms_) >= SAVER_MOVE_MS)) {
    r.move = true;
    last_move_ms_ = now_ms;
  }
  state_ = next;
  r.state = next;
  return r;
}
