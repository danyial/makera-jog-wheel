#pragma once
// Pure display/driver details used only by src/ (SPEC 8.9). All logic values live in lib/core/core_config.h.
#include <cstdint>

constexpr uint32_t UI_BAT_EMPTY_SHOW_MS   = 5000;    // "Akku leer" screen before display power save (9.3)
constexpr uint32_t UI_START_SHOW_MS       = 1000;    // start screen (10.2)
constexpr uint32_t UI_OTA_RESULT_SHOW_MS  = 1000;    // OTA result before the restart (9.5)
constexpr uint32_t UI_LED_BLINK_NORMAL_HZ = 2;       // axis selected, gate blocked (9.4)
constexpr uint32_t UI_LED_BLINK_PORTAL_HZ = 1;       // portal (9.4)
constexpr int      UI_LINE_MAX_CHARS      = 21;      // alarm/error line shortened to this (8.8)
constexpr uint16_t UI_DEFAULT_PORT        = 2222;    // machine TCP port default (9.1)
constexpr int      AP_PASS_DIGITS         = 8;       // portal WPA2 password length (9.1)
constexpr int      PCNT_LIMIT             = 30000;   // PCNT high/low limit (8.7)
constexpr int      PCNT_GLITCH_NS         = 1000;    // PCNT glitch filter (8.7)

// Position map of the web status page (ticket #36, SPEC 9.1, DECISIONS.md).
// Travel: read from the machine (config-get sd, 2026-11-11): soft_endstop.{x,y,z}_min = -207/-206/-102 mm,
// max = -1 mm (hard-coded in the Makera firmware).
constexpr int32_t UI_TRAVEL_X_MIN_UM         = -207000;
constexpr int32_t UI_TRAVEL_X_MAX_UM         = -1000;
constexpr int32_t UI_TRAVEL_Y_MIN_UM         = -206000;
constexpr int32_t UI_TRAVEL_Y_MAX_UM         = -1000;
constexpr int32_t UI_TRAVEL_Z_MIN_UM         = -102000;
constexpr int32_t UI_TRAVEL_Z_MAX_UM         = -1000;
// Board placement: MEASURED by the user (ticket #40, 2026-11-11). Makera Studio "QuickMove -> Anchor1" gives
// MPos X -191.580 / Y -194.140 (= coordinate.anchor1); a square laid against the front left board corner
// with both arms 15 mm wide shows anchor 1 sits 15.0/15.0 mm inside the corner. Corner = anchor 1 - (15, 15).
// The board reaches 3.14 mm in front of the Y travel minimum; the 2D map and the 3D view draw both.
// Previous estimate -207000/-206000 (DECISIONS.md). UI_POS_CALIBRATED covers X/Y only, not the height.
constexpr bool    UI_POS_CALIBRATED          = true;
constexpr int32_t UI_BOARD_ORIGIN_MPOS_X_UM  = -206580;  // MPos X of the front left board corner (measured)
constexpr int32_t UI_BOARD_ORIGIN_MPOS_Y_UM  = -209140;  // MPos Y of the front left board corner (measured)
// Board top in MPos Z (3D view, ticket #37): ESTIMATED, NOT MEASURED. Put at the bottom of the Z travel,
// so the travel box stands on the board. Not covered by UI_POS_CALIBRATED and without a page hint (ticket
// #40, DECISIONS.md): MPos Z depends on the tool length, so it stays an approximation anyway.
constexpr int32_t UI_BOARD_TOP_MPOS_Z_UM     = -102000;
// Rotary module (A axis, 3D view): shape and position FROM CAD (ticket #48, include/a_axis_geometry.h,
// generated from Makera-Z1_Simplified_4Axis.step), relative to the measured board corner and the board
// top above, so no separate MPos values here. NOT verified on the machine: while UI_A_POS_CALIBRATED is
// false the 3D view says "Lage der Drehachse aus CAD" next to the A angle. Check: H-22, DECISIONS.md.
// (The axis Y from CAD is 9.26 mm behind anchor 1 + rotation_offset_y of the machine config.)
constexpr bool    UI_A_POS_CALIBRATED        = false;
