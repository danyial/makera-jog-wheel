#include "display.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "core_config.h"
#include "fixed_fmt.h"
#include "pins.h"

// Screensaver picture (SPEC 10.4): the private logo if present (private repository only, made with
// tools/logo_to_xbm.py), otherwise the neutral "MPG". Both define LOGO_W, LOGO_H and LOGO_XBM.
#if __has_include("logo_bitmap.h")
#include "logo_bitmap.h"
#else
#include "logo_fallback.h"
#endif
static_assert(LOGO_W > 0 && LOGO_W <= 128 && LOGO_H > 0 && LOGO_H <= 64, "screensaver picture must fit the display");
static_assert(sizeof(LOGO_XBM) == static_cast<size_t>((LOGO_W + 7) / 8 * LOGO_H), "XBM size does not match LOGO_W x LOGO_H");

namespace display {
namespace {

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
uint32_t last_draw_ms = 0;
bool drawn_once = false;
bool powered_off = false;  // last setPowerSave() state, so the command goes out only on a change

const char* axis_name(AxisMode m) {
  switch (m) {
    case AxisMode::X: return "X";
    case AxisMode::Y: return "Y";
    case AxisMode::XY: return "XY";
    case AxisMode::Z: return "Z";
    case AxisMode::A: return "A";
    default: return "-";
  }
}

void fmt_pos(int32_t um, bool inch, char* out, size_t cap) {
  if (inch) {
    format_inch_e4(um, out, cap);
  } else {
    format_um(um, out, cap);
  }
}

}  // namespace

void begin() {
  u8g2.setI2CAddress(0x3C * 2);  // U8g2 expects the 8-bit address
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.setFontPosTop();
}

bool due(uint32_t now_ms) {
  if (!drawn_once || static_cast<uint32_t>(now_ms - last_draw_ms) >= 1000 / DISPLAY_MAX_HZ) {
    drawn_once = true;
    last_draw_ms = now_ms;
    return true;
  }
  return false;
}

void power_save(bool on) {
  if (on == powered_off) return;
  u8g2.setPowerSave(on ? 1 : 0);  // SH1106/SSD1306 DISPLAYOFF (0xAE) / DISPLAYON (0xAF)
  powered_off = on;
}

void show_saver() {
  // Random position, logo fully visible; esp_random() is a hardware RNG (no seeding needed).
  const int x = static_cast<int>(esp_random() % static_cast<uint32_t>(u8g2.getDisplayWidth() - LOGO_W + 1));
  const int y = static_cast<int>(esp_random() % static_cast<uint32_t>(u8g2.getDisplayHeight() - LOGO_H + 1));
  u8g2.clearBuffer();
  u8g2.drawXBMP(x, y, LOGO_W, LOGO_H, LOGO_XBM);
  u8g2.sendBuffer();
}

void show_start(const char* version) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(0, 8, "Makera Handrad");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 32, "Firmware");
  u8g2.drawStr(54, 32, version);
  u8g2.sendBuffer();
}

void show_portal(const char* ssid, const char* pass, const char* ip) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 0, "Einrichtung");
  u8g2.drawStr(0, 14, ssid);
  u8g2.drawStr(0, 28, "Passwort:");
  u8g2.drawStr(60, 28, pass);
  u8g2.drawStr(0, 42, ip);
  u8g2.sendBuffer();
}

void show_battery_empty() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawUTF8(0, 12, "Akku leer");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawUTF8(0, 36, "bitte ausschalten");
  u8g2.sendBuffer();
  power_save(false);  // after drawing: the screensaver may have switched the display off
}

void show_ota(const char* line, int percent) {
  char buf[8];
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(0, 8, "Update");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawUTF8(0, 30, line);
  if (percent >= 0 && percent <= 100) {
    u8g2.drawFrame(0, 48, 100, 10);
    u8g2.drawBox(0, 48, percent, 10);
    snprintf(buf, sizeof(buf), "%d%%", percent);
    u8g2.drawStr(104, 48, buf);
  }
  u8g2.sendBuffer();
  power_save(false);  // after drawing: the screensaver may have switched the display off
}

void show_wrong_board(unsigned board_rev, unsigned firmware_rev) {
  char buf[24];
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(0, 0, "Falsche Firmware");
  u8g2.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof(buf), "Platine Rev. %u", board_rev);
  u8g2.drawStr(0, 18, buf);
  snprintf(buf, sizeof(buf), "Firmware Rev. %u", firmware_rev);
  u8g2.drawStr(0, 29, buf);
  u8g2.drawUTF8(0, 42, "Platine gewechselt?");
  u8g2.drawUTF8(0, 53, "Board-Taste 3 s");
  u8g2.sendBuffer();
  power_save(false);
}

void show_wrong_board_done() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(0, 0, "Bestaetigt");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 20, "Taste loslassen,");
  u8g2.drawStr(0, 31, "dann Neustart");
  u8g2.sendBuffer();
  power_save(false);
}

void show_diag(const DiagData& d) {
  char buf[32];
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof(buf), "Diagnose  Platine %d", BOARD_REV);  // board revision (ticket #49)
  u8g2.drawStr(0, 0, buf);
  snprintf(buf, sizeof(buf), "Zaehler %ld", static_cast<long>(d.enc_total));
  u8g2.drawStr(0, 12, buf);
  snprintf(buf, sizeof(buf), "A=%d B=%d", d.enc_a ? 1 : 0, d.enc_b ? 1 : 0);
  u8g2.drawStr(0, 22, buf);
  snprintf(buf, sizeof(buf), "roh X%d Y%d Z%d A%d B%d", d.raw[0], d.raw[1], d.raw[2], d.raw[4], d.raw[3]);
  u8g2.drawStr(0, 32, buf);
  snprintf(buf, sizeof(buf), "ent X%d Y%d Z%d A%d B%d", d.stable[0], d.stable[1], d.stable[2], d.stable[4],
           d.stable[3]);
  u8g2.drawStr(0, 42, buf);
  snprintf(buf, sizeof(buf), "Akku %u mV", static_cast<unsigned>(d.bat_mv));
  u8g2.drawStr(0, 52, buf);
  u8g2.sendBuffer();
}

void show_main(const MainData& d) {
  char buf[24];
  u8g2.clearBuffer();

  // Top line: axis mode, step size, WiFi bars, battery.
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 0, axis_name(d.axis));
  format_um(STEP_UM[d.step_level < 3 ? d.step_level : 0], buf, sizeof(buf));
  // "0.010" -> "0.01" (two decimals are enough for 10/50/100 um)
  size_t n = strlen(buf);
  if (n > 0 && buf[n - 1] == '0') buf[n - 1] = '\0';
  u8g2.drawStr(18, 0, buf);
  for (int i = 0; i < 3; ++i) {
    const int h = 3 + 3 * i;
    if (d.rssi_bars > i) {
      u8g2.drawBox(70 + 5 * i, 9 - h, 3, h);
    } else {
      u8g2.drawFrame(70 + 5 * i, 9 - h, 3, h);
    }
  }
  if (d.bat_absent) {
    u8g2.drawStr(100, 0, "USB");
  } else {
    snprintf(buf, sizeof(buf), "%s%u%%", d.bat_warn ? "!" : "", static_cast<unsigned>(d.bat_percent));
    u8g2.drawStr(128 - u8g2.getStrWidth(buf), 0, buf);
  }

  // Positions (WPos).
  if (d.have_status) {
    const bool inch = d.status.inch_mode;
    const char* names[4] = {"X", "Y", "Z", "A"};
    int axes[4];
    int count = 0;
    switch (d.axis) {
      case AxisMode::X: axes[count++] = 0; break;
      case AxisMode::Y: axes[count++] = 1; break;
      case AxisMode::XY: axes[count++] = 0; axes[count++] = 1; break;
      case AxisMode::Z: axes[count++] = 2; break;
      case AxisMode::A: axes[count++] = 3; break;
      default:
        axes[0] = 0; axes[1] = 1; axes[2] = 2; count = 3;
        if (d.axis_a_enabled && d.status.has_a) axes[count++] = 3;
        break;
    }
    if (d.axis == AxisMode::None) {
      u8g2.setFont(u8g2_font_6x10_tf);
      for (int i = 0; i < count; ++i) {
        const int a = axes[i];
        if (a == 3) {
          format_um(d.status.wpos_um[3], buf, sizeof(buf));  // degrees, never inch
        } else {
          fmt_pos(d.status.wpos_um[a], inch, buf, sizeof(buf));
        }
        u8g2.drawStr(0, 12 + 10 * i, names[a]);
        u8g2.drawStr(110 - u8g2.getStrWidth(buf), 12 + 10 * i, buf);
      }
    } else {
      u8g2.setFont(u8g2_font_9x15B_tf);
      for (int i = 0; i < count; ++i) {
        const int a = axes[i];
        if (a == 3) {
          format_um(d.status.wpos_um[3], buf, sizeof(buf));
        } else {
          fmt_pos(d.status.wpos_um[a], inch, buf, sizeof(buf));
        }
        u8g2.drawStr(0, 13 + 17 * i, names[a]);
        u8g2.drawStr(118 - u8g2.getStrWidth(buf), 13 + 17 * i, buf);
      }
    }
  }

  // Bottom line: state / block reason, unit.
  u8g2.setFont(u8g2_font_6x10_tf);
  if (d.line != nullptr) u8g2.drawUTF8(0, 54, d.line);
  if (d.bar_permille >= 0) {
    // Unlock hold (8.10): frame from after the text to the right edge, filled by the progress.
    const int x0 = (d.line != nullptr ? u8g2.getUTF8Width(d.line) : 0) + 4;
    const int w = 128 - x0;
    const int p = d.bar_permille > 1000 ? 1000 : d.bar_permille;
    u8g2.drawFrame(x0, 55, w, 8);
    u8g2.drawBox(x0, 55, w * p / 1000, 8);
  }
  if (d.full_line || d.bar_permille >= 0) {
    u8g2.sendBuffer();
    return;
  }
  const char* unit = (d.have_status && d.status.inch_mode) ? "in" : "mm";
  if (d.axis == AxisMode::A) unit = "deg";
  u8g2.drawStr(128 - u8g2.getStrWidth(unit), 54, unit);
  u8g2.sendBuffer();
}

}  // namespace display
