#include "encoder.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <driver/rtc_io.h>

#include "core_config.h"
#include "pins.h"
#include "ui_config.h"

namespace encoder {
namespace {

pcnt_unit_handle_t unit = nullptr;
int last_raw = 0;
int32_t total_counts = 0;

constexpr gpio_num_t kPwr = static_cast<gpio_num_t>(PIN_ENC_PWR);
constexpr gpio_num_t kA = static_cast<gpio_num_t>(PIN_ENC_A);
constexpr gpio_num_t kB = static_cast<gpio_num_t>(PIN_ENC_B);

// Level first, then output: pinMode(OUTPUT) would drive LOW (wheel on) for a moment.
void set_power(bool on) {
  gpio_set_level(kPwr, on ? 0 : 1);  // AO3401 gate: LOW = wheel on
  gpio_set_direction(kPwr, GPIO_MODE_OUTPUT);
}

// rtc_gpio_* only applies to RTC-capable pins; pins.h decides which pins A and B are (ticket #25).
bool is_rtc(gpio_num_t p) { return rtc_gpio_is_valid_gpio(p); }

int read_raw() {
  int v = 0;
  if (unit != nullptr) pcnt_unit_get_count(unit, &v);
  return v;
}

}  // namespace

bool begin() {
  // Wheel on: AO3401 high-side switch, gate LOW = on. Edges while powering up must not count.
  // A shutdown before a brownout reset may have left A/B isolated and held (RTC domain survives).
  for (gpio_num_t p : {kA, kB}) {
    if (!is_rtc(p)) continue;
    rtc_gpio_hold_dis(p);
    rtc_gpio_deinit(p);
  }
  set_power(true);
  delay(ENC_POWER_SETTLE_MS);

  pcnt_unit_config_t ucfg = {};
  ucfg.low_limit = -PCNT_LIMIT;
  ucfg.high_limit = PCNT_LIMIT;
  ucfg.flags.accum_count = 1;
  if (pcnt_new_unit(&ucfg, &unit) != ESP_OK) return false;

  pcnt_glitch_filter_config_t fcfg = {};
  fcfg.max_glitch_ns = PCNT_GLITCH_NS;
  pcnt_unit_set_glitch_filter(unit, &fcfg);

  // Quadrature x4: channel 0 counts edges of A with level B, channel 1 edges of B with level A.
  pcnt_chan_config_t a_cfg = {};
  a_cfg.edge_gpio_num = PIN_ENC_A;
  a_cfg.level_gpio_num = PIN_ENC_B;
  pcnt_channel_handle_t chan_a = nullptr;
  if (pcnt_new_channel(unit, &a_cfg, &chan_a) != ESP_OK) return false;

  pcnt_chan_config_t b_cfg = {};
  b_cfg.edge_gpio_num = PIN_ENC_B;
  b_cfg.level_gpio_num = PIN_ENC_A;
  pcnt_channel_handle_t chan_b = nullptr;
  if (pcnt_new_channel(unit, &b_cfg, &chan_b) != ESP_OK) return false;

  // pcnt_new_channel() enables the internal pull-ups. A and B have external 10k pull-ups to the
  // SWITCHED wheel supply; internal ones to 3V3 would feed the unpowered wheel (SPEC 7.2).
  gpio_pullup_dis(kA);
  gpio_pullup_dis(kB);

  pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

  // Watch points at both limits are required for accum_count to extend the range.
  pcnt_unit_add_watch_point(unit, PCNT_LIMIT);
  pcnt_unit_add_watch_point(unit, -PCNT_LIMIT);

  if (pcnt_unit_enable(unit) != ESP_OK) return false;
  pcnt_unit_clear_count(unit);
  if (pcnt_unit_start(unit) != ESP_OK) return false;
  last_raw = read_raw();
  total_counts = 0;
  return true;
}

void power_off() { set_power(false); }

void isolate() {
  // No pull-ups, no leakage into the unpowered wheel during deep sleep. RTC pins are isolated
  // explicitly; digital pins are not held in deep sleep and their pull-ups are already disabled.
  for (gpio_num_t p : {kA, kB}) {
    if (is_rtc(p)) rtc_gpio_isolate(p);
  }
}

int32_t take_counts() {
  const int raw = read_raw();
  int32_t counts = static_cast<int32_t>(raw - last_raw);
  last_raw = raw;
  if (ENC_INVERT) counts = -counts;
  total_counts += counts;
  return counts;
}

int32_t total() { return total_counts; }

bool level_a() { return digitalRead(PIN_ENC_A) == HIGH; }
bool level_b() { return digitalRead(PIN_ENC_B) == HIGH; }

}  // namespace encoder
