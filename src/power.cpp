#include "power.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

#include "core_config.h"
#include "display.h"
#include "encoder.h"
#include "inputs.h"
#include "leds.h"
#include "machine_link.h"
#include "pins.h"
#include "ui_config.h"

namespace power {

uint16_t measure_battery_mv() {
  uint32_t sum = 0;
  for (int i = 0; i < BAT_SAMPLES; ++i) sum += analogReadMilliVolts(PIN_BAT_ADC);
  return static_cast<uint16_t>((sum / BAT_SAMPLES) * 2);
}

void shutdown(SleepReason reason) {
  // 1. No more commands: close the socket in order and give the FIN POWER_OFF_FLUSH_MS to leave before WiFi
  //    stops (harmless if WiFi never started). The caller has already ended the jog session.
  machine::stop();
  delay(POWER_OFF_FLUSH_MS);
  esp_wifi_stop();
  // 2. Wheel off (gate HIGH; in deep sleep GPIO18 is high impedance and the 100k pull-up keeps the AO3401
  //    off; the pin is never held LOW), LEDs off.
  encoder::power_off();
  leds::set(false, false, false, false);
  // 3. Battery: message for 5 s. Then display power save in both cases.
  if (reason == SleepReason::BatteryEmpty) {
    display::show_battery_empty();
    delay(UI_BAT_EMPTY_SHOW_MS);
  }
  display::power_save(true);
  // 4. LED pins LOW and RTC GPIOs isolated; also wheel A/B (no pull-ups into the unpowered wheel).
  leds::shutdown();
  encoder::isolate();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  // 5. Wake only by the on/off button: ext0 (level triggered), RTC pull-up kept on during deep sleep.
  const bool on_now = inputs::power_raw_on();
  const bool wake_on = sleep_wake_when_on(reason, on_now);
  const int wake_level = (wake_on == POWER_SW_ACTIVE_LOW) ? 0 : 1;
  const gpio_num_t pwr = static_cast<gpio_num_t>(PIN_PWR_SW);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext0_wakeup(pwr, wake_level);
  rtc_gpio_pullup_en(pwr);
  rtc_gpio_pulldown_dis(pwr);
  Serial.printf("Ein/Aus: Tiefschlaf, Wecken mit Taste %s\n", wake_on ? "ein" : "aus");
  Serial.flush();
  esp_deep_sleep_start();
}

}  // namespace power
