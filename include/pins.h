#pragma once
// GPIO assignment (SPEC 7.3). The ONLY place where GPIO numbers live.
// Mapping "silk label -> GPIO" must be confirmed on the device (HARDWARE_CHECKLIST H-1).
// Chosen so that the single-sided carrier PCB (hardware/) routes without crossings (tickets #21, #25, #49).
//
// Board revision (ticket #49): HANDRAD_BOARD_REV, evaluated only here and in src/ (lib/core is revision-free).
// 2 = soft power board: battery always connected, on/off button on GPIO4, own pin map (default without the flag).
#ifndef HANDRAD_BOARD_REV
#define HANDRAD_BOARD_REV 2
#endif
#if HANDRAD_BOARD_REV == 2
#define HANDRAD_BOARD_REV_VALID
#endif
#ifndef HANDRAD_BOARD_REV_VALID
#error "HANDRAD_BOARD_REV: unknown board revision"
#endif
constexpr int BOARD_REV = HANDRAD_BOARD_REV;

// Same on every board revision.
constexpr int PIN_ENC_A     = 23;  // MOSI handwheel A, 10k pull-up to switched wheel VCC
constexpr int PIN_ENC_B     = 19;  // MISO handwheel B, 10k pull-up to switched wheel VCC
constexpr int PIN_BAT_ADC   = 34;  // A2   on-board battery divider 1M/1M
constexpr int PIN_ENC_PWR   = 18;  // SCK  gate AO3401, 100k pull-up to 3V3; LOW = wheel on
constexpr int PIN_SDA       = 21;
constexpr int PIN_SCL       = 22;
constexpr int PIN_BOARD_BTN = 27;  // D4   on-board button, pressed = LOW

// Soft power board (ticket #49): buttons X, Y, Z, A in one connector row, display/wheel/on-off in the other.
constexpr int PIN_BTN_X     = 13;  // D7   internal pull-up, contact to GND
constexpr int PIN_BTN_Y     = 26;  // D3   internal pull-up, contact to GND
constexpr int PIN_BTN_Z     = 35;  // A3   10k pull-up to 3V3, contact to GND (input only)
constexpr int PIN_BTN_A     = 36;  // A0   rotary axis A button: 10k pull-up to 3V3, contact to GND (input only)
constexpr int PIN_LED_X     = 14;  // D6   LED+ of button X (GPIO14 outputs PWM briefly at boot: LED flickers)
constexpr int PIN_LED_Y     = 25;  // D2   LED+ of button Y
constexpr int PIN_LED_Z     = 15;  // A4   LED+ of button Z. Strapping MTDO: internal pull-up at reset against the
                                   //      LED to GND; either level is harmless (LOW only silences the ROM boot log)
constexpr int PIN_LED_A     = 12;  // D13  LED+ of button A. Strapping MTDI: must be LOW at reset (3.3 V flash); the
                                   //      LED to GND only pulls it down, never up
constexpr int PIN_PWR_SW    = 4;   // D12  on/off button (soft power): internal pull-up, contact to GND, closed = LOW
                                   //      = on; RTC GPIO, ext0 wake from deep sleep; no strapping pin
// GPIO39 (A1) stays unused on this board.

// Input-only GPIOs 34-39 have no internal pull-ups; their buttons have 10k pull-ups on the board.
constexpr bool pin_has_internal_pullup(int gpio) { return gpio < 34; }
