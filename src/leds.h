#pragma once
// Axis button LED rings via LEDC PWM (SPEC 7.4, 9.4).
#include <cstdint>

namespace leds {

void begin();
// on/off per LED (X, Y, Z, A); brightness LED_DUTY_PERCENT.
void set(bool x, bool y, bool z, bool a);
// LEDC off, pins LOW, RTC GPIOs isolated (shutdown, SPEC 9.3 step 4).
void shutdown();

}  // namespace leds
