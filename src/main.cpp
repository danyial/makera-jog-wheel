// AppMode state machine and main loop (SPEC 9.2, 8.7). One cooperative loop, no own tasks.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "axis_select.h"
#include "battery.h"
#include "board_rev.h"
#include "core_config.h"
#include "display.h"
#include "encoder.h"
#include "detents.h"
#include "inputs.h"
#include "jog_planner.h"
#include "leds.h"
#include "jog_gate.h"
#include "machine_link.h"
#include "ota.h"
#include "pins.h"
#include "portal.h"
#include "power.h"
#include "power_switch.h"
#include "screensaver.h"
#include "settings.h"
#include "settings_rules.h"
#include "status_json.h"
#include "step_policy.h"
#include "ui_config.h"
#include "unlock.h"
#include "webui.h"

enum class AppMode : uint8_t { Normal, Portal, Diagnose, Shutdown };

RTC_NOINIT_ATTR uint32_t rtc_next_mode_magic;  // survives esp_restart(), undefined after power-on
RTC_NOINIT_ATTR uint8_t rtc_next_mode;         // valid only if rtc_next_mode_magic == NEXT_MODE_MAGIC
RTC_NOINIT_ATTR uint32_t rtc_brownout_magic;   // brownout counter, valid only with NEXT_MODE_MAGIC
RTC_NOINIT_ATTR uint8_t rtc_brownout_count;

namespace {

AppMode mode = AppMode::Normal;
Battery battery;
PowerSwitch power_switch(POWER_SW_DEBOUNCE_MS);  // on/off button, soft power (SPEC 9.3, ticket #49)
uint32_t last_bat_ms = 0;
uint32_t last_tick_ms = 0;
bool start_screen = false;
uint32_t start_screen_ms = 0;
settings::Settings cfg;

// Normal mode state (SPEC 8.7).
AxisSelector selector;
AxisMode axis = AxisMode::None;
machine::LinkState prev_link = machine::LinkState::Idle;
BlockReason gate = BlockReason::AxisNone;
DetentAccumulator accumulator(COUNTS_PER_DETENT);
StepPolicy step_policy;
JogPlanner planner;
bool have_clipped = false;
uint32_t last_clipped_ms = 0;

// Screensaver (SPEC 10.4, ticket #28).
ScreenSaver screen;
ScreenState screen_state = ScreenState::On;
bool saver_move = false;  // Saver: draw the picture at a new position at the next display update
bool attention = false;   // attention event since the last tick, consumed by the next tick

// Unlock after Alarm/HALT (SPEC 8.10, ticket #41).
UnlockRequest unlock;
UnlockTick unlock_view{false, UnlockView::None, 0, nullptr};  // result of the last tick, for display and web
// Texts name the hold time; keep them in step with UNLOCK_HOLD_MS.
static_assert(UNLOCK_HOLD_MS == 5000, "unlock texts say 5 s");
constexpr const char* UNLOCK_HINT_TEXT = "Entsperren: Taste 5 s";      // display, 21 characters (10.1)
constexpr const char* UNLOCK_HINT_WEB = "Alarm, Taste 5 s: Entsperren";  // web UI gate field
constexpr const char* UNLOCK_HOLD_WEB = "Alarm, Entsperren läuft";      // web UI gate field while held
constexpr const char* UNLOCK_HOLD_TEXT = "Entsperren";
constexpr const char* UNLOCK_SENT_TEXT = "Entsperren...";

// An alarm/error line of the machine has priority on the bottom line for ALARM_SHOW_MS (10.1).
bool alarm_line_shown(uint32_t now) {
  return machine::alarm_line() != nullptr && static_cast<uint32_t>(now - machine::alarm_ms()) < ALARM_SHOW_MS;
}

// Link loss and alarm/error lines (SPEC 8.8).
void on_session_end(uint32_t now) { planner.end_session(now); }

const char* block_text(BlockReason r, bool session_active) {
  switch (r) {
    case BlockReason::None: return session_active ? "Fährt" : "Bereit";
    case BlockReason::BatteryLow: return "Akku leer";
    case BlockReason::NoWifi: return "WLAN...";
    case BlockReason::NoMachine: return "Maschine...";
    case BlockReason::NoData: return "Keine Daten";
    case BlockReason::AxisNone: return "Achse wählen";
    case BlockReason::NoAxisA: return "Keine A-Achse";
    case BlockReason::JobRunning: return "Job läuft";
    case BlockReason::Alarm: return "Alarm";
    case BlockReason::NoRates: return "Raten unbekannt";
    case BlockReason::MachineBusy: return "Maschine belegt";
  }
  return "";
}

void restart_into_diag() {
  rtc_next_mode = NEXT_MODE_DIAG;
  rtc_next_mode_magic = NEXT_MODE_MAGIC;
  esp_restart();
}

void sample_battery(uint32_t now) {
  if (static_cast<uint32_t>(now - last_bat_ms) >= BAT_INTERVAL_MS) {
    last_bat_ms = now;
    battery.add_sample(power::measure_battery_mv(), false);
  }
}

uint32_t last_diag_log_ms = 0;
uint32_t max_loop_ms = 0;     // longest loop() pass since the last statistics line
uint32_t max_draw_ms = 0;     // longest display update since the last statistics line
uint32_t max_web_us = 0;      // longest webui::loop() step since the last statistics line (ticket #17)
uint64_t loop_sum_us = 0;     // sum and count of loop() passes for the mean (ticket #17)
uint32_t loop_count = 0;
StatsView last_stats{};       // last completed 5 s window, shown in the web UI
bool have_last_stats = false;

void reset_loop_stats() {
  max_loop_ms = 0;
  max_draw_ms = 0;
  max_web_us = 0;
  loop_sum_us = 0;
  loop_count = 0;
}

void loop_diag(uint32_t now) {
  inputs::poll(now);
  encoder::take_counts();
  // Once per second on the serial console as well, for checks without a display (ticket #15).
  if (static_cast<uint32_t>(now - last_diag_log_ms) >= 1000) {
    last_diag_log_ms = now;
    Serial.printf("Diag: Platine Rev. %d | Zaehler %ld A=%d B=%d | roh X%d Y%d Z%d A%d Board%d | ent X%d Y%d Z%d A%d Board%d | Akku %u mV\n",
                  BOARD_REV, static_cast<long>(encoder::total()), encoder::level_a(), encoder::level_b(),
                  inputs::raw_pressed(inputs::BTN_X), inputs::raw_pressed(inputs::BTN_Y),
                  inputs::raw_pressed(inputs::BTN_Z), inputs::raw_pressed(inputs::BTN_A),
                  inputs::raw_pressed(inputs::BTN_BOARD), inputs::stable_pressed(inputs::BTN_X),
                  inputs::stable_pressed(inputs::BTN_Y), inputs::stable_pressed(inputs::BTN_Z),
                  inputs::stable_pressed(inputs::BTN_A), inputs::stable_pressed(inputs::BTN_BOARD),
                  static_cast<unsigned>(battery.last_mv()));
  }
  if (!display::due(now)) return;
  display::DiagData d{};
  d.enc_total = encoder::total();
  for (int i = 0; i < inputs::COUNT; ++i) {
    d.raw[i] = inputs::raw_pressed(static_cast<inputs::Id>(i));
    d.stable[i] = inputs::stable_pressed(static_cast<inputs::Id>(i));
  }
  d.bat_mv = battery.last_mv();
  d.enc_a = encoder::level_a();
  d.enc_b = encoder::level_b();
  display::show_diag(d);
}

// true during the "on" half of a blink period at hz.
bool blink_phase(uint32_t now, uint32_t hz) { return (now % (1000 / hz)) < (500 / hz); }

// LED table of SPEC 9.4.
void update_leds(uint32_t now) {
  switch (mode) {
    case AppMode::Normal: {
      const bool sx = axis == AxisMode::X || axis == AxisMode::XY;
      const bool sy = axis == AxisMode::Y || axis == AxisMode::XY;
      const bool sz = axis == AxisMode::Z;
      const bool sa = axis == AxisMode::A;
      const bool lit = gate == BlockReason::None || blink_phase(now, UI_LED_BLINK_NORMAL_HZ);
      leds::set(sx && lit, sy && lit, sz && lit, sa && lit);
      break;
    }
    case AppMode::Portal: {
      const bool on = blink_phase(now, UI_LED_BLINK_PORTAL_HZ);
      leds::set(on, on, on, on && cfg.axis_a);
      break;
    }
    case AppMode::Diagnose:
      leds::set(inputs::stable_pressed(inputs::BTN_X), inputs::stable_pressed(inputs::BTN_Y),
                inputs::stable_pressed(inputs::BTN_Z), inputs::stable_pressed(inputs::BTN_A));
      break;
    case AppMode::Shutdown:
      leds::set(false, false, false, false);
      break;
  }
}

// Both ways into deep sleep (SPEC 9.3) end the jog session before anything else; power::shutdown() then closes
// the machine socket in order, so no further $J goes out.
[[noreturn]] void enter_shutdown() {
  mode = AppMode::Shutdown;
  planner.end_session(millis());
  Serial.println("Akku leer, Tiefschlaf");
  power::shutdown(SleepReason::BatteryEmpty);
}

[[noreturn]] void enter_switch_off() {
  mode = AppMode::Shutdown;
  planner.end_session(millis());
  Serial.println("Ein/Aus: aus, Tiefschlaf");
  power::shutdown(SleepReason::SwitchOff);
}

// Boot (SPEC 9.2 step 2a): sample the on/off button until its level is stable for POWER_SW_DEBOUNCE_MS, at most
// POWER_SW_BOOT_MAX_MS. Unknown after that (noise): normal start, the loop keeps debouncing.
PowerLevel boot_power_level() {
  const uint32_t t0 = millis();
  PowerLevel level = PowerLevel::Unknown;
  while (level == PowerLevel::Unknown && static_cast<uint32_t>(millis() - t0) < POWER_SW_BOOT_MAX_MS) {
    level = power_switch.update(millis(), inputs::power_raw_on()).level;
    if (level == PowerLevel::Unknown) delay(1);
  }
  return level;
}

void draw_main(uint32_t now) {
  display::MainData d{};
  d.axis = axis;
  d.step_level = step_policy.level();
  d.rssi_bars = machine::rssi_bars();
  d.bat_absent = battery.absent();
  d.bat_warn = battery.warn();
  d.bat_percent = battery.percent();
  d.have_status = machine::have_status();
  if (d.have_status) d.status = machine::status();
  d.axis_a_enabled = cfg.axis_a;
  d.bar_permille = -1;
  d.line = block_text(gate, planner.session_active());
  if (have_clipped && static_cast<uint32_t>(now - last_clipped_ms) < CLIPPED_SHOW_MS) d.line = "Begrenzt";
  // Unlock (8.10): the hint replaces the gate text (in Alarm it would mostly say "Achse wählen"); the alarm line
  // keeps its priority over the hint, a running hold and "Entsperren..." have priority over everything.
  if (unlock_view.view == UnlockView::Hint) {
    d.line = UNLOCK_HINT_TEXT;
    d.full_line = true;
  }
  if (alarm_line_shown(now)) {
    d.line = machine::alarm_line();
    d.full_line = false;
  }
  if (unlock_view.view == UnlockView::Holding) {
    d.line = UNLOCK_HOLD_TEXT;
    d.bar_permille = static_cast<int16_t>(unlock_view.permille);
  } else if (unlock_view.view == UnlockView::Sent) {
    d.line = UNLOCK_SENT_TEXT;
    d.full_line = true;
  }
  display::show_main(d);
}

// Attention events wake the display (SPEC 10.4): battery warning, a new alarm/error line from the
// machine, the machine status entering Alarm or halted. Link loss is flagged in loop_normal().
// The status is watched directly: while the saver runs, axis == None and the gate says AxisNone (rule 5)
// before it could ever say Alarm (rule 7).
void watch_attention() {
  static bool last_warn = false;
  static uint32_t last_alarm_ms = 0;
  static bool last_machine_alarm = false;
  const bool warn = battery.warn();
  if (warn && !last_warn) attention = true;
  last_warn = warn;
  if (machine::alarm_line() != nullptr && machine::alarm_ms() != last_alarm_ms) {
    last_alarm_ms = machine::alarm_ms();
    attention = true;
  }
  const bool machine_alarm = machine::have_status() &&
                             (machine::status().state == MachineState::Alarm || machine::status().halted);
  if (machine_alarm && !last_machine_alarm) attention = true;
  last_machine_alarm = machine_alarm;
}

const char* screen_text(ScreenState st) {
  switch (st) {
    case ScreenState::On: return "an";
    case ScreenState::Saver: return "Bildschirmschoner";
    case ScreenState::Off: return "aus";
  }
  return "?";
}

// One tick in the exact order of SPEC 8.7.
void tick_normal(uint32_t now) {
  // Before step 0: screensaver (SPEC 10.4). Read the press edges and the encoder counts first; inputs
  // while asleep, in the waking tick and for WAKE_SWALLOW_MS after it are discarded here, before they
  // reach AxisSelector, accumulator and planner.
  uint8_t edges = inputs::poll(now);
  int32_t counts = encoder::take_counts();
  const bool busy = axis != AxisMode::None || planner.session_active();
  const ScreenTick st = screen.feed(now, edges != 0, counts, busy, attention);
  attention = false;
  if (st.swallow) {
    edges = 0;
    counts = 0;
    accumulator.clear();  // no remainder from before or during the wake may combine with later counts
  }
  if (st.woke) Serial.println("Anzeige: geweckt, Eingabe verworfen");
  if (st.state != screen_state) {
    if (!st.woke) Serial.printf("Anzeige: %s\n", screen_text(st.state));
    screen_state = st.state;
  }
  if (st.move) saver_move = true;

  // 0. Axis selection: press edges in order X, Y, Z, then mode() exactly once.
  if (edges & (1u << inputs::BTN_X)) selector.on_press(Button::X, now);
  if (edges & (1u << inputs::BTN_Y)) selector.on_press(Button::Y, now);
  if (edges & (1u << inputs::BTN_Z)) selector.on_press(Button::Z, now);
  // Axis A is optional: without activation the button has no effect (SPEC 8.4).
  if (cfg.axis_a && (edges & (1u << inputs::BTN_A))) selector.on_press(Button::A, now);
  const AxisMode new_axis = selector.mode(now);
  if (new_axis != axis) {
    // A rotation from before must never arrive on the new axis.
    planner.end_session(now);
    step_policy.reset();
    accumulator.clear();
  }
  axis = new_axis;

  // 1. Encoder (counts were read before step 0).
  if (counts != 0) selector.on_activity(now);

  // 2. Detents.  3. Step size.
  const int32_t detents = accumulator.take(now, counts);
  const int32_t step_um = step_policy.update(now, static_cast<uint32_t>(detents < 0 ? -detents : detents));

  // 4. Gate, every tick with all fields.
  const bool have_status = machine::have_status();
  const MachineStatus* status = have_status ? &machine::status() : nullptr;
  const uint32_t age = machine::status_age_ms(now);
  GateInputs in{};
  in.battery_critical = battery.critical();
  in.wifi_ok = machine::wifi_ok();
  in.tcp_ok = machine::tcp_ok();
  in.have_status = have_status;
  if (have_status) in.status = *status;
  in.status_age_ms = age;
  in.axis = axis;
  in.rates_known = rates_cover(machine::rates(), axis);
  in.session_active = planner.session_active();
  in.session_consistent = have_status ? planner.session_consistent(*status) : false;
  gate = evaluate_gate(in);

  // 5. Planner, with the rates of this connection (F, 8.7 f; ticket #38).
  planner.set_rates(machine::rates());
  const PlannerTickResult r = planner.tick(now, detents, step_um, axis, gate, status, age);
  machine::set_jog_active(planner.session_active());
  if (gate != BlockReason::None || status == nullptr) accumulator.clear();
  if (r.clipped) {
    have_clipped = true;
    last_clipped_ms = now;
  }

  // 6. Send; an incomplete send undoes the target step and drops the link.
  if (r.cmd.valid && !machine::send_line(r.cmd.text)) {
    planner.notify_send_failed();
    machine::drop(now);
  }

  // 7. Unlock (SPEC 8.10): an axis button held UNLOCK_HOLD_MS while the machine reports Alarm sends one $X.
  // Only press edges that passed the screensaver start a hold, and only while the hint is on the display.
  // Axis selection above is unchanged: the press edge selects as always, the hold is in addition.
  {
    const uint8_t kBits[4] = {inputs::BTN_X, inputs::BTN_Y, inputs::BTN_Z, inputs::BTN_A};
    UnlockInputs ui{};
    ui.normal_mode = mode == AppMode::Normal;
    ui.online = machine::tcp_ok();
    ui.have_status = have_status;
    if (have_status) ui.status = *status;
    ui.status_age_ms = age;
    ui.ota_updating = ota::updating();
    ui.hint_visible = st.state == ScreenState::On && !alarm_line_shown(now);
    static uint32_t unlock_alarm_ms = 0;  // alarm_ms() seen by the previous tick: a change is a new line
    ui.new_message = machine::alarm_line() != nullptr && machine::alarm_ms() != unlock_alarm_ms;
    unlock_alarm_ms = machine::alarm_ms();
    for (uint8_t b = 0; b < 4; ++b) {
      if (b == static_cast<uint8_t>(Button::A) && !cfg.axis_a) continue;  // A only when enabled (8.4)
      const uint8_t bit = static_cast<uint8_t>(1u << b);
      if (inputs::stable_pressed(static_cast<inputs::Id>(kBits[b]))) ui.pressed |= bit;
      if (edges & (1u << kBits[b])) ui.edges |= bit;
    }
    unlock_view = unlock.tick(now, ui);
    if (unlock_view.aborted != nullptr) Serial.printf("Entsperren: abgebrochen (%s)\n", unlock_view.aborted);
    if (unlock_view.send) {
      // Nothing may move by itself afterwards: a new session needs an Idle status received START_GAP_MS
      // after the $X (8.7 c.3), and no detent from before survives.
      planner.end_session(now);
      accumulator.clear();
      if (machine::send_line(UNLOCK_TEXT)) {
        Serial.println("Entsperren: $X gesendet");
      } else {
        Serial.println("Entsperren: $X nicht vollstaendig gesendet");
        machine::drop(now, "$X nicht vollstaendig gesendet");
      }
    }
  }
}

// Serial log of state changes in normal mode, for operation without a display (ticket #15).
const char* link_text(machine::LinkState ls) {
  switch (ls) {
    case machine::LinkState::Idle: return "Idle";
    case machine::LinkState::WifiConnecting: return "WLAN verbindet";
    case machine::LinkState::TcpConnecting: return "TCP verbindet";
    case machine::LinkState::Online: return "Online";
    case machine::LinkState::Backoff: return "Backoff";
  }
  return "?";
}

const char* axis_text(AxisMode m) {
  switch (m) {
    case AxisMode::X: return "X";
    case AxisMode::Y: return "Y";
    case AxisMode::XY: return "XY";
    case AxisMode::Z: return "Z";
    case AxisMode::A: return "A";
    default: return "keine";
  }
}

void log_changes(uint32_t now) {
  static machine::LinkState last_link = machine::LinkState::Idle;
  static BlockReason last_gate = BlockReason::None;
  static bool first = true;
  static AxisMode last_axis = AxisMode::None;
  static uint32_t last_alarm_ms = 0;
  static uint32_t last_stats_ms = 0;
  const machine::LinkState ls = machine::state();
  if (ls == machine::LinkState::Online && last_link != machine::LinkState::Online) {
    // New connection: the first statistics window starts now, without leftovers.
    machine::take_stats();
    reset_loop_stats();
    have_last_stats = false;
    last_stats_ms = now;
  }
  if (first || ls != last_link) {
    if (ls == machine::LinkState::TcpConnecting || ls == machine::LinkState::Online) {
      Serial.printf("Link: %s (IP %s, RSSI %d dBm, Maschine %s:%u)\n", link_text(ls),
                    WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()), cfg.host.c_str(),
                    static_cast<unsigned>(cfg.port));
    } else if (ls == machine::LinkState::Backoff) {
      Serial.printf("Link: Backoff (%s)\n", machine::last_error());
    } else {
      Serial.printf("Link: %s\n", link_text(ls));
    }
    last_link = ls;
  }
  if (first || gate != last_gate) {
    Serial.printf("Status: %s\n", block_text(gate, planner.session_active()));
    last_gate = gate;
  }
  if (axis != last_axis) {
    Serial.printf("Achse: %s\n", axis_text(axis));
    last_axis = axis;
  }
  const char* alarm = machine::alarm_line();
  if (alarm != nullptr && machine::alarm_ms() != last_alarm_ms) {
    Serial.printf("Maschine meldet: %s\n", alarm);
    last_alarm_ms = machine::alarm_ms();
  }
  if (ls == machine::LinkState::Online && static_cast<uint32_t>(now - last_stats_ms) >= 5000) {
    last_stats_ms = now;
    const machine::LinkStats st = machine::take_stats();
    const uint32_t mean_us = loop_count > 0 ? static_cast<uint32_t>(loop_sum_us / loop_count) : 0;
    Serial.printf("Statistik 5 s: Status %lu ok / %lu kaputt, Text %lu, andere %lu, max. Luecke %lu ms, "
                  "Luecken >250 ms %lu, Decoder verworfen seit Verbindung %lu, max. Loop %lu ms, "
                  "Loop Mittel %lu us, max. Display %lu ms, max. Web %lu us\n",
                  (unsigned long)st.status_ok, (unsigned long)st.status_bad, (unsigned long)st.text_frames,
                  (unsigned long)st.other_frames, (unsigned long)st.max_gap_ms, (unsigned long)st.gaps_over_limit,
                  (unsigned long)st.dropped, (unsigned long)max_loop_ms, (unsigned long)mean_us,
                  (unsigned long)max_draw_ms, (unsigned long)max_web_us);
    last_stats = StatsView{st.status_ok, st.status_bad, st.text_frames, st.other_frames, st.max_gap_ms,
                           st.gaps_over_limit, st.dropped, max_loop_ms, mean_us, max_web_us};
    have_last_stats = true;
    reset_loop_stats();
  }
  first = false;
}

// Web UI hooks (ticket #17): read-only copies of the state; the web layer never reaches machine_link.
bool web_settings_allowed() { return settings_change_allowed(axis, planner.session_active()); }

void web_snapshot(StatusView& v) {
  static char ip[16];
  const uint32_t now = millis();
  v.fw = FW_VERSION;
  v.uptime_s = now / 1000;
  v.link = link_text(machine::state());
  v.last_error = machine::last_error();
  v.wifi = machine::wifi_ok();
  v.rssi_dbm = v.wifi ? WiFi.RSSI() : 0;
  snprintf(ip, sizeof(ip), "%s", v.wifi ? WiFi.localIP().toString().c_str() : "");
  v.ip = ip;
  v.have_status = machine::have_status();
  if (v.have_status) {
    v.status = machine::status();
    v.status_age_ms = machine::status_age_ms(now);
  }
  v.gate = block_text(gate, planner.session_active());
  // Unlock hint (8.10), read-only: the web UI never sends anything to the machine.
  if (unlock_view.view == UnlockView::Hint) v.gate = UNLOCK_HINT_WEB;
  if (unlock_view.view == UnlockView::Holding) v.gate = UNLOCK_HOLD_WEB;
  if (unlock_view.view == UnlockView::Sent) v.gate = UNLOCK_SENT_TEXT;
  v.axis = axis_text(axis);
  v.axis_a_enabled = cfg.axis_a;
  v.session_active = planner.session_active();
  v.settings_allowed = web_settings_allowed();
  v.bat_mv = battery.last_mv();
  v.bat_percent = battery.percent();
  v.bat_absent = battery.absent();
  v.bat_warn = battery.warn();
  v.alarm = machine::alarm_line();
  v.have_stats = have_last_stats;
  v.stats = last_stats;
}

// Only called by webui after settings_allowed() held in the same loop pass: no axis, no session.
void web_restart() {
  planner.end_session(millis());
  machine::stop();  // orderly close towards the machine (no half-open connection there)
  delay(WEB_RESTART_FLUSH_MS);
  esp_restart();
}

const settings::Settings& web_config() { return cfg; }

const webui::Hooks web_hooks{web_snapshot, web_settings_allowed, web_restart, web_config};

// OTA hooks (SPEC 9.5, ticket #35). Acceptance rule: no axis, no session, battery not low.
bool ota_allowed() { return ota_update_allowed(axis, planner.session_active(), battery.warn()); }

// The update has started: the loop is blocked inside ArduinoOTA.handle() until the restart, so no
// tick runs; machine::stop() additionally makes every send_line() fail. Orderly close towards the
// machine, as before the restart after saving settings.
void ota_stop() {
  planner.end_session(millis());
  machine::stop();
}

const ota::Hooks ota_hooks{ota_allowed, ota_stop};

void loop_normal(uint32_t now) {
  machine::loop(now);
  log_changes(now);
  // Leaving Online deselects the axes (SPEC 8.4).
  const machine::LinkState ls = machine::state();
  if (prev_link == machine::LinkState::Online && ls != machine::LinkState::Online) {
    selector.clear();
    attention = true;  // link lost wakes the display (SPEC 10.4)
  }
  prev_link = ls;
  watch_attention();

  if (start_screen) {
    if (static_cast<uint32_t>(now - start_screen_ms) < UI_START_SHOW_MS) return;
    start_screen = false;
  }
  if (static_cast<uint32_t>(now - last_tick_ms) >= TICK_MS) {
    last_tick_ms = now;
    tick_normal(now);
  }
  if (display::due(now)) {
    const uint32_t t0 = millis();
    switch (screen_state) {
      case ScreenState::On:
        draw_main(now);
        display::power_save(false);  // after drawing: a woken display never shows the old frame
        break;
      case ScreenState::Saver:
        display::power_save(false);
        if (saver_move) display::show_saver();  // redraw only when the picture moves
        saver_move = false;
        break;
      case ScreenState::Off:
        display::power_save(true);
        break;
    }
    const uint32_t dt = millis() - t0;
    if (dt > max_draw_ms) max_draw_ms = dt;
  }

  // Web UI (ticket #17): one bounded, non-blocking step after tick and display, STA only.
  if (!webui::started() && machine::wifi_ok()) {
    webui::begin(web_hooks);
    Serial.printf("Web: http://%s/ bzw. http://%s.local/\n", WiFi.localIP().toString().c_str(), WEB_HOSTNAME);
  }
  if (webui::started()) {
    const uint32_t t0 = micros();
    webui::loop(now);
    const uint32_t dt = micros() - t0;
    if (dt > max_web_us) max_web_us = dt;
  }

  // OTA (ticket #35): only with the setting, after the web UI has started mDNS. Last step of the pass,
  // the acceptance rule is evaluated directly before: no tick can select an axis or start a jog between
  // the check and a transfer that blocks inside ArduinoOTA.handle().
  if (!ota::started() && cfg.ota && webui::started()) ota::begin(settings::ap_pass().c_str(), ota_hooks);
  if (ota::started()) ota::step(ota_allowed());
}

}  // namespace

// Board revision guard (SPEC 9.2, ticket #49), safe mode: this firmware was built for another carrier board.
// No output is configured (LED, wheel and on/off pins stay inputs as after reset; the wheel stays off by its
// gate pull-up), no WiFi, no machine. Only the display (I2C pins are the same on every revision) and the board
// button (GPIO27, same on every revision). Holding the board button BOARD_REV_CONFIRM_MS confirms a deliberate
// board change: the compiled revision is stored and the handwheel restarts. Otherwise flash the right firmware
// over USB. After BOARD_REV_SAFE_SLEEP_MS the chip goes to deep sleep without wake source (battery).
[[noreturn]] void run_wrong_board(uint8_t stored) {
  display::begin();
  display::show_wrong_board(stored, BOARD_REV);
  pinMode(PIN_BOARD_BTN, INPUT_PULLUP);
  BoardRevConfirm confirm(BOARD_REV_CONFIRM_MS);
  const uint32_t t0 = millis();
  uint32_t last_log = 0;
  bool logged = false;
  while (true) {
    const uint32_t now = millis();
    if (!logged || static_cast<uint32_t>(now - last_log) >= 5000) {
      logged = true;
      last_log = now;
      Serial.printf("Falsche Firmware: Platine Rev. %u, Firmware Rev. %d\n", static_cast<unsigned>(stored), BOARD_REV);
      Serial.println("Platine absichtlich gewechselt? Board-Taste 3 s halten. Sonst passende Firmware per USB flashen.");
    }
    if (confirm.update(now, digitalRead(PIN_BOARD_BTN) == LOW)) {
      settings::store_board_rev(static_cast<uint8_t>(BOARD_REV));
      Serial.printf("Platine Rev. %d bestaetigt, Neustart\n", BOARD_REV);
      display::show_wrong_board_done();
      // Restart only after the button is released: held at the next start it would open the portal (9.2 step 7).
      while (digitalRead(PIN_BOARD_BTN) == LOW) delay(10);
      Serial.flush();
      delay(1000);
      esp_restart();
    }
    if (static_cast<uint32_t>(now - t0) >= BOARD_REV_SAFE_SLEEP_MS) {
      Serial.println("Falsche Firmware: Tiefschlaf ohne Wecken (Reset oder USB neu stecken)");
      Serial.flush();
      display::power_save(true);
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
      esp_deep_sleep_start();
    }
    delay(10);
  }
}

void setup() {
  Serial.setTxBufferSize(1024);  // log lines must not block the loop on the UART FIFO
  Serial.begin(115200);

  // 0. Board revision guard (SPEC 9.2), before any output pin is configured.
  {
    uint8_t stored = 0;
    const bool have = settings::load_board_rev(stored);
    switch (board_rev_action(have, stored, static_cast<uint8_t>(BOARD_REV))) {
      case BoardRevAction::Store:
        settings::store_board_rev(static_cast<uint8_t>(BOARD_REV));
        Serial.printf("Platine Rev. %d gespeichert\n", BOARD_REV);
        break;
      case BoardRevAction::Run:
        break;
      case BoardRevAction::WrongBoard:
        run_wrong_board(stored);
    }
  }

  // 1. Outputs safe, display, read and clear the diag request.
  leds::begin();
  encoder::power_off();
  display::begin();
  const bool want_diag = rtc_next_mode_magic == NEXT_MODE_MAGIC && rtc_next_mode == NEXT_MODE_DIAG;
  rtc_next_mode_magic = 0;

  // 2. Buttons, debounced at boot; the result initializes the debouncers.
  inputs::begin();

  // 2a. On/off button (soft power): off at boot (USB plugged in, after flashing, watchdog or brownout reset,
  // woken by switching off after a battery shutdown) -> deep sleep at once, no WiFi, display only switched off.
  const PowerLevel boot_level = boot_power_level();
  if (boot_level == PowerLevel::Off) {
    Serial.println("Ein/Aus: aus beim Start, Tiefschlaf");
    mode = AppMode::Shutdown;
    power::shutdown(SleepReason::SwitchOff);
  }
  if (boot_level == PowerLevel::Unknown) Serial.println("Ein/Aus: kein stabiler Pegel, normaler Start");

  // 3. Brownout counter.
  const bool brownout = esp_reset_reason() == ESP_RST_BROWNOUT;
  if (brownout) {
    if (rtc_brownout_magic != NEXT_MODE_MAGIC) {
      rtc_brownout_magic = NEXT_MODE_MAGIC;
      rtc_brownout_count = 0;
    }
    if (rtc_brownout_count < 255) ++rtc_brownout_count;
  } else {
    rtc_brownout_magic = NEXT_MODE_MAGIC;
    rtc_brownout_count = 0;
  }

  // 4. Startup battery measurement.
  battery.add_sample(power::measure_battery_mv(), true);
  last_bat_ms = millis();
  // Shutdown only after two brownouts in a row, or a brownout with a low start reading:
  // a loose contact or a bouncing power switch also causes brownouts with a full battery.
  const bool low_start = !battery.absent() && battery.last_mv() < BAT_WARN_MV;
  if ((brownout && (rtc_brownout_count >= 2 || low_start)) || battery.critical()) enter_shutdown();

  // 5. Wheel supply on, then PCNT.
  if (!encoder::begin()) Serial.println("PCNT init failed");

  Serial.printf("makera-handrad %s, Platine Rev. %d, reset %d, bat %u mV\n", FW_VERSION, BOARD_REV,
                static_cast<int>(esp_reset_reason()), static_cast<unsigned>(battery.last_mv()));

  // 6.-8. Mode selection.
  const bool have_credentials = settings::load(cfg);
  if (want_diag) {
    mode = AppMode::Diagnose;
  } else if (inputs::held_at_boot(inputs::BTN_X) || inputs::held_at_boot(inputs::BTN_BOARD) || !have_credentials) {
    mode = AppMode::Portal;
    portal::begin(restart_into_diag);
    display::show_portal(portal::ap_ssid(), portal::ap_password(), "192.168.4.1");
    // Also on the serial console, so the portal is reachable without a display (ticket #15).
    Serial.printf("Portal (%s%s%s): SSID %s, Passwort %s, http://192.168.4.1\n",
                  inputs::held_at_boot(inputs::BTN_X) ? "X gehalten " : "",
                  inputs::held_at_boot(inputs::BTN_BOARD) ? "Board-Taste gehalten " : "",
                  have_credentials ? "" : "keine Zugangsdaten", portal::ap_ssid(), portal::ap_password());
  } else {
    mode = AppMode::Normal;
    display::show_start(FW_VERSION);
    start_screen = true;
    start_screen_ms = millis();
    const machine::Config mc{cfg.ssid.c_str(), cfg.pass.c_str(), cfg.host.c_str(), cfg.port};
    machine::begin(mc, on_session_end);
  }
}

void loop() {
  const uint32_t now = millis();
  static uint32_t prev_loop_start = 0;
  const uint32_t pass = now - prev_loop_start;
  if (prev_loop_start != 0 && pass > max_loop_ms) max_loop_ms = pass;
  prev_loop_start = now;
  // Mean pass duration in microseconds (ticket #17), from loop start to loop start.
  static uint32_t prev_loop_us = 0;
  const uint32_t now_us = micros();
  if (prev_loop_us != 0) {
    loop_sum_us += static_cast<uint32_t>(now_us - prev_loop_us);
    ++loop_count;
  }
  prev_loop_us = now_us;
  // Soft power (SPEC 9.3): switched off -> end the session, deep sleep. In every AppMode, before anything else.
  if (power_switch.update(now, inputs::power_raw_on()).turned_off) enter_switch_off();
  sample_battery(now);
  if (battery.critical()) enter_shutdown();
  switch (mode) {
    case AppMode::Diagnose: loop_diag(now); break;
    case AppMode::Portal: portal::loop(now); break;
    case AppMode::Normal: loop_normal(now); break;
    case AppMode::Shutdown: break;
  }
  update_leds(now);
}
