#pragma once
// SH1106 128x64 via U8g2 (SPEC 4, 10). Rendering only; no logic.
#include <cstdint>

#include "axis_select.h"
#include "jog_gate.h"
#include "status.h"

namespace display {

void begin();
// true when the next frame may be drawn (at most DISPLAY_MAX_HZ).
bool due(uint32_t now_ms);

void show_start(const char* version);
void show_portal(const char* ssid, const char* pass, const char* ip);
void show_battery_empty();  // also switches a display in power save back on
// true = display off (power save, SH1106 DISPLAYOFF), false = on. Sends only on a change.
void power_save(bool on);
// Screensaver (SPEC 10.4): picture (private logo or neutral "MPG") at a new random position, rest black.
void show_saver();
// OTA update (SPEC 9.5): "Update", a status line and, for percent 0..100, a progress bar.
// Also switches a display in power save back on.
void show_ota(const char* line, int percent);
// Board revision guard (SPEC 9.2): firmware for another carrier board, safe mode with the confirm hint.
void show_wrong_board(unsigned board_rev, unsigned firmware_rev);
// Short status line in safe mode (e.g. after the confirmation).
void show_wrong_board_done();

struct DiagData {
  int32_t enc_total;
  bool raw[5];     // X, Y, Z, board, A
  bool stable[5];
  uint16_t bat_mv;
  bool enc_a;
  bool enc_b;
};
void show_diag(const DiagData& d);

struct MainData {
  AxisMode axis;
  uint8_t step_level;     // 0..2
  int8_t rssi_bars;       // -1 = no WiFi, 0..3
  bool bat_absent;
  bool bat_warn;
  uint8_t bat_percent;
  bool have_status;
  MachineStatus status;
  const char* line;       // bottom line text (state / block reason / alarm)
  bool axis_a_enabled;    // show A in the overview (axis None)
  bool full_line;         // unlock line (8.10): uses all 21 characters, no unit
  int16_t bar_permille;   // unlock hold progress 0..1000 as a bar after the line text, -1 = none
};
void show_main(const MainData& d);

}  // namespace display
