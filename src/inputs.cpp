#include "inputs.h"

#include <Arduino.h>
#include <driver/rtc_io.h>

#include "core_config.h"
#include "pins.h"

namespace inputs {
namespace {

struct Debouncer {
  bool stable = false;
  bool candidate = false;
  uint32_t since_ms = 0;
  bool raw = false;
};

const int kPins[COUNT] = {PIN_BTN_X, PIN_BTN_Y, PIN_BTN_Z, PIN_BOARD_BTN, PIN_BTN_A};
Debouncer deb[COUNT];
bool boot_held[COUNT];

bool read_pressed(int i) {
  const bool high = digitalRead(kPins[i]) == HIGH;
  if (i == BTN_BOARD) return !high;  // board button: pressed = LOW
  return BUTTON_ACTIVE_LOW ? !high : high;
}

}  // namespace

void begin() {
  // Input-only GPIOs (34-39) have external 10k pull-ups on the board, all others use the internal one.
  for (int p : {PIN_BTN_X, PIN_BTN_Y, PIN_BTN_Z, PIN_BTN_A}) pinMode(p, pin_has_internal_pullup(p) ? INPUT_PULLUP : INPUT);
  pinMode(PIN_BOARD_BTN, INPUT_PULLUP);
  // On/off button: after a deep sleep the pin is still an RTC IO (ext0 wake, RTC pull-up); hand it back to
  // the digital GPIO matrix first. Internal pull-up only (no external resistor on the board).
  const gpio_num_t pwr = static_cast<gpio_num_t>(PIN_PWR_SW);
  if (rtc_gpio_is_valid_gpio(pwr)) {
    rtc_gpio_hold_dis(pwr);
    rtc_gpio_deinit(pwr);
  }
  pinMode(PIN_PWR_SW, INPUT_PULLUP);
  delay(1);
  // A button counts as held only if it reads pressed during the whole boot window.
  bool all[COUNT];
  for (int i = 0; i < COUNT; ++i) all[i] = true;
  const uint32_t t0 = millis();
  while (static_cast<uint32_t>(millis() - t0) < BOOT_DEBOUNCE_MS) {
    for (int i = 0; i < COUNT; ++i) all[i] = all[i] && read_pressed(i);
    delay(1);
  }
  const uint32_t now = millis();
  for (int i = 0; i < COUNT; ++i) {
    boot_held[i] = all[i];
    deb[i].stable = all[i];
    deb[i].candidate = all[i];
    deb[i].raw = all[i];
    deb[i].since_ms = now;
  }
}

bool held_at_boot(Id id) { return boot_held[id]; }

uint8_t poll(uint32_t now_ms) {
  uint8_t edges = 0;
  for (int i = 0; i < COUNT; ++i) {
    Debouncer& d = deb[i];
    d.raw = read_pressed(i);
    if (d.raw != d.candidate) {
      d.candidate = d.raw;
      d.since_ms = now_ms;
    } else if (d.candidate != d.stable && static_cast<uint32_t>(now_ms - d.since_ms) >= DEBOUNCE_MS) {
      d.stable = d.candidate;
      if (d.stable) edges |= static_cast<uint8_t>(1u << i);
    }
  }
  return edges;
}

bool raw_pressed(Id id) { return deb[id].raw; }

bool power_raw_on() {
  const bool high = digitalRead(PIN_PWR_SW) == HIGH;
  return POWER_SW_ACTIVE_LOW ? !high : high;
}
bool stable_pressed(Id id) { return deb[id].stable; }

}  // namespace inputs
