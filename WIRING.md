# Wiring

Board: DFRobot FireBeetle 2 ESP32-UE (DFR1140), module ESP32-WROOM-32UE-N16R2. In the firmware, GPIO numbers appear only in `include/pins.h`. The mapping "silkscreen label → GPIO" must be confirmed with the diagnostics screen before soldering (H-1).


## Bill of materials

| Qty | Part | Data |
|---|---|---|
| 1 | Handwheel ZSS600-001-100B-5L | 60 mm, 100 pulses/rev, line driver A, −A, B, −B; operated at 3.3 V (SPEC 7.2) |
| 1 | Display SH1106 | 128×64, I2C, address 0x3C |
| 3 (+1) | Push button 12 mm, momentary, green LED ring | labelled X, Y, Z (+ A only for the optional rotary axis); red = LED+, black = LED−, both green = contact |
| 1 | On/off button, latching | input on GPIO4 (D12) to GND, internal pull-up (soft power, SPEC 9.3, ticket #49); not in the battery lead |
| 1 | LiPo 1S 3.7 V | PH2.0 connector |
| 1 | P-channel MOSFET AO3401 (SOT-23, marking A19T) | pin 1 gate, 2 source, 3 drain; SOT-23 adapter board recommended |
| 3 (+1) | Resistor 10 kΩ | 2 × pull-up encoder A/B, 1 × pull-up button Z (+1 × pull-up button A) |
| 1 | Resistor 100 kΩ | gate pull-up AO3401 |

Only if H-3 yields more than 12 mA per LED: additionally 3 × AO3401 and 3 × 100 kΩ (4 each with button A) (LED high-side, "LOW = on").
Only if H-8 shows resets: additionally 10 kΩ in series with the gate and 100 nF between gate and source (soft start).

## Handwheel terminal assignment (ZSS600)

| Row | Terminal | Connection |
|---|---|---|
| top | A | GPIO23 (MOSI) and 10 kΩ to wheel VCC (drain AO3401) |
| top | B | GPIO19 (MISO) and 10 kΩ to wheel VCC (drain AO3401) |
| top | 0V | GND |
| top | VCC | drain AO3401 (wheel VCC, switched) |
| bottom | /A | unused |
| bottom | /B | unused |

## Netlist

| Net | Connections |
|---|---|
| BAT+ | battery plus (J1 pin 1) → pogo P2 → "Li-ion +" pad of the board, always connected (ticket #49) |
| BAT− | battery minus (J1 pin 2) → pogo P1 → "Li-ion −" pad of the board (= GND) |
| PWR_SW | GPIO4 (D12), internal pull-up → on/off button (J2 pin 1), other contact to GND (J2 pin 2). Closed = LOW = on |
| GND | board GND, handwheel 0V, display GND, all button contacts (one green wire), all LED− (black) |
| 3V3 | board 3V3 (top row pin 2): 10 kΩ pull-ups buttons X/Y/A, display VCC |
| 3V3_ENC | board 3V3 (bottom row pin 2, on the board the same net as 3V3): source AO3401 (pin 2), 100 kΩ gate pull-up |
| ENC_VCC | drain AO3401 (pin 3), handwheel VCC, 10 kΩ pull-ups A and B |
| ENC_PWR | GPIO18 (SCK) → gate AO3401 (pin 1), 100 kΩ to 3V3. LOW = wheel on |
| ENC_A | handwheel A → GPIO23 (MOSI) |
| ENC_B | handwheel B → GPIO19 (MISO) |
| BTN_X | GPIO13 (D7), internal pull-up, button X (other green wire) |
| BTN_Y | GPIO26 (D3), internal pull-up, button Y |
| BTN_Z | GPIO35 (A3), 10 kΩ to 3V3, button Z |
| LED_X | GPIO14 (D6) → LED+ (red) button X |
| LED_Y | GPIO25 (D2) → LED+ (red) button Y |
| LED_Z | GPIO15 (A4) → LED+ (red) button Z |
| SDA / SCL | GPIO21 / GPIO22 → display |
| BAT_ADC | GPIO34 (A2), divider 1 MΩ/1 MΩ on the board (H-6) |
| BOARD_BTN | GPIO27 (D4), button on the board |
| BTN_A (optional) | GPIO36 (A0), 10 kΩ to 3V3, button A (rotary axis) |
| LED_A (optional) | GPIO12 (D13) → LED+ (red) button A, LED− to GND |
| GND_ENC | board GND (bottom row pin 4) → handwheel 0V (J6 pin 1); on the board separate from the GND bus, joined on the FireBeetle |

Notes:
- The pull-ups of A and B hang on the **switched** wheel VCC, not on 3V3. Otherwise, with the wheel switched off, current would flow through the protection diodes into the wheel.
- Off limits: GPIO0, 2, 5 (strapping), 16 and 17 (PSRAM), 1 and 3 (USB serial), 6–11 (flash). The strapping pins GPIO12 and GPIO15 only drive button LEDs, which can only pull them LOW at reset (SPEC 7.3). GPIO39 is unused.
- macOS: The USB-serial converter is a WCH CH340 (PID 0x7522) and needs the WCH driver. Port: `/dev/cu.wchusbserial*`.


## Measured values (to be entered by the human)

| Item | Value |
|---|---|
| H-6 battery divider | 2026-11-09: 4.05 V at the battery, 1.933 V at A2 against GND (loaded by the multimeter, consistent with 1M/1M). Confirmation in the diagnostics screen open. |
| H-8 handwheel current at 3.3 V | open |
| H-9 / H-23 (a) sleep current with the button off | open |
| H-9 / H-23 (b) charging with the button off | open |
| Acceptance A-8 half-open connections | open |


## Safety

The handwheel is **not a safety component**. The machine's emergency stop remains authoritative.

- **Jog gate:** Jogging happens only with an axis selected, fresh status (at most 250 ms old, at session start at most 150 ms) and a ready machine. If a job is running (`P:` in the status), the machine is in alarm or otherwise busy (e.g. spindle running), no `$J` goes out.
- **Overrun:** After letting go, the machine travels at most `WINDOW_UM + 10 µm` further: 5.01 mm in X/Y, 1.01 mm in Z, 2.01° in A (since ticket #32, previously 2 mm and 0.5 mm respectively). Surplus detents are discarded. X, Y and Z move about 0.92 times as fast as the wheel is turned (the same formula applies to A, but the firmware accelerates A up to about 10-fold, so A usually moves faster); the backlog grows by about 8 % of the turned distance and is worked off after letting go, typically in about `0.09 × turning duration + 0.2 s`. The distance always remains limited by the window. Detents are bundled for up to 60 ms; collected detents never go out after a lock, an axis change or a session end. At most 12 handwheel blocks sit in the machine's queue (even with a new session on stale Idle, at most 28 of 32 slots in total) (the firmware mishandles a full queue, see `DECISIONS.md`).
- **Residual risk job start (SPEC 8.5):** If Makera Studio starts a job in the roughly 100–250 ms between two status reports, one more `$J` can go out before the job becomes visible in the status. The machine does not acknowledge `$J`. At most one window is affected (5 mm XY, 1 mm Z, 2° A).
- **Residual risk stale Idle status (SPEC 8.7):** With Wi-Fi dropouts over 200 ms, a new session can start on a stale Idle. The overrun is then in the worst case about twice the window (about 10 mm XY, 2 mm Z); at most 28 handwheel blocks then sit in the machine's queue.
- **Residual risk foreign commands (SPEC 8.7):** If another client sends a small move during a handwheel session (deviation under 100 µm), further handwheel steps queue up behind it, at most one window in total. If another client pauses the machine (hold) and resumes it, handwheel steps already queued continue for at most one window.
- **Short data loss (SPEC 8.7 a0/a1):** If status reports are missing for up to 1 s, a running session is only paused. Jogging resumes only on a status received at least 200 ms after the gap began, and only if it shows no job, alarm or foreign motion. The residual risk "job start between two reports" remains (at most one window).
- **Buffered commands after connection teardown (SPEC 8.8):** The connection is closed in an orderly fashion. Already buffered `$J` frames (at most one window) can still reach the machine after the teardown. If a new session starts immediately after reconnecting, the overrun is in the worst case about twice the window (see `DECISIONS.md`).
- **Machine rates (SPEC 8.8, ticket #38):** After every connect, the handwheel queries the maximum rates with `M203.1` (without arguments, read-only). Until a plausible answer arrives, the jog gate stays closed (`Raten unbekannt` (rates unknown)). The top speed (X/Y 20 mm/s, Z 5 mm/s, A 30 °/s) is fixed and does not depend on the rates read. When idle (Idle, no session), it re-reads them every 30 s; a failed re-read keeps the old rates, and within a session they never change. Residual risk: if the rates are changed while the handwheel is connected, only the speed is off until the next re-read (about 30 s) or until the end of the session; the overrun remains limited by the window.
- **Soft limits:** In the machine's default configuration they are off (`soft_endstop.enable false`). The handwheel does not check travel limits and can drive into the hardware limit switches (alarm). **Recommendation:** enable soft limits in the machine. **State on the user's machine** (read out 2026-11-11 with `config-get sd`, ticket #36): `soft_endstop.enable = true`, `x_min/y_min/z_min = -207/-206/-102` mm, maximum fixed at −1 mm (Makera firmware). The recommendation is met there.
- **OTA update (SPEC 9.5, ticket #35):** Off by default. When enabled, the handwheel accepts an update only if no axis is selected, no session is running and the battery is not low; it then cannot jog either. When the update starts, the connection to the machine is closed in an orderly fashion; no command goes out until the restart. An aborted update leaves the old firmware active. The password is the handwheel password; anyone who knows it and is on the same network can flash arbitrary firmware.
- **Unlock after alarm (SPEC 8.10, ticket #41):** An axis button held for 5 s while the machine reports alarm sends exactly one `$X` (like the unlock dialog in Studio), regardless of the alarm cause, even after a limit switch or without homing; the machine then takes the position from the motor positions. Never automatically, never via the wheel or the web UI. Afterwards nothing moves by itself: detents from the alarm expire, jogging happens only after a fresh Idle status and new turning. The press edge of the held button acts like any press on the axis selection (select, deselect, form XY, with Z/A deselect everything else); after the unlock, the state is selected that this press produces.
- **Switching off while moving (soft power, ticket #49):** The on/off button is an input. Off (debounced 100 ms) first ends the jog session, then closes the connection to the machine in an orderly way and puts the handwheel into deep sleep; the motion ends within the window, afterwards the machine receives no more commands (acceptance A-7). Switching on is a normal start (no axis selected). A missing button cable reads as "off". During an OTA transfer the transfer ends first (the connection to the machine is already closed), then the handwheel restarts and sleeps.
- **Residual risk (soft power):** "Off" is not a disconnect. The battery stays connected, the sleeping board draws about 0.5 mA (a 3000 mAh battery is empty after months; its protection board cuts off at 3.0 V). For storage unplug J1.
