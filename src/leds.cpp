#include "leds.h"

#include <Arduino.h>
#include <driver/rtc_io.h>

#include "core_config.h"
#include "pins.h"

namespace leds {
namespace {

constexpr int kPins[4] = {PIN_LED_X, PIN_LED_Y, PIN_LED_Z, PIN_LED_A};

// Only RTC-capable GPIOs can be held/isolated; decided at runtime so pins.h stays the only pin source.
bool is_rtc(int p) { return rtc_gpio_is_valid_gpio(static_cast<gpio_num_t>(p)); }
constexpr uint8_t kResolutionBits = 8;
constexpr uint32_t kDuty = (255u * LED_DUTY_PERCENT) / 100u;
bool attached = false;
bool state[4] = {false, false, false, false};

}  // namespace

void begin() {
  for (int p : kPins) {
    if (!is_rtc(p)) continue;
    // A shutdown before a brownout reset may have left the pins isolated and held (RTC domain survives).
    rtc_gpio_hold_dis(static_cast<gpio_num_t>(p));
    rtc_gpio_deinit(static_cast<gpio_num_t>(p));
  }
  for (int p : kPins) {
    ledcAttach(p, LED_PWM_HZ, kResolutionBits);
    // Low-active wiring: invert the output once; the duty is never inverted additionally.
    if (!LED_ACTIVE_HIGH) ledcOutputInvert(p, true);
    ledcWrite(p, 0);
  }
  attached = true;
}

void set(bool x, bool y, bool z, bool a) {
  if (!attached) return;
  const bool want[4] = {x, y, z, a};
  for (int i = 0; i < 4; ++i) {
    if (want[i] != state[i]) {
      ledcWrite(kPins[i], want[i] ? kDuty : 0);
      state[i] = want[i];
    }
  }
}

void shutdown() {
  for (int p : kPins) {
    if (attached) ledcDetach(p);
    pinMode(p, OUTPUT);
    digitalWrite(p, LED_ACTIVE_HIGH ? LOW : HIGH);
  }
  attached = false;
  // Isolate the RTC-capable LED pins (pins.h: all of them); gpio_reset_pin would enable pull-ups. Then an RTC
  // pull-down under the hold: the LED stays off and strapping pins (GPIO12/15 on board rev 2) stay LOW.
  for (int p : kPins) {
    if (!is_rtc(p)) continue;
    const gpio_num_t g = static_cast<gpio_num_t>(p);
    rtc_gpio_isolate(g);  // disconnects the pad, disables pulls, enables hold
    rtc_gpio_hold_dis(g);
    rtc_gpio_pulldown_en(g);
    rtc_gpio_hold_en(g);
  }
}

}  // namespace leds
