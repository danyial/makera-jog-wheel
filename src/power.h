#pragma once
// Battery measurement, shutdown and deep sleep (SPEC 9.3).
#include <cstdint>

#include "power_switch.h"

namespace power {

// Mean of BAT_SAMPLES readings of the on-board 1M/1M divider, scaled x2, in mV.
uint16_t measure_battery_mv();
// One path into deep sleep for both reasons (SPEC 9.3): link and WiFi off, wheel and LEDs off, display power
// save (BatteryEmpty first shows "Akku leer" for 5 s), RTC GPIOs isolated, ext0 wake on the on/off button
// (level from sleep_wake_when_on). The caller has ended the jog session before. Never returns.
[[noreturn]] void shutdown(SleepReason reason);

}  // namespace power
