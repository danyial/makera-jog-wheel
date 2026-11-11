#pragma once
// Single source of all logic constants (SPEC 8.9). Only <cstdint> and constexpr definitions.
#include <cstdint>

constexpr const char* FW_VERSION = "0.1.0";                 // 10.2

constexpr bool BUTTON_ACTIVE_LOW = true;                    // 7.3
constexpr bool LED_ACTIVE_HIGH   = true;                    // 7.4

constexpr uint32_t AXIS_IDLE_CLEAR_MS  = 60000;             // 8.4
constexpr uint32_t START_GAP_MS        = 200;               // 8.7
constexpr uint32_t ENC_POWER_SETTLE_MS = 10;                // 7.2
constexpr bool     ENC_INVERT          = false;             // 8.7
constexpr int32_t  COUNTS_PER_DETENT   = 4;                 // 8.7
constexpr uint32_t TICK_MS             = 20;                // 8.7
constexpr uint32_t DEBOUNCE_MS         = 30;                // 8.4
constexpr uint32_t STATUS_INTERVAL_MS  = 100;               // 8.3

constexpr uint32_t SEND_MAX_AGE_MS  = 250;                  // 8.5
constexpr uint32_t START_MAX_AGE_MS = 150;                  // 8.5

constexpr uint32_t SPEED_WINDOW_MS = 500;                   // 8.6
constexpr int      SPEED_RING      = 32;                    // 8.6
constexpr int32_t  STEP_UM[3]      = {10, 50, 100};         // 8.6
constexpr uint32_t LEVEL_UP_V[3]   = {0, 8, 20};            // 8.6, index 0 never read
constexpr uint32_t LEVEL_DOWN_V[3] = {0, 4, 12};            // 8.6, index 0 never read
constexpr uint32_t STEP_RESET_MS   = 300;                   // 8.6, 8.7

constexpr int32_t WINDOW_UM[4]    = {5000, 5000, 1000, 2000};  // 8.7 (X, Y, Z in um; A in millidegrees; #32)
constexpr int32_t REVERSAL_EPS_UM = 10;                     // 8.7
constexpr int32_t ARRIVE_UM       = 10;                     // 8.7
constexpr int32_t FOREIGN_TOL_UM  = 100;                    // 8.7

constexpr uint32_t SESSION_IDLE_MS     = 1000;              // 8.7
constexpr uint32_t SESSION_MAX_IDLE_MS = 3000;              // 8.7
constexpr uint32_t NODATA_HOLD_MS      = 1000;              // 8.7 a0 (ticket #19)

// Feed of $J from the wheel tempo (8.7 f, tickets #32, #38). F is a factor on the max rate of the slowest
// moved axis as the machine reports it (M203.1, 8.8); all F values here are in thousandths (1000 = F1.000).
// The jog speed is bounded by the absolute limits below, independent of the machine's rates: the cap of F is
// floor(JOG_MAX_SPEED x 1000 / rate), at most F_MAX_MILLI; its floor ceil(JOG_MIN_SPEED x 1000 / rate), at
// least 1 and never above the cap. A higher machine rate never makes the handwheel faster.
constexpr uint32_t JOG_MAX_SPEED_UM_S[4] = {20000, 20000, 5000, 30000};  // 8.7, X, Y (XY: path), Z um/s; A mdeg/s
constexpr uint32_t JOG_MIN_SPEED_UM_S[4] = {400, 400, 200, 1200};        // 8.7, lower limit, never F <= 0
constexpr int32_t  F_MAX_MILLI      = 1000;                 // 8.7, never above the machine's max rate
constexpr uint32_t FEED_WINDOW_MS   = 200;                  // 8.7, tempo = want in this window
constexpr int      FEED_RING        = 16;                   // 8.7, entries with want > 0
constexpr uint32_t FEED_LEAD_PCT    = 92;                   // 8.7, machine runs 0.92 x the wheel tempo (queue fills)
// Bundling (8.7 e1, ticket #32): after the first command of a session, detents are collected and sent as one
// command in the tick BUNDLE_MS after the first collected detent (3 ticks = 60 ms of turning per command).
constexpr uint32_t BUNDLE_MS        = 40;                   // 8.7 e1
// Upper bound of handwheel blocks possibly still in the machine's planner queue (firmware: 32 blocks,
// a full queue re-enters the console handler). 2 x 12 = 24 < 32 also covers a session that starts on a
// stale Idle while the previous session's blocks are still queued (8.7 c restrisk).
constexpr int      JOG_MAX_QUEUED   = 12;                   // 8.7 e2
constexpr uint32_t FEED_XY_PATH_PERMILLE = 1414;            // 8.7, XY path speed = sqrt(2) x axis speed

// Max rates read from the machine after every connect (8.8, ticket #38). Until they are known for the selected
// axis the gate stays closed (8.5 rule 7a). Plausible range per axis, X, Y, Z in um/s, A in millidegrees/s:
// 60 ... 10000 mm/min and 60 ... 36000 deg/min (Z1 Pro 2026-11-11: 3000/3000/1000 mm/min, 3600 deg/min).
constexpr const char* RATE_QUERY_TEXT     = "M203.1";      // 8.8, reports the live actuator max rates in mm/s
constexpr uint32_t RATE_MIN_UM_S[4]       = {1000, 1000, 1000, 1000};          // 8.8
constexpr uint32_t RATE_MAX_UM_S[4]       = {166667, 166667, 166667, 600000};  // 8.8
constexpr uint32_t RATE_QUERY_TIMEOUT_MS  = 1000;           // 8.8, answer expected within
constexpr uint32_t RATE_RETRY_MS[3]       = {1000, 2000, 5000};  // 8.8, then 5000 forever
constexpr uint32_t RATE_REFRESH_MS        = 30000;          // 8.8, re-read while Idle without session; failure keeps rates

// Unlock after Alarm/HALT (8.10, ticket #41): an axis button held UNLOCK_HOLD_MS while the machine reports Alarm
// sends UNLOCK_TEXT once per hold, at most one per UNLOCK_MIN_INTERVAL_MS. Never automatic, never from the web UI.
constexpr const char* UNLOCK_TEXT          = "$X";         // 8.10, console line (0xA2) like M203.1
constexpr uint32_t UNLOCK_HOLD_MS          = 5000;         // 8.10, user decision (ticket #41)
constexpr uint32_t UNLOCK_MIN_INTERVAL_MS  = 2000;         // 8.10, second safeguard
constexpr uint32_t UNLOCK_SHOW_MS          = 2000;         // 8.10, "Entsperren..." until no Alarm, at most this long

constexpr uint16_t TX_MAX_PAYLOAD = 513;                    // 8.1
constexpr uint16_t MAX_RX_PAYLOAD = 600;                    // 8.2
constexpr uint16_t TEXT_LINE_MAX  = 256;                    // 8.2

constexpr uint32_t WIFI_TIMEOUT_MS        = 15000;          // 8.8
constexpr uint32_t TCP_TIMEOUT_MS         = 3000;           // 8.8
constexpr uint32_t LINK_STATUS_TIMEOUT_MS = 3000;           // 8.8
constexpr uint32_t BACKOFF_MS[3]          = {1000, 2000, 5000};  // 8.8, then 5000 forever

constexpr uint32_t ALARM_SHOW_MS   = 2000;                  // 8.8
constexpr uint32_t CLIPPED_SHOW_MS = 500;                   // 10.1

constexpr uint16_t BAT_EMPTY_MV = 3300;                     // 9.3
constexpr uint16_t BAT_FULL_MV  = 4150;                     // 9.3
constexpr uint16_t BAT_WARN_MV  = 3400;                     // 9.3
constexpr uint16_t BAT_CRIT_MV  = 3300;                     // 9.3
constexpr int      BAT_SAMPLES     = 16;                    // 9.3
constexpr uint32_t BAT_INTERVAL_MS = 5000;                  // 9.3
constexpr int      BAT_CRIT_COUNT  = 3;                     // 9.3
constexpr uint16_t BAT_ABSENT_LOW_MV  = 2800;               // 9.3
constexpr uint16_t BAT_ABSENT_HIGH_MV = 4400;               // 9.3

constexpr uint32_t PORTAL_TIMEOUT_MS = 600000;              // 9.1

constexpr uint32_t NEXT_MODE_MAGIC = 0x4D4B5231;            // 9.2
constexpr uint8_t  NEXT_MODE_DIAG  = 0xD1;                  // 9.2
constexpr uint32_t BOOT_DEBOUNCE_MS = 50;                   // 9.2

// Soft power (9.3, ticket #49): the latching on/off button is an input on PIN_PWR_SW, contact to GND,
// internal pull-up. Closed = on. Debounced longer than the axis buttons: off puts the chip into deep sleep.
constexpr bool     POWER_SW_ACTIVE_LOW  = true;             // 9.3, contact closed = LOW = on
constexpr uint32_t POWER_SW_DEBOUNCE_MS = 100;              // 9.3, level must hold this long
constexpr uint32_t POWER_SW_BOOT_MAX_MS = 1000;             // 9.2, boot waits at most this long for a stable level
constexpr uint32_t POWER_OFF_FLUSH_MS   = 150;              // 9.3, FIN to the machine goes out before WiFi stops

// Board revision guard (9.2, ticket #49): wrong firmware for the board waits in safe mode until the board button
// is held this long, which confirms a deliberate board change.
constexpr uint32_t BOARD_REV_CONFIRM_MS = 3000;             // 9.2
constexpr uint32_t BOARD_REV_SAFE_SLEEP_MS = 600000;        // 9.2, safe mode ends in deep sleep (battery)

constexpr uint32_t LED_PWM_HZ       = 1000;                 // 7.4
constexpr uint32_t LED_DUTY_PERCENT = 60;                   // 7.4

constexpr uint32_t DISPLAY_MAX_HZ = 5;                      // 10

// Screensaver against OLED burn-in (10.4, ticket #28), times since the last input or attention event.
constexpr uint32_t SCREENSAVER_MS = 60000;                  // 10.4, logo instead of the main screen
constexpr uint32_t DISPLAY_OFF_MS = 600000;                 // 10.4, display fully off (power save)
constexpr uint32_t SAVER_MOVE_MS  = 5000;                   // 10.4, logo jumps to a new position
constexpr uint32_t WAKE_SWALLOW_MS = 150;                   // 10.4, inputs discarded after a wake
constexpr int32_t  WAKE_MIN_COUNTS = COUNTS_PER_DETENT / 2; // 10.4, counts needed to wake (not one noise count)

// Web UI in normal mode (9.1, ticket #17).
constexpr uint16_t    WEB_PORT             = 80;            // 9.1
constexpr const char* WEB_HOSTNAME         = "handrad";     // 9.1, mDNS name handrad.local
constexpr uint32_t    WEB_REQ_MAX          = 2048;          // 9.1, request head + body
constexpr uint32_t    WEB_REQ_TIMEOUT_MS   = 2000;          // 9.1, per connection
constexpr uint32_t    WEB_AUTH_LOCK_MS     = 2000;          // 9.1, after a wrong password
constexpr uint32_t    WEB_RESTART_FLUSH_MS = 300;           // 9.1, response and FIN out before esp_restart()
constexpr int         WEB_SLOTS            = 2;             // 9.1, simultaneous connections
constexpr int         WEB_LISTEN_BACKLOG   = 4;             // 9.1
constexpr uint32_t    WEB_LISTEN_RETRY_MS  = 1000;          // 9.1, reopen a broken listening socket
constexpr uint32_t    WEB_JSON_MAX         = 1536;          // 9.1, /status.json buffer (worst case: test 67)
constexpr uint32_t    WEB_FIELD_MAX        = 80;            // 9.1, decoded form field incl. NUL
constexpr uint32_t    WEB_LOG_MIN_MS       = 5000;          // 9.1, at most one "Web: Anfrage verworfen" line per 5 s (#29)
constexpr uint32_t    WEB_ASSET_SEND_MAX_MS = 30000;        // 9.1, absolute bound for sending a static asset (#37)

// OTA update over WiFi (9.5, ticket #35).
constexpr uint16_t    OTA_PORT             = 3232;          // 9.5, ArduinoOTA/espota UDP invitation port
constexpr uint32_t    OTA_SLOW_HANDLE_MS   = 100;           // 9.5, log an ArduinoOTA.handle() that took longer
constexpr uint32_t    OTA_AUTH_LOCK_MS     = 5000;          // 9.5, listener closed after a wrong OTA password
constexpr uint32_t    OTA_LISTEN_RETRY_MS  = 5000;          // 9.5, retry opening UDP port OTA_PORT
