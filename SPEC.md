# SPEC — Wi-Fi handwheel for the Makera Z1 Pro

As of: 2026-11-09 · Status: review rounds 1–5 incorporated · Addendum ticket #14: optional rotary axis A · Addendum ticket #17: web UI in normal operation

---

## 1. Role and assignment

You are an embedded developer building the firmware for a wireless handwheel (ESP32) that jogs a CNC mill of type Makera Z1 Pro over Wi-Fi.

Work autonomously according to this specification. If something is unclear or not covered, do not ask; decide with a rationale and record the decision in `DECISIONS.md` (Date, Question, Decision, Rationale). The guiding principle is the core promises in chapter 3. When in doubt, **safety before convenience**.

The hardware is not available to you. Everything that can only be checked on real hardware is marked **[MANUAL]**. List these items in `HARDWARE_CHECKLIST.md` as open tasks for the human and never check them off yourself.

## 2. Product in one sentence

A battery-powered handwheel with display that connects directly to the Makera Z1 Pro over Wi-Fi and jogs the axes X and Y (individually or together) as well as Z with detent precision, optionally also the rotary axis A, without a PC and in parallel with Makera Studio.

## 3. Core promises

1. **The machine never moves unexpectedly.**
   - Motion happens only when an axis is selected, a fresh status is available and the machine is ready (8.5).
   - When the wheel stops, the machine comes to a halt after at most `WINDOW_UM` (+10 µm) remaining travel (8.7).
   - Excess detents are **discarded instead of queued up**.
2. **Fine and fast without a selector switch.** The step size follows the turning speed: slow 0.01 mm per detent, fast at most 0.1 mm (8.6).
3. **Coexists with other clients.** Makera Studio and Home Assistant can be connected at the same time. The handwheel does not disturb them and cannot be disturbed by them.
4. **Switch on and go.** With reachable Wi-Fi and a reachable machine, the handwheel is ready to jog in under 10 s after switching on. It is set up once via a configuration portal, entirely without code.

## 4. Hard requirements

All versions were checked online on 2026-11-09, the build configuration additionally with a real build (6.3).

| Topic | Requirement | Rationale |
|---|---|---|
| Target board | DFRobot **FireBeetle 2 ESP32-UE** (DFR1140), module **ESP32-WROOM-32UE-N16R2**. This is a classic ESP32 (D0WDR2-V3) with 16 MB flash, 2 MB PSRAM, external antenna and LiPo connector (PH2.0) including USB-C charging. | Existing hardware, confirmed on 2026-11-09 via `esptool flash-id`: chip **ESP32-D0WDR2-V3, revision v3.1**, 16 MB flash, 40 MHz crystal, flash voltage 3.3 V. The USB-serial converter is a **WCH CH340 with product ID 0x7522** (on macOS the WCH driver is required, see `WIRING.md`). The classic ESP32 has 8 PCNT units. |
| Language | C++17 | |
| Framework | **Arduino-ESP32 3.3.x** (stable 3.3.12, based on ESP-IDF 5.5.5) | Ships WebServer, DNSServer and Preferences. The IDF APIs for PCNT, sleep, GPIO and sockets are directly usable. |
| Build | **PlatformIO**, `platform = espressif32 @ ^8.0.0` | Reproducible, tests via `pio test` |
| Encoder | ESP-IDF **PCNT API** `driver/pulse_cnt.h`. The old `driver/pcnt.h` is removed in IDF 6.0 and **forbidden**. | The hardware counts even while the main loop is blocked. |
| Display | **U8g2 2.36.18**: the newest version in the PlatformIO registry. GitHub is already at 2.37.1, but `^2.37.1` cannot be resolved. Constructor `U8G2_SH1106_128X64_NONAME_F_HW_I2C`; before `begin()` call `setI2CAddress(0x3C * 2)` (U8g2 expects the 8-bit address) and `setBusClock(400000)`. | |
| Network to the machine | **lwIP sockets** (`lwip/sockets.h`) in mode `O_NONBLOCK`, no `WiFiClient` | `WiFiClient::connect` and `write` block. That stalls the single loop, and then the gate, session end and buttons do not respond (8.8). |
| Persistence | `Preferences` (NVS), namespace `handrad`, only the keys from 9.1 and `board_rev` (9.2). Volatile state across `esp_restart()`: `RTC_NOINIT_ATTR` (9.2). Deep sleep only when switched off or with an empty battery (9.3). | |
| Wi-Fi setup | Own captive portal built from the core's `WebServer` and `DNSServer`, **no** third-party library | No compatibility risks |
| Concurrency | **One** cooperative main loop, no own FreeRTOS tasks | The hardware counts the detents. Own tasks only bring race conditions. |
| Arithmetic | Positions and distances as **integer micrometres (`int32_t`)**, no `float` in `lib/core` except in the status parser when reading in | `2.0f - 1.95f = 0.04999995` leads to wrong rounding down. This is proven. |
| Tests | Unity via `pio test -e native` for the entire core logic (ch. 11) | |
| Further dependencies | None except U8g2. Each additional one requires an entry in `DECISIONS.md`. | |

## 5. Way of working

- Main branch `main`. `main` is protected server-side: nobody may push directly, merging happens only via merge request with a green pipeline.
- **One step from ch. 12 = one ticket = one branch = one merge request.** Tickets #1–#12 correspond to steps 0–11 and have already been created.
- Branch names: `feature/<ticket>-<slug>` or `bugfix/<ticket>-<slug>`, in lowercase.
- Commit messages in the imperative, in English.
- Every merge request ends buildable: `pio run -e firebeetle` and `pio test -e native` are green, locally and in the pipeline.
- Before every merge a fresh context checks the diff (ch. 15).

## 6. Project setup

### 6.1 Directory tree

```
makera-handrad/
├── SPEC.md
├── CLAUDE.md
├── README.md, LICENSE            # public overview (English), MIT licence
├── .gitlab-ci.yml                 # private repo only, not exported
├── .claude/                       # skills/build-test; settings.json, skills/git-workflow: private repo only, not exported
├── DECISIONS.md
├── HARDWARE_CHECKLIST.md          # all [MANUAL] items, format: "- [ ] H-<n>: …"
├── WIRING.md                      # netlist, bill of materials, terminal assignment
├── platformio.ini
├── include/
│   ├── pins.h                     # ONLY pin numbers as constexpr int, no includes
│   ├── ui_config.h                # display/driver values for src/ only (8.9), position map of the status page (9.1)
│   ├── spoilboard_geometry.h      # spoilboard for the position map, generated by tools/make_spoilboard_geometry.py (9.1)
│   └── a_axis_geometry.h          # rotary module for the 3D view, generated by tools/make_a_axis_geometry.py (9.1)
├── lib/
│   └── core/                      # Arduino-FREE, tested natively
│       ├── core_config.h          # all logic constants (table 8.9), no includes except <cstdint>
│       ├── frame.h/.cpp
│       ├── status.h/.cpp
│       ├── text_lines.h/.cpp      # 0x90 line buffer
│       ├── axis_select.h/.cpp
│       ├── step_policy.h/.cpp
│       ├── jog_gate.h/.cpp
│       ├── detents.h/.cpp         # DetentAccumulator
│       ├── jog_planner.h/.cpp
│       ├── feed_rate.h/.cpp       # feed F from the turning speed (8.7 f, tickets #32, #38)
│       ├── machine_rates.h/.cpp   # maximum machine rates via M203.1: parser, query (8.8, ticket #38)
│       ├── fixed_fmt.h/.cpp       # µm → "12.340" without printf
│       ├── battery.h/.cpp         # mV → percent, critical logic
│       ├── power_switch.h/.cpp    # on/off button debouncing, wake level (soft power, 9.3, ticket #49)
│       ├── board_rev.h/.cpp       # board revision guard: stored vs compiled revision, confirm hold (9.2, #49)
│       ├── http_parse.h/.cpp      # HTTP header, form fields (9.1, ticket #17)
│       ├── settings_rules.h/.cpp  # settings validation, acceptance rules (9.1, OTA 9.5), password lockout (9.1)
│       ├── status_json.h/.cpp     # status snapshot as JSON (9.1)
│       ├── screensaver.h/.cpp     # screensaver: state, wake-up swallows input (10.4, ticket #28)
│       └── unlock.h/.cpp          # unlock after alarm: hold button → one `$X` (8.10, ticket #41)
├── src/                           # Arduino-dependent, thin adapters
│   ├── main.cpp                   # setup()/loop(), AppMode state machine
│   ├── encoder.cpp/.h
│   ├── machine_link.cpp/.h
│   ├── inputs.cpp/.h
│   ├── leds.cpp/.h
│   ├── display.cpp/.h
│   ├── power.cpp/.h
│   ├── settings.cpp/.h            # NVS settings and form fields, shared by portal and web UI
│   ├── portal.cpp/.h
│   ├── webui.cpp/.h               # web UI in normal operation (9.1, ticket #17)
│   ├── web_pages.h                # look of all web pages, constant part of the status page (9.1, ticket #36)
│   ├── web_assets.h               # three.js and 3D view gzipped as byte arrays, generated (9.1, ticket #37)
│   ├── web_asset_paths.h          # path of the 3D module with content hash, generated (ticket #37)
│   ├── ota.cpp/.h                 # OTA update over Wi-Fi with ArduinoOTA (9.5, ticket #35)
│   ├── logo_fallback.h            # neutral "MPG" as XBM for the screensaver (10.4, ticket #45)
│   └── logo_bitmap.h              # private repo only, not exported: Makera logo as XBM from tools/logo_to_xbm.py (10.4)
├── tools/
│   ├── logo_to_xbm.py             # PNG → 1-bit XBM header (ticket #28)
│   ├── make_spoilboard_geometry.py # STEP of the spoilboard → spoilboard_geometry.h (ticket #36)
│   ├── make_a_axis_geometry.py    # Makera 4-axis machine STEP → a_axis_geometry.h (ticket #48)
│   ├── strip_private.py           # public export: drops private blocks and private files (tickets #49, #50)
│   ├── make_web_assets.py         # three.js (npm package, SHA-256 verified) + web/view3d.js → web_assets.h (ticket #37)
│   ├── dump_web_pages.cpp         # host tool: web pages as HTML for browser checks (ticket #36)
│   └── z1_config_download.py      # download a machine file read-only over Wi-Fi, e.g. /sd/config.txt (ticket #38)
├── web/
│   ├── view3d.js                  # source of the 3D view (9.1, ticket #37)
│   └── vendor/                    # three.js 0.160.0 gzip (three, OrbitControls) and license text (MIT)
└── test/
    └── test_core/
```

**Rules:**
- `lib/core` includes nothing except the C++ standard library.
- Time is passed in as `uint32_t now_ms` and never read internally.
- Time differences are always formed as `(uint32_t)(now_ms - t)` so that they remain correct across the overflow after 49.7 days.
- GPIO numbers appear exclusively in `include/pins.h`.

### 6.2 platformio.ini (verified)

```ini
[platformio]
default_envs = firebeetle

[env:firebeetle]
platform = espressif32 @ ^8.0.0
board = esp32dev
framework = arduino
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.partitions = default_16MB.csv
monitor_speed = 115200
build_flags = -DCORE_DEBUG_LEVEL=2
lib_deps =
  olikraus/U8g2 @ ^2.36.18

[env:firebeetle-ota]               ; addendum ticket #35, OTA flashing (9.5)
extends = env:firebeetle
upload_protocol = espota
upload_port = handrad.local
upload_flags =
  --port=3232
  --auth=${sysenv.HANDRAD_OTA_PASS}

[env:native]
platform = native
test_framework = unity
build_flags = -std=c++17
lib_compat_mode = off
```

`board_upload.flash_size` is required because the board profile `esp32dev` specifies 4 MB and the bootloader header would otherwise report 4 MB; the bootloader then discards the 16 MB partition table (observed on real hardware on 2026-11-09, ticket #16). `board = esp32dev` fits the classic ESP32; there is no dedicated board profile for the FireBeetle 2 UE. If `espressif32 @ ^8.0.0` unexpectedly cannot be resolved, the fallback is `platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip`. That decision then goes into `DECISIONS.md`.

### 6.3 Build verification

Checked on 2026-11-09 with exactly this `platformio.ini`:

- **`pio run -e firebeetle`: SUCCESS.** espressif32 8.0.0 and Arduino-ESP32 3.3.12 were used. A test sketch included and called the following interfaces:
  - `driver/pulse_cnt.h` including glitch filter
  - `esp_sleep_enable_ext0_wakeup`
  - U8g2 (SH1106)
  - `WebServer`, `DNSServer`, `Preferences`
  - `analogReadMilliVolts`

  With `default_16MB.csv`, 6.25 MB are available per app partition.
- **`pio test -e native`: PASSED** (placeholder test).
- **`pio test -e native -v`** prints one line `datei:zeile:testname:PASS` per test.

## 7. Hardware

### 7.1 Components

| Part | Data | Status |
|---|---|---|
| Handwheel | **ZSS600-001-100B-5L**: 60 mm, **100 pulses/rev**, **5 V DC ±5 %**, max. 50 mA, **line driver** (A, −A, B, −B), max. 10 kHz. Terminals per manufacturer datasheet (GTEACH/Kesheng ZSS600): top row **A, B, 0V, VCC**, bottom row **/A, /B**. | available |
| Display | SH1106, 128×64, I2C, 0x3C | available |
| Axis buttons | 4 × 12 mm metal push buttons, **momentary**, green LED ring, IP65, labelled X, Y, Z, A (A only needed if the rotary axis is used). Connections per vendor image: **red = LED+, black = LED−, both green = switch contact**. LED for 3–9 V DC (series resistor built in) | to procure |
| On/off switch | 1 × latching 12 mm metal push button (same series as the axis buttons). An **input** (soft power, 9.3, ticket #49): contact between `PIN_PWR_SW` and GND, not in the battery lead. The battery is always connected to the FireBeetle's "Li-ion +/−" pads, so it charges on USB with the switch off. | to procure |
| Battery | 1S LiPo 3.7 V, installed: YELUFT 3000 mAh with protection board (Amazon B0F18QM8SJ). Factory JST 1.25 connector replaced with JST-PH 2.0 (J1). Complete bill of materials: `hardware/BOM.md` | available |
| P-channel MOSFET | **AO3401** (SOT-23, marking A19T; pins 1 = gate, 2 = source, 3 = drain; V_GS(th) −0.5 to −1.3 V; R_DS(on) ≤ 85 mΩ at −2.5 V). 1 × encoder supply (7.2). SOT-23 adapter board recommended. | ordered |
| Resistors | 2 × 10 kΩ (pull-ups A/B), 2 × 10 kΩ (pull-ups buttons Z/A on the input-only GPIO35/36), 1 × 100 kΩ (gate pull-up AO3401). Buttons X/Y and the on/off button use internal pull-ups. | to procure |


### 7.2 Encoder at 3.3 V: measurement and wiring

Measured on 2026-11-09 with a lab power supply on the handwheel (multimeter A against 0V, wheel turned slowly):

| Supply | Low | High (unloaded) | High with 10 kΩ to VCC |
|---|---|---|---|
| 3.3 V | 0.15 V | 2.09 V | **3.28 V** (Low 0.19 V) |
| 3.7 V | 0.15 V | 2.5 V | – |
| 5.0 V | 0.12 V | 3.79 V | – |

/A and /B each behave exactly inversely.

**Conclusions** (these are the points where replicas typically fail):
- In the high state the line driver outputs sit about 1.2 V below the supply. The ESP32 recognises high only from 0.75 × 3.3 V ≈ 2.48 V and tolerates at most 3.6 V.
  - Unloaded, therefore, **no** supply voltage fits: 3.3 V is too low, 5 V too high.
- The output stage drives only weakly upwards. A **10 kΩ pull-up** lifts high to 3.28 V, low stays at 0.19 V. Both lie safely within the ESP32 limits.
- **Decision:** The handwheel runs at **3.3 V**. A and B go **directly** to the GPIOs via a 10 kΩ pull-up each. /A and /B remain unused.
  - No step-up, no level shifter.
- **Outside the datasheet:** The datasheet requires 5 V ±5 %. At 3.3 V the wheel demonstrably switches. Whether it loses no detents when turned briskly is only checked by **[MANUAL] H-4** with the counter reading. If H-4 fails, the fallback is: wheel at 5 V (step-up) and A/B via a BSS138 level shifter. Then an entry goes into `DECISIONS.md`.
- **Switchable encoder supply:** According to the datasheet the wheel draws up to 50 mA. With a flat battery it must be switched off as well (9.3), otherwise it would keep draining the battery. Therefore an **AO3401 as high-side switch** sits between 3V3 and the VCC terminal of the wheel:
  - source to 3V3, drain to wheel VCC,
  - gate directly to `PIN_ENC_PWR` with **100 kΩ pull-up to 3V3**.
  - **GPIO LOW = wheel on.** GPIO HIGH or high-impedance (reset, shutdown) = wheel off.

  At V_GS = −3.3 V the AO3401 is fully switched on.
- **Pull-ups on the switched side:** The two 10 kΩ pull-ups go to the **drain** of the AO3401 (wheel VCC), **not** to 3V3. Otherwise, with the wheel switched off, current would flow into the wheel via the pull-ups and the protection diodes.
- **Power-on sequence:** `encoder.cpp` switches the wheel on, waits `ENC_POWER_SETTLE_MS = 10` and only then starts the PCNT and reads the initial state. Edges generated during power-on are thus not counted.
- **[MANUAL] H-8:** Read the current of the wheel at 3.3 V on the power supply and enter it in `WIRING.md`. Additionally switch the handwheel on ten times at about 3.5 V battery voltage: no reset may occur. Otherwise add a soft start at the gate (10 kΩ in series, 100 nF between gate and source).

### 7.3 Pin assignment

The mapping "silkscreen → GPIO" comes from the sister board FireBeetle 2 ESP32-E (espboards.dev). For the UE variant DFRobot only confirms D9 = GPIO2, D8 = GPIO5 and D4 = GPIO27. **[MANUAL] H-1:** Before soldering, confirm every mapping with the diagnostics screen (10.3).

**Board revision** (ticket #49): `HANDRAD_BOARD_REV`, evaluated only in `include/pins.h` and `src/` (`lib/core` is revision-independent). Without the flag the soft power board (revision 2, this table) is built; an unknown value → `#error`. The revision appears on serial at start (`Platine Rev. n`) and in the diagnostics screen.


| Constant in `pins.h` | GPIO | Silkscreen | Wiring |
|---|---|---|---|
| `PIN_ENC_A` | 23 | MOSI | Handwheel A, 10 kΩ pull-up to wheel VCC (switched) |
| `PIN_ENC_B` | 19 | MISO | Handwheel B, 10 kΩ pull-up to wheel VCC (switched) |
| `PIN_BTN_X` | 13 | D7 | Button X: internal pull-up, contact to GND |
| `PIN_BTN_Y` | 26 | D3 | Button Y: internal pull-up, contact to GND |
| `PIN_BTN_Z` | 35 | A3 | Button Z: 10 kΩ pull-up to 3V3, contact to GND (input only) |
| `PIN_BAT_ADC` | 34 | A2 | Battery divider on the board (see 9.3) |
| `PIN_ENC_PWR` | 18 | SCK | Gate AO3401 (encoder supply), 100 kΩ pull-up to 3V3; **LOW = on** |
| `PIN_SDA` / `PIN_SCL` | 21 / 22 | SDA / SCL | Display |
| `PIN_LED_X` | 14 | D6 | LED+ (red) of button X, LED− to GND; **HIGH = on** (GPIO14 briefly outputs a PWM signal during boot, the LED then flickers briefly) |
| `PIN_LED_Y` | 25 | D2 | ditto button Y |
| `PIN_LED_Z` | 15 | A4 | ditto button Z (strapping pin MTDO, see below) |
| `PIN_BOARD_BTN` | 27 | D4 | Button on the board (pressed = LOW) |
| `PIN_BTN_A` | 36 | A0 | Button A (rotary axis): 10 kΩ pull-up to 3V3, contact to GND (input only) |
| `PIN_LED_A` | 12 | D13 | LED+ (red) of button A, LED− to GND; **HIGH = on** (strapping pin MTDI, see below) |
| `PIN_PWR_SW` | 4 | D12 | On/off button (soft power, 9.3, ticket #49): contact to GND, **internal** pull-up; **closed = LOW = on**. RTC GPIO: wakes the chip from deep sleep (ext0) |

GPIO39 (A1) is unused. The assignment is chosen so that the single-sided carrier board (`hardware/`) can be routed without crossings with the buttons X, Y, Z, A in one connector row and display, handwheel and on/off button in the other (tickets #21, #25, #49).

- **Off-limits:** GPIO0, 2, 5 (strapping), GPIO16 and 17 (PSRAM, R2 variant), GPIO1 and 3 (USB serial), GPIO6–11 (flash). The strapping pins GPIO12 and GPIO15 are used only for button LEDs (see below).
- **Strapping pins as LED outputs (ticket #49):** An LED (with built-in series resistor) from the GPIO to GND can only pull the pin down. GPIO12 (MTDI, LED A) must be LOW at reset for 3.3 V flash: the LED and the internal reset pull-down keep it LOW. GPIO15 (MTDO, LED Z) has its internal pull-up at reset against the LED to GND; either level is harmless, LOW only silences the ROM boot log. In deep sleep both get an RTC pull-down under the hold (9.3). Never put a pull-up or a button on these pins.
- **GPIO4 for the on/off button (ticket #49):** RTC-capable (ext0 wake), internal pull-up, no strapping pin. It is the outermost lane on the right of the FireBeetle (top row pin 12), which reaches J2 at the right end of the upper row without crossings (next to the on/off button in the right enclosure wall).
- **RTC capability:** All LED pins (12, 14, 15, 25) are RTC-capable and are isolated in deep sleep. `PIN_PWR_SW` (4) is RTC-capable and is the only wake source (ext0, RTC pull-up on during deep sleep). The encoder pins (23, 19) and `PIN_ENC_PWR` (18) are not: in deep sleep they are high-impedance, and the 100 kΩ gate pull-up keeps the wheel off. `leds.cpp` and `encoder.cpp` call `rtc_gpio_*` only for RTC-capable pins (`rtc_gpio_is_valid_gpio`).
- **GPIO36 and 39:** According to the Espressif errata they can show short dips when ADC or Wi-Fi is active. All buttons are therefore debounced **by polling only**, never by interrupt.
- **Button logic:** pressed = contact closed = LOW. **[MANUAL] H-2:** Check whether the buttons are normally open (the two green wires). If they are normally closed, switch `BUTTON_ACTIVE_LOW` in `core_config.h` and increase the pull-ups on Z (and A, if fitted) to 100 kΩ, otherwise about 0.33 mA (with A 0.66 mA) flows permanently (H-5); X and Y use internal pull-ups (about 75 µA each).
- **On/off (soft power, ticket #49):** The latching button is an input; the battery is always connected (J1 → pogo pins → "Li-ion +/−"). "Off" means deep sleep (9.3), also on USB. A missing or broken button cable reads as "off" (pull-up), so the handwheel then sleeps: fail-safe. **[MANUAL] H-23** checks the switch behaviour.

### 7.4 LED rings

According to the vendor the button LEDs are rated for 3–9 V DC, the series resistor is built in. Each LED is connected **directly to the GPIO**: LED+ (red) to the GPIO, LED− (black) to GND, **HIGH = on**.

**[MANUAL] H-3:** Before connecting to the GPIO, run one LED directly from 3V3, measure the current (multimeter in series) and judge the brightness. Too dim: `LED_DUTY_PERCENT = 100`.
- Up to 12 mA: the direct connection is fine (ESP32 GPIO: up to 20 mA recommended).
- Above that: use an AO3401 high-side with 100 kΩ gate pull-up (then additionally 3 × AO3401 and 3 × 100 kΩ in the bill of materials, 4 each with button A); then "LOW = on" applies. The firmware switches this with `LED_ACTIVE_HIGH` in `core_config.h`.

Brightness is controlled by LEDC PWM (`LED_PWM_HZ = 1000`, `LED_DUTY_PERCENT = 60`). With `LED_ACTIVE_HIGH == false`, call `ledcOutputInvert(pin, true)` once after `ledcAttach` and never additionally invert the duty cycle.

## 8. Core logic

### 8.1 Protocol to the machine

The source is the Makera Z1 firmware v1.1.3 (github.com/MakeraInc/MakeraZ1Firmware), checked on a real Z1 Pro with firmware 1.1.2.

- **Connection:** TCP to the machine IP, port 2222.
  - Observed on the real machine: the Wi-Fi module allows **multiple clients at the same time** and mirrors the output to all of them.
  - This cannot be checked in the source code because the Wi-Fi module's firmware is not public.
- **Frame format** (big-endian):
  ```
  0x86 0x68 | len (2) | type (1) | payload (n) | crc (2) | 0x55 0xAA
  len = 1 + n + 2
  crc = CRC-16/CCITT-XModem (poly 0x1021, init 0x0000) over len, type, payload
  ```
  - The machine's main board accepts at most 513 bytes of payload.
  - It does not check the CRC; the handwheel computes it correctly anyway.
- **Types:**

  | Type | Direction | Meaning |
  |---|---|---|
  | `0xA1` | → machine | Real-time character, 1 byte; here only `?` |
  | `0xA2` | → machine | Console line, **without** trailing `\n` (`$J`, `M203.1`, `$X`) |
  | `0x81` | ← machine | Status report. **Only this type** is parsed as status. |
  | `0x90` | ← machine | Text output; lines can be spread across several frames |
  | others (incl. `0x82` diagnostics with identically named fields) | ← | ignore |

- **Planner queue** (ticket #32): The machine enqueues every `$J` as a block in a queue with 32 slots. `$J` is executed synchronously during frame processing. If the queue is full, the firmware waits and meanwhile processes further frames, including the next `$J`, nested. This overwrites the still-waiting block (sources in `DECISIONS.md`). The handwheel therefore keeps at most `JOG_MAX_QUEUED = 12` blocks in the queue (8.7 e2). One `$J` yields at most one block (no segmentation), and `MPos` is the real step position, not the target of the last block (DECISIONS).
- **Test vectors:**
  - `crc16("123456789") == 0x31C3`
  - Real frame on `model`: `86 68 00 1d 90` + `model = Z1, 4, 1, 0, Idle\n` + `b1 06 55 aa`
  - Real status report (payload of a `0x81` frame, captured on the machine). The fields `E:` and `OTA:` do not come from the public firmware and are ignored:
    `<Idle|MPos:-1.0000,-1.0000,-1.0000,0.0000,0.0000|WPos:190.5400,193.4900,73.1641,-90.0000,0.0000|F:0.0,3000.0,100.0|S:0.0,10000.0,100.0,0,22.8,24.1,0,0,0,0|T:1,1.329,-1|L:0, 0, 0, 0.0,100.0|C:4,1,0,1|E:0,0,0,25,7613|OTA:0,0>\n`

### 8.2 Interfaces of `lib/core`

Binding header sketches. Names and types are fixed, internal details are free.

```cpp
// frame.h
namespace frame {
  uint16_t crc16(const uint8_t* data, size_t len);
  // Writes a complete frame to out; returns length, 0 if out is too small or n > 513.
  size_t build(uint8_t type, const uint8_t* payload, size_t n, uint8_t* out, size_t out_cap);

  // MAX_RX_PAYLOAD = 600 (core_config.h). Frames with len < 3 or len > MAX_RX_PAYLOAD + 3 are invalid:
  // On any error (invalid len, wrong CRC, wrong footer) the decoder searches again for 0x86 0x68 starting
  // at the byte after the discarded 0x86.
  struct Frame { uint8_t type; uint16_t n; uint8_t payload[MAX_RX_PAYLOAD]; };  // n = payload length

  class Decoder {
   public:
    // Feeds bytes; calls on_frame for every valid frame. Buffers partial frames internally.
    template <class F> void feed(const uint8_t* data, size_t n, F on_frame);
    uint32_t dropped() const;   // counter of discarded frames (diagnostics)
  };
}

// text_lines.h — buffers 0x90 payloads into lines, max. 256 characters per line (longer: truncate)
class TextLines { public: template <class F> void feed(const uint8_t* p, size_t n, F on_line); };

// status.h
enum class MachineState : uint8_t { Idle, Run, Hold, Pause, Alarm, Home, Tool, Sleep, Wait, Unknown };
struct MachineStatus {
  MachineState state;
  int32_t  mpos_um[4];   // X,Y,Z machine position, ALWAYS micrometres; [3] = A in millidegrees
  int32_t  wpos_um[4];   // X,Y,Z work position, ALWAYS micrometres; [3] = A in millidegrees
  bool     has_a;        // MPos and WPos have a 4th value
  bool     inch_mode;    // field C, 3rd value
  bool     playing;      // field P present
  bool     halted;       // field H present
};
// false = "no status" (broken, no '<…>', mandatory field MPos, WPos or C missing).
bool parse_status(const char* text, size_t len, MachineStatus& out);

// axis_select.h — AxisSelector
enum class AxisMode : uint8_t { None, X, Y, XY, Z, A };
enum class Button : uint8_t { X, Y, Z, A };
class AxisSelector {                       // rules see 8.4
 public:
  void on_press(Button b, uint32_t now_ms);   // debounced press edge; applies the auto-deselect first
  void on_activity(uint32_t now_ms);          // call when PCNT counts ≠ 0 (8.7 step 1)
  void clear();                               // deselect everything
  AxisMode mode(uint32_t now_ms);             // applies the auto-deselect
};

// step_policy.h
class StepPolicy {
 public:
  static uint8_t level_for(uint32_t v_detents_per_s, uint8_t prev_level); // pure function, table 8.6
  // Call exactly once per tick; abs_detents = |detents of this tick|. Returns µm per detent.
  int32_t update(uint32_t now_ms, uint32_t abs_detents);
  uint8_t level() const;
  void reset();
};

// jog_gate.h
enum class BlockReason : uint8_t { None, BatteryLow, NoWifi, NoMachine, NoData,
                                   AxisNone, NoAxisA, JobRunning, Alarm, NoRates, MachineBusy };
struct GateInputs {
  bool battery_critical;    // from Battery (9.3), once true → stays true until restart
  bool wifi_ok;             // Wi-Fi connected with IP
  bool tcp_ok;              // MachineLink in state Online
  bool have_status;         // at least one status since Online
  MachineStatus status;     // last status (only valid if have_status)
  uint32_t status_age_ms;   // now − receive time of the last status
  AxisMode axis;
  bool rates_known;         // rates_cover(machine::rates(), axis): maximum rates of the axis (axes) read (8.8)
  bool session_active;      // from the JogPlanner
  bool session_consistent;  // from the JogPlanner: |target − mpos| ≤ WINDOW_UM + FOREIGN_TOL_UM per axis (X, Y, Z, A)
};
BlockReason evaluate_gate(const GateInputs& in);

// detents.h — counts → whole detents
class DetentAccumulator {
 public:
  explicit DetentAccumulator(int32_t counts_per_detent);
  // Internal order: (1) if the last counts≠0 was at least STEP_RESET_MS ago → remainder = 0;
  // (2) remainder += counts; (3) detents = remainder / counts_per_detent (rounds toward 0); remainder -= detents × cpd.
  int32_t take(uint32_t now_ms, int32_t counts);
  void clear();                       // remainder = 0 (mode change, lock)
  int32_t remainder() const;          // for tests
};

// jog_planner.h
struct JogCommand { bool valid; char text[48]; };  // e.g. "$J X0.030 Y0.030 F0.131", "$J A0.030 F0.020"
struct PlannerTickResult {
  JogCommand cmd;        // at most one command per tick
  bool clipped;          // window (5e) or direction lock (5d) has shortened or discarded
  bool session_active;   // state after this tick
};
class JogPlanner {
 public:
  JogPlanner();                                   // rates unknown: never sends
  explicit JogPlanner(const MachineRates& rates);
  void set_rates(const MachineRates& rates);      // main.cpp before every tick with machine::rates() (ticket #38)
  // detents = result of DetentAccumulator::take for this tick (signed).
  // step_um = result of StepPolicy::update for this tick.
  // status may be nullptr (no status); then gate is ≠ None anyway.
  PlannerTickResult tick(uint32_t now_ms, int32_t detents, int32_t step_um, AxisMode axis,
                         BlockReason gate, const MachineStatus* status, uint32_t status_age_ms);
  void notify_send_failed();   // MachineLink could not send the command completely
  void end_session(uint32_t now_ms);  // mode change, connection loss, alarm line; sets last_end_ms,
                                      // clears the FeedEstimator
  bool session_active() const;
  bool session_consistent(const MachineStatus& s) const;  // false if no session
  int32_t target_um(int axis) const;   // for tests; axis 0..3 = X, Y, Z, A
  int8_t  dir() const;                 // for tests: −1, 0, +1
};

// machine_rates.h — maximum rates of the machine (8.8 "machine rates", ticket #38)
struct MachineRates { uint32_t um_s[4]; bool xyz; bool a; };  // X, Y, Z µm/s, A mdeg/s, rounded up
bool rates_cover(const MachineRates& r, AxisMode axis);       // all axes of axis known; None → false
enum class RateLine : uint8_t { NotRates, Valid, Invalid };
RateLine parse_rate_line(const char* line, size_t n, MachineRates& out);  // one response line of M203.1
class RateQuery {
 public:
  void reset();                                   // new connection: unknown, next query immediately
  static bool ready(const MachineStatus* st);     // status present, no alarm, not halted
  static bool refresh_allowed(const MachineStatus* st, bool jog_active);  // ready, Idle, no session
  bool poll(uint32_t now_ms, bool ready, bool refresh_ok = false);  // true → send RATE_QUERY_TEXT now
  RateLine on_line(const char* line, size_t n, uint32_t now_ms);  // every 0x90 line
  bool known() const;
  const MachineRates& rates() const;              // all flags false while unknown
  uint32_t refresh_failures() const;              // consecutive failed re-read attempts
};

// feed_rate.h — feed from the turning speed (8.7 f, tickets #32, #38); the JogPlanner holds a FeedEstimator
bool feed_limits(AxisMode axis, const MachineRates& rates, int32_t& lo, int32_t& cap);  // limits of F
int32_t feed_milli(uint32_t v_um_s, AxisMode axis, const MachineRates& rates);  // F in thousandths;
                                                // 0 only without rates for the axis, otherwise ≥ 1
class FeedEstimator {
 public:
  uint32_t update(uint32_t now_ms, uint32_t want_um);  // exactly once per tick; returns µm/s (A: mdeg/s)
  void reset();
};

// unlock.h — unlock after alarm/HALT (8.10, ticket #41)
struct UnlockInputs { bool normal_mode, online, have_status; MachineStatus status; uint32_t status_age_ms;
                      bool ota_updating, hint_visible, new_message; uint8_t pressed, edges; };  // bits 1 << Button
enum class UnlockView : uint8_t { None, Hint, Holding, Sent };
struct UnlockTick { bool send; UnlockView view; uint16_t permille; const char* aborted; };
class UnlockRequest {
 public:
  static const char* blocked(const UnlockInputs& in);   // nullptr = unlock possible, otherwise reason
  UnlockTick tick(uint32_t now_ms, const UnlockInputs& in);  // exactly once per tick
};

// battery.h
class Battery {
 public:
  // mv = battery voltage already converted ×2 (mean of 16 measurements).
  // startup = true for the single measurement at startup (then one measurement < CRIT suffices for critical).
  void add_sample(uint16_t mv, bool startup);
  uint8_t percent() const;    // 0..100 in steps of 5
  bool warn() const;          // last measurement < BAT_WARN_MV
  bool critical() const;      // latched until restart; never true while absent()
  bool absent() const;        // last measurement < BAT_ABSENT_LOW_MV or > BAT_ABSENT_HIGH_MV (on USB without battery)
};

// power_switch.h — soft power (9.3, ticket #49)
enum class PowerLevel : uint8_t { Unknown, On, Off };
struct PowerTick { PowerLevel level; bool turned_off; };  // turned_off: became Off in this sample, exactly once
class PowerSwitch {
 public:
  explicit PowerSwitch(uint32_t debounce_ms);
  PowerTick update(uint32_t now_ms, bool raw_on);   // level counts once stable for debounce_ms; Unknown before
  PowerLevel level() const;
};
enum class SleepReason : uint8_t { SwitchOff, BatteryEmpty };
bool sleep_wake_when_on(SleepReason reason, bool switch_on_now);  // ext0 wake level, see 9.3

// fixed_fmt.h — no printf/locale, point as separator
size_t format_um(int32_t um, char* out, size_t cap);        // µm → mm, exactly 3 decimal places: -5 → "-0.005"
size_t format_inch_e4(int32_t um, char* out, size_t cap);   // µm → inch, exactly 4 decimal places, rounded
                                                            // half away from zero: 25400 → "1.0000", -1270 → "-0.0500";
                                                            // if rounding yields 0, no minus: -1 → "0.0000"
```

### 8.3 Status report

- **Query:** send a `0xA1 '?'` every `STATUS_INTERVAL_MS = 100` ms. This roughly doubles the status traffic on the connection, but is uncritical for the machine.
- **Format:** `<State|Key:Values|…>`, the trailing `\n` is optional. Ignore unknown fields.
- **Axis A:** The 4th value of `MPos`/`WPos` is the rotary axis A in degrees. It is stored as millidegrees (value × 1000, rounded) in `mpos_um[3]`/`wpos_um[3]`, **independent of `inch_mode`** (rotary axes are not converted; [MANUAL] H-10). If the 4th value is missing, `has_a = false` and `mpos_um[3] = wpos_um[3] = 0`.
  - **Caution:** The real Z1 always reports 5 values in `MPos`/`WPos` (see capture 8.1), even without the rotary module. `has_a` therefore does **not** show whether the module is connected. Only the portal checkbox (`axis_a`) is authoritative. Rule 5a is merely a protection against incomplete reports. What the machine does on `$J A…` without the module is checked by [MANUAL] H-10.
- **Mandatory fields** are `MPos`, `WPos` and `C`. If one is missing, there is no status. Rationale: without `C` the unit is unknown.
- **Conversion to µm:** `inch_mode` ? value × 25,400 : value × 1000, mathematically rounded to whole µm.
  - The firmware reports positions in the active unit, but `$J` **always calculates in mm**.
  - This trap is the reason why everything runs internally in µm.
- **`playing`:** The field `P:` is in the report as long as a file is running, also during pause and for a few reports after an abort. That is exactly what is intended, because no jogging happens during this time.
- **Foreign reports:** Status reports count regardless of who requested them.
- **Receive time:** `MachineLink` records the receive time for each status.

### 8.4 Axis selection (AxisSelector)

The buttons X, Y and Z are momentary. The firmware holds the selection itself. The inputs are debounced by polling with `DEBOUNCE_MS = 30`; only the **press edge** is evaluated. Holding triggers nothing further for axis selection; the only exception is unlocking in alarm (8.10), which runs in addition to the axis selection of the edge.

**Rules for `on_press`** (initial state after startup: `None`):

| State before | X pressed | Y pressed | Z pressed | A pressed |
|---|---|---|---|---|
| None | X | Y | Z | A |
| X | None | XY | Z | A |
| Y | XY | None | Z | A |
| XY | Y | X | Z | A |
| Z | X | Y | None | A |
| A | X | Y | Z | None |

X and Y each toggle, **Z and A are exclusive**: Z or A deselect everything else, and X or Y deselect Z or A respectively. An invalid state "Z or A together with another axis" cannot arise. If several press edges are detected in one tick, the order X, Y, Z, A applies.

**Axis A optional:** If it is not enabled in the portal (NVS key `axis_a`), `main.cpp` never calls `on_press` for button A. The button then has no effect in normal operation, and LED A stays off there. In the diagnostics screen, LED A still shows the button (9.4, H-10).

**Auto-deselect:** `mode(now)` returns `None` and resets the selection if at least `AXIS_IDLE_CLEAR_MS = 60000` have passed since the last activity. Activity means press edges (`on_press`) and any turning movement, i.e. PCNT `counts ≠ 0` (`on_activity`), even when the gate is locked. `on_press` applies the auto-deselect **first**: if the timeout has expired, the initial state `None` applies. A press on X after a 60 s pause therefore yields X, not `None`.

**Additional deselect** (`clear()`, called by `main.cpp`):
- on the transition of MachineLink from `Online` into another state,
- on every change of the `AppMode`.

Rationale: after a connection loss, nobody should continue jogging with a forgotten selection.

**XY** means both axes move by the same amount, i.e. at 45°.

If the `AxisMode` returned by `mode()` changes, `main.cpp` calls `planner.end_session(now)`, `step_policy.reset()` and `accumulator.clear()`. A rotation from before must never arrive on the new axis.

### 8.5 Jog gate (`evaluate_gate`)

The first matching rule determines the result:

1. `battery_critical` → `BatteryLow`
2. `!wifi_ok` → `NoWifi`
3. `!tcp_ok` → `NoMachine`
4. `!have_status || status_age_ms > SEND_MAX_AGE_MS (250)` → `NoData`
5. `axis == None` → `AxisNone`
5a. `axis == A && !status.has_a` → `NoAxisA`
6. `status.playing` → `JobRunning`
7. `status.state == Alarm || status.halted` → `Alarm`
7a. `!rates_known` → `NoRates` (maximum rates of the selected axis (axes) not read since connecting, 8.8; ticket #38). After job and alarm, so that the actual reason is shown there; before 8 and 9, so the jog gate never opens without rates.
8. `session_active && status.state ∈ {Idle, Run} && session_consistent` → **None**
9. `!session_active && status.state == Idle` → **None**
10. otherwise → `MachineBusy`

**This is the most important place in the firmware.** The machine firmware checks no state on `$J`. A `$J` during a job would simply be queued into its queue. This gate is the only protection against that.

- **Session start:** A new session may only start if the Idle status is additionally at most `START_MAX_AGE_MS = 150` ms old. The planner checks this itself (8.7), because rule 9 allows up to 250 ms.
- **Short data outage during a session** (`NoData` up to `NODATA_HOLD_MS`) does not end the session but pauses it (8.7 a0). No command goes out during this time.
- **Residual risk:** If Studio starts a job in the roughly 100–250 ms between two status reports, one more `$J` can still go out before `P:` becomes visible. This is technically unavoidable because the machine acknowledges nothing. Document it in `DECISIONS.md` and in `WIRING.md` under "Safety".
- **Running spindle:** The firmware only reports `Idle` when the motion queue is empty **and** the spindle is off. With the spindle running, the handwheel is therefore always "Maschine belegt" (machine busy). That is intended: with the cutter spinning, there is no manual jogging.

### 8.6 Step size from the turning speed (StepPolicy)

- **Speed** `v` in detents per second: sum of the `abs_detents` of all ticks in the last `SPEED_WINDOW_MS = 500` ms, **including** the current tick, × 2.
  - A ring buffer with 32 entries `{t_ms, detents}` is stored. Entries with `now − t ≥ 500` do not count.
  - Direction changes count too, because counting is absolute.
  - Resolution: 2 detents/s.
- **Levels** (`level_for`):

  | Level | µm per detent | step up if v ≥ | step down if v < |
  |---|---|---|---|
  | 0 | 10 | – | – |
  | 1 | 50 | 8 | 4 |
  | 2 | 100 | 20 | 12 |

  - `level_for` is bindingly defined as follows:
    ```
    L = prev_level
    while (L < 2 && v >= LEVEL_UP_V[L+1])  L++;
    while (L > 0 && v <  LEVEL_DOWN_V[L])  L--;
    return L;
    ```
  - `update` limits stepping up to **one level per tick**. Stepping down goes directly to the target level.
  - If the last tick with `abs_detents > 0` was at least `STEP_RESET_MS = 300` ms before `now` (i.e. `now − t_last ≥ 300`), the level jumps immediately to 0 and the ring buffer is cleared. This check happens **before** entering the current tick. Otherwise the old speed would raise the level again in the next tick.
- **Meaning:** Slow turning (up to 3 detents per half second) stays at 0.01 mm. Level 1 starts only from 4 detents in 500 ms.
- **Maximum:** 0.1 mm per detent, also for Z. The user decided it that way.
- **Important:** The planner takes the step size from the **current** tick, after its detents have been accounted for.

### 8.7 Jog planner and overrun window

`TICK_MS = 20`. Each tick, the main loop calls the following in this order:

**Before step 0: screensaver** (10.4, ticket #28): Press edges (`inputs::poll`) and `counts` (`encoder::take_counts`, step 1) are read first and passed to `ScreenSaver::feed`. If it returns `swallow` (display asleep, wake tick, or until `WAKE_SWALLOW_MS` afterwards), this tick's edges and `counts` are discarded and `accumulator.clear()` is called; steps 0–6 then run unchanged with "no input". The screensaver can only remove input, never create it; gate, planner and window remain untouched.

0. **Axis selection** (`inputs.cpp`, `main.cpp`): Debounce the buttons, call `selector.on_press(b, now)` for every press edge (order X, Y, Z, A), then `axis = selector.mode(now)` **exactly once**. If `axis` differs from the previous tick's value, `planner.end_session(now)`, `step_policy.reset()` and `accumulator.clear()` follow immediately, i.e. **before** steps 2–5. LEDs and display use exclusively this stored `axis`.

1. **Read encoder** (`encoder.cpp`):
   - `pcnt_unit_get_count` returns the accumulated count (`flags.accum_count = 1`, limits ±30000, watchpoints at both limits).
   - `counts = current count − last count`, then `last count = current count`.
   - If `counts ≠ 0`, `selector.on_activity(now)` follows (counts as activity for auto-deselect, even with the gate locked).
   - The counter is **never** reset to zero, because edges would be lost between reading and zeroing.
   - Glitch filter: `max_glitch_ns = 1000`.
   - **Quadrature x4 decoding:** One PCNT unit with two channels. Channel 0: edge A, level B. Channel 1: edge B, level A. Edge actions `DECREASE`/`INCREASE` or mirrored respectively, level actions `KEEP`/`INVERSE` (example "Rotary Encoder" in the IDF docs). This yields 4 counts per quadrature cycle.
   - Direction of rotation: clockwise, `counts` is positive. With reversed wiring, `ENC_INVERT` in `core_config.h` corrects the sign before `counts` is passed on.
   - **[MANUAL] H-4:** In the diagnostics screen (shows the count **after** applying `ENC_INVERT`), turn exactly one revolution clockwise. The count must increase by +400 (100 detents × `COUNTS_PER_DETENT = 4`). One detent must yield exactly 4 counts. If this differs, adjust `COUNTS_PER_DETENT` or `ENC_INVERT` and record it in `DECISIONS.md`.
2. **Compute detents:** `detents = accumulator.take(now, counts)` (`DetentAccumulator`, 8.2). The remainder is discarded if `STEP_RESET_MS` have passed since the last count pulse. Otherwise a remainder between two mechanical detents would later act as half a detent.
3. `step_um = step_policy.update(now, |detents|)`.
4. **Gate:** `evaluate_gate` is called in **every** tick with all fields. `session_active = planner.session_active()`. `session_consistent = have_status ? planner.session_consistent(status) : false`.
5. `planner.tick(now, detents, step_um, axis, gate, status_or_null, age)`. Internally, the following apply in order:
   - a0. **Hold on short data loss** (ticket #19): If a session is active and `gate == NoData`, `status != nullptr`, `status_age_ms ≤ NODATA_HOLD_MS (1000)` and `axis` equals the session axis, the session is **only paused**: no command, detents discarded (`main.cpp` calls `accumulator.clear()`), continue with (g). The session only continues once a fresh status brings the gate back to None **and** this status was received at least `START_GAP_MS` after the hold began: `status_age_ms + START_GAP_MS ≤ (uint32_t)(now − hold_since_ms)` (a1). Until then the session stays paused (no command, detents discarded, (g) runs). After that, rule 8 (state Idle/Run, `session_consistent`), direction lock and window apply unchanged. Reason for a1: after a gap, delayed reports arrive bunched together; a report whose content dates from before the gap must not release a command (same logic as c.3).
     Rationale: On the real Z1, status reports occasionally arrive with gaps of 280–430 ms (outside the ESP, DECISIONS.md). Ending the session at every gap would make jogging stutter every few seconds. Safety: nothing goes out during the gap; after the gap the gate checks with a fresh status (≤ 250 ms) for job, alarm and foreign motion as before. A session **start** on a stale status remains excluded (c).
   - a. otherwise: `gate ≠ None` or `status == nullptr` or `axis` differs from the axis the running session started with, or no rates for `axis` (`!rates_cover(rates, axis)`, ticket #38) → end session, no command. (The axis and rate checks are second safeguards to step 0 and rule 7a.) Additionally, `main.cpp` calls `accumulator.clear()`.
   - b. `detents == 0` → no new detents; collected detents may still be due, continue with (e1).
   - c. **Session start**, if no session is active. All three conditions must hold, otherwise discard and continue with (g):
     1. `status.state == Idle`,
     2. `status_age_ms ≤ START_MAX_AGE_MS`,
     3. the status was received **after the last session end**, with a gap: `status_age_ms + START_GAP_MS ≤ (uint32_t)(now − last_end_ms)` with `START_GAP_MS = 200`. Without a previous session end ("never"), the condition counts as met.

     **`last_end_ms`** is set to `now` exactly when an active session ends (a, g, `notify_send_failed`), and on every `end_session(now)`, even without an active session. Because every session only ends after its last command has been sent, `last_end_ms` never lies before the last send. A `max()` over raw timestamps is therefore unnecessary. It would also be wrong because of wraparound.

     Rationale for 3: After an alarm line or a mode change, the last status often still shows the old `Idle` from before the event. Without this condition, a new session would start in alarm or with a `target` from before the last `$J` (double window). The 200 ms gap covers round-trip times up to about 200 ms. **Residual risk:** With Wi-Fi dropouts over 200 ms, a new session can start on a stale Idle. The overrun is then at worst about `2 × WINDOW_UM`, and at most `2 × (JOG_MAX_QUEUED + 2) = 28 < 32` handwheel blocks are in the queue (e2, with the count bound). Document this in `DECISIONS.md` and in `WIRING.md` under "Safety".

     At start, `target_um = status.mpos_um` (all four axes) and `dir = sign(detents)`.
   - d. **Direction lock** (only with an already running session): If `sign(detents) ≠ dir`, reversal only happens if `|target_um − mpos_um| ≤ REVERSAL_EPS_UM (10)` holds for **all involved** axes. Then `dir = sign(detents)`. Otherwise discard, `clipped = true`, continue with (g).
     In (d), collected, not yet sent detents (e1) of the old direction are also discarded (`clipped = true`).
   - e1. **Bundling** (ticket #32): `want = |detents| × step_um` is added to the accumulated value `pending` (always in direction `dir`), at most up to the smallest `WINDOW_UM` of the involved axes (excess discarded, `clipped = true`). `pending` is sent as **one** command as soon as
     - no handwheel blocks can be queued anymore (e2 counts 0, so the machine is surely at rest; this always applies to the first command of a session, which thus goes out in the session-start tick as before), or
     - `now − pending_since ≥ BUNDLE_MS (40)`, i.e. in the third tick after the first collected detent: one command thus covers 3 ticks = 60 ms of turning. On release, the remainder goes out at the latest 40 ms after its first detent.

     `pending` is **discarded** (never sent) on a0/a1 (hold), a (lock, no status, axis change), every session end (g, `end_session`, `notify_send_failed`) and on d. Sending therefore only happens in a tick in which all conditions for a command hold, exactly as without bundling.
   - e2. **Queue limit:** Sending only happens if fewer than `JOG_MAX_QUEUED = 12` handwheel blocks can be queued. Counting (ticket #32, review):
     - The session's sent blocks are kept in a list **in send order** with end point (first moving axis), direction and send time. The machine processes them in this order.
     - A block counts as finished when a status received at least `START_GAP_MS` after it was sent (`status_age_ms + START_GAP_MS ≤ (uint32_t)(now − sent_ms)`, same logic as c.3 and a1) shows `mpos` at most `ARRIVE_UM` before its end point (in its direction) or beyond. Then all older ones are finished too: the newest such block is removed together with all older ones.
     - As a result, no blocks of the old direction remain as "phantoms" after a reversal (they go with the first confirmed block of the new direction), and a block after a reversal does not count as finished just because an older status happens to lie on its end point.
     - **Bound:** Every sent block is at least `ARRIVE_UM` long (`STEP_UM[0] ≥ ARRIVE_UM`; a block clipped by the window below `ARRIVE_UM` is discarded instead of sent, like a full window overflow). Therefore e2 counts at most **one** block too few (the newest removed one, with at most `ARRIVE_UM` remaining), immediately after a reversal at most two (plus the last block of the old direction). This is the price of the tolerance for rounding of the reported position to steps (e.g. `mpos` stops 3 µm short of the target; without tolerance a phantom would remain, and a detent after a pause would be collected and discarded with the session end (g)). The count is updated in every tick of a session.
     - While the limit is reached, collecting continues (e1) and is later sent as one longer block. Rationale: The machine has 32 planner slots; if the queue is full, the firmware processes the next `$J` nested and overwrites the waiting block (8.1/DECISIONS). That must never happen.
     - The list is cleared at session start and end. If a new session starts on a stale Idle (residual risk c.3), up to 12 + 2 blocks of the old session can still be queued: in total at most `2 × (12 + 2) = 28 < 32`.
     - (g) cannot end a session with a collected remainder via `SESSION_IDLE_MS`: "arrived" and a status more than 750 ms after the last send mean that the last block counts as finished, so the count is 0, and then the remainder goes out in the same tick before (g).
   - e. **Window** per involved axis `a` on the bundled amount: `allowed = max(0, WINDOW_UM[a] − |target_um[a] − mpos_um[a]|)`, `move = min(pending, allowed)`. For XY, `move = min(move_X, move_Y)` applies to both axes. If `move < pending`, `clipped = true`, the excess is discarded. `pending = 0`.
   - f. `move > 0` → create command (with `F` from the turning speed, see "Feed" below), for each involved axis `target_um[a] += dir × move`, `last_send_ms = now`, remember end point for e2.
   - g. **Session end**, checked in every tick **while a session is active**, if one of the conditions applies:
     - gate ≠ None (see a; except while holding after a0),
     - `end_session(now)` was called (mode change, connection loss, alarm/error line, send error),
     - `now − last_send_ms ≥ SESSION_IDLE_MS (1000)` **and** `|target − mpos| ≤ ARRIVE_UM (10)` for all axes,
     - `now − last_send_ms ≥ SESSION_MAX_IDLE_MS (3000)`, as a hard upper limit in case `mpos` never reports the target.
6. If the tick returns a command, `MachineLink` sends it. If sending does not complete fully, `planner.notify_send_failed()` (reverts `target_um` and ends the session) and a disconnect follow.
7. **Unlock** (8.10, ticket #41): `unlock.tick(now, …)` with the debounced levels and the press edges of this tick **after** the screensaver. If it returns `send`, `planner.end_session(now)`, `accumulator.clear()` and sending `$X` follow. In alarm the gate locks (rule 7 or 5), so no `$J` went out in this tick.

**`session_consistent`:** `|target_um[a] − mpos_um[a]| ≤ WINDOW_UM[a] + FOREIGN_TOL_UM (100)` for X, Y, Z and A (A only with `has_a`). If another client moves the machine during a session, this becomes inconsistent, rule 8 no longer applies, and the session ends via (a).

**Residual risk of foreign commands** (document in `DECISIONS.md` and `WIRING.md` "Safety"):
- If another client sends a small move during a session (e.g. MDI `G0` under 100 µm deviation), further handwheel steps queue behind it, in total at most `WINDOW_UM`.
- If another client pauses the machine (hold) and resumes it, already queued handwheel steps continue up to at most `WINDOW_UM`.

**Command format** (frame type `0xA2`, without `\n`):

```
$J X<dx> F<f>        $J Y<dy> F<f>        $J X<d> Y<d> F<f>        $J Z<dz> F<f>        $J A<da> F<f>
```

- For A, `format_um` outputs millidegrees as degrees with 3 decimal places. Step sizes and windows apply to A in millidegrees instead of µm (one slow detent = 0.01°).
- For `session_consistent` and at session end (ARRIVE), A only counts if `status.has_a`.

- Numbers are produced by `format_um`, including `F`: `format_um(feed_milli(...))` writes thousandths with exactly 3 decimal places (`325` → `0.325`).
- In this firmware, `F` is a **factor on the maximum rate** of the slowest involved axis, not a value in mm/min (`SimpleShell::jog`, v1.1.3 `SimpleShell.cpp` lines 1944, 1962–1982: `actuators[i]->get_max_rate()`, i.e. exactly the value that `M203.1` reports, 8.8). The firmware does not limit it to 1 and silently discards F ≤ 0 (`Robot::delta_move`, `Robot.cpp` line 1744). Therefore `F` always lies in `[lo, cap]` from `feed_limits` (below), with `1 ≤ lo ≤ cap ≤ F_MAX_MILLI = 1000`. Without read rates there is no `F` and no command.

- The Z1 firmware has **no** continuous jog (`$J -c` yields `error:bad axis -`).

**Feed from the turning speed** (tickets #32, #38, `feed_rate.h`):
- **Why:** With a fixed `F1.000`, the machine ran every short segment at full speed, was done before the next detent, braked to 0 and waited: stop-and-go. Firmware finding (v1.1.3): The planner always plans the respective last block with exit speed 0 (`Planner.cpp` line 325, `minimum_planner_speed` default 0, line 47). A block that is already running is no longer replanned (`Block.cpp` line 153) and passes its planned exit speed to its successor (line 304). A block that starts while it is the last in the queue therefore ends at standstill. `$J` also bypasses the queue's collection delay (`force_queue()`, `SimpleShell.cpp` line 1984). The machine only moves smoothly as long as the next block is already queued when each block starts. If it moves faster than the wheel is turned, it reduces the backlog down to about its braking distance `v²/2a` (at 3 mm/s 0.03 mm), i.e. less than one block: almost every block then starts as the last one. Therefore it moves slightly **slower** than the wheel is turned, the backlog grows slowly and is limited by the window, and the detents are bundled into longer blocks (e1).
- **Blending:** Consecutive `$J` on the same straight line are joined without stopping, even with different `F`: at a 0° corner the junction speed is `min(nominal_before, nominal_now)` (`Planner.cpp` lines 175–203; junction deviation 0.01 mm only applies at real corners). `F` may therefore change per command.
- **Speed** `v` (µm/s per axis, A in mdeg/s): Measured **only during a session**: `FeedEstimator::update(now, want)` in every tick that passes (a) and in which a session is active or begins, with `want = |detents| × step_um` of this tick **before** window and direction lock (5e/5d), i.e. the speed at which the wheel is turned. No measurement in a0/a1. `v` = sum of all `want` of the last `FEED_WINDOW_MS = 200` ms including the current tick × 1000 / 200. Ring buffer with `FEED_RING = 16` entries (only `want > 0`); if it overflows, the oldest are missing, and `v` only gets smaller. The buffer is cleared at session start and at every session end. A command's `F` comes from `v` of the tick in which it is sent.
- **Formula:** `F_milli = ⌈v × FEED_LEAD_PCT / 100 × p / rate × 1000⌉`, limited to `[lo, cap]`. `rate` is the read maximum rate (8.8) of the slowest involved axis (XY: the smaller of X and Y). `p = 1` for X, Y, Z, A and `p = √2` (`FEED_XY_PATH_PERMILLE = 1414`) for XY, because the firmware applies `F` to the path speed (`Robot.cpp` lines 1564, 1633). Computed in 64-bit, integer, rounded up.
- **Limits from fixed maximum and minimum speeds** (`feed_limits`, ticket #38): `cap = min(F_MAX_MILLI, ⌊JOG_MAX_SPEED_UM_S[a] × 1000 / rate⌋)`, `lo = min(cap, max(1, ⌈JOG_MIN_SPEED_UM_S[a] × 1000 / rate⌉))`; for XY the X/Y values apply to the path. `JOG_MAX_SPEED_UM_S = {20000, 20000, 5000, 30000}` (X, Y or XY path 20 mm/s, Z 5 mm/s, A 30 °/s) and `JOG_MIN_SPEED_UM_S = {400, 400, 200, 1200}` are the absolute speeds that #32 intended with `F_SCALE`/`F_MIN` at the rates assumed back then (20/20/10 mm/s, 60 °/s). **The maximum speed thus does not depend on the machine:** if it reports a higher rate, `cap` decreases; `cap` is rounded down and the rate is read rounded up, so `F × rate` never exceeds `JOG_MAX_SPEED`. At the Z1 Pro's rates (8.8: X/Y 50, Z 16.667 mm/s, A 30 °/s) this gives X/Y/XY `[8, 400]`, Z `[12, 299]` (at most 4.98 mm/s), A `[40, 1000]` (at 60 °/s per the file it would be `[20, 500]`, same speed); with the old assumptions exactly the old values `[20, 1000]`, `[20, 500]`, `[20, 500]` (test 93).
- **Rationale for the values:**
  - Lead `FEED_LEAD_PCT = 92`: The machine moves 0.92 times as fast as the wheel is turned. The backlog grows by 8 % of the turned distance until the window limits it. With bundling, several blocks are then always in the queue and cover gaps in the detents and short status gaps (at 0.5 mm backlog and 5 mm/s about 100 ms).
  - Overrun after steady turning for `T` seconds at `v`: backlog ≈ `0.08 × v × T` + averaging ramp-up (about `0.11 s × v`) + at most 60 ms of collected detents, at most the window. The overrun time is thus about `0.09 × T + 0.2 s`, independent of `v`, at most `WINDOW_UM / (0.92 × v)`. Examples: 2 s at 5 mm/s → about 1.6 mm in 0.4 s; 10 s at 1 mm/s → about 1 mm in 1.1 s; long and fast (20 mm/s) → 5 mm in 0.3 s.
  - 200 ms averaging: 10 ticks, reacts quickly when starting.
  - Bundling over 60 ms: A block is as long as 60 ms of turning (at 5 mm/s 0.3 mm). Fewer, longer blocks mean less planner load and more margin to the e2 limit. The waiting time is short (a single detent with the machine at rest goes out immediately).
  - `JOG_MAX_QUEUED = 12`: Even a new session on a stale Idle with blocks of the old one still queued stays, with the count bound (at most 2 too few per session), at 28 < 32 slots; the rest is reserve for the conservative count (blocks of the last 200 ms and, during status gaps, long-finished ones are counted) and for blocks of other clients. 12 blocks of 60 ms each cover about 0.7 s.
  - Minimum speed `JOG_MIN_SPEED_UM_S`: X/Y 0.4 mm/s, a single slow detent (10 µm) takes about 25 ms; Z 0.2 mm/s, i.e. about 50 ms; A 1.2 °/s. Below this speed the machine moves faster than the wheel is turned, and every block ends at standstill. That fits slow detent-by-detent jogging. Each axis has its own minimum speed so that it (unlike a shared `F_MIN`) does not depend on the rates.
  - The rates are read after every connect (8.8), not hard-coded: the assumption from `configZ1Pro.default` of the public repo (1200/1200/600 mm/min) was wrong on the real machine (there 3000/3000/1000 mm/min, ticket #38); the machine moved X/Y about 2.3 times and Z 1.5 times as fast as the wheel was turned, caught up with the backlog and stuttered.
- **Axis A:** For pure A moves, the firmware additionally converts the rate and, at small rotation radius, speeds up by about 10x, at most to the A maximum rate (`Robot.cpp` lines 1646–1666). The formula with lead 0.92 is applied equally to all axes, including A; the firmware, however, then usually moves A faster than `F` specifies and thus faster than the wheel is turned, so for A the queue can run empty and A can keep stuttering. Window and limit e2 apply unchanged.
- **Safety:** `F` only changes the speed, never the distance. Bundling only changes when collected detents go out: only in a tick in which gate, session and axis allow a command, and the window applies to the bundled command. Jog gate, session start, direction lock and session rules remain unchanged.

**Why window and direction lock?**
- The firmware does not acknowledge `$J`, it only queues it. It adds each delta to the target of the most recently queued move, exactly like `target_um`.
- Within one direction, the machine moves monotonically from `mpos` (reported, older) towards `target`. The real position therefore lies in between, and the remaining distance is at most `|target − mpos| ≤ WINDOW_UM` (rule e).
- Reversal only happens once the old direction has been processed down to `REVERSAL_EPS_UM` (d). The machine can then move at most 10 µm in the old direction before the new one begins.
- Without the direction lock, fast back-and-forth turning could stack forward and return moves in the queue, even though the end point lies within the window.

**Result:** After the last sent command, the machine moves at most `WINDOW_UM + REVERSAL_EPS_UM` further, i.e. 5.01 mm in X/Y, 1.01 mm in Z and 2.01° in A. With on-board means this is indistinguishable from 5 or 1 mm. The window was enlarged with ticket #32 from 2 mm (XY) and 0.5 mm (Z) respectively (user decision): the status only arrives every 100 ms and on the real Z1 with gaps up to 430 ms, so `mpos` lags behind by up to about 0.4 s; with 2 mm even medium speed was limited (display `Begrenzt` (limited)).

**Honest assessment of the speed:**
- Maximum speed independent of the machine rate (`JOG_MAX_SPEED_UM_S`): X, Y and XY path 20 mm/s, Z 5 mm/s, A 30 °/s. Read rates of the Z1 Pro on 2026-11-11: X/Y 3000 mm/min = 50 mm/s, Z 1000 mm/min ≈ 16.67 mm/s, A 30 °/s (`M203.1`; the file says 3600 °/min = 60 °/s, the firmware caps A, 8.8). For A the maximum speed of 30 °/s is thus equal to the machine rate (`F` up to 1.000); the overrun of A remains limited by the window (2°).
- X, Y, XY and Z move about 0.92 times as fast as the wheel is turned; A is usually moved faster by the firmware (Feed above). At an acceleration of 150 mm/s² and small `F`, starting and braking are short (at 3 mm/s about 20 ms and 0.03 mm).
- Fast turning continues to be limited by the window (5 mm XY, 1 mm Z), the maximum speed and the acceleration; it then loses detents. This is intended (core promise 1). If the queue runs empty (turning hesitates, status gap), the machine stops briefly. Complete freedom from jerkiness is **not** promised; the proof on the machine is H-16.
- **Overrun time:** The distance after release is limited by the window. The time is about `0.09 × turning duration + 0.2 s` (derivation above), i.e. under 1 s for short turning motions. After very long slow turning it can take longer than `SESSION_MAX_IDLE_MS`; then the session ends and the handwheel shows `Maschine belegt` (machine busy) until the machine reports Idle again.

### 8.8 Connection sequence (MachineLink)

```cpp
enum class LinkState : uint8_t { Idle, WifiConnecting, TcpConnecting, Online, Backoff };
```

- **Wi-Fi:** connect with the stored credentials, timeout 15 s.
- **TCP** via lwIP socket with `O_NONBLOCK`:
  - `connect()` returns `EINPROGRESS`, then each loop iteration checks via `select()` with timeout 0.
  - Timeout 3 s, no blocking calls.
  - After connecting: set `TCP_NODELAY`, send `?` immediately.
- **Backoff** after every error: 1 s, 2 s, 5 s, then always 5 s. Reset after success.
- **Online** ends on socket error, socket close, any incomplete `send()`, or if no status report arrived for `LINK_STATUS_TIMEOUT_MS = 3000`. Then the socket is closed, `planner.end_session(now)` is called and backoff begins.
- **Sending:** `send()` non-blocking, equally for `?`, `$J`, `M203.1` and `$X`. If a frame is not completely written (partial write or `EAGAIN`), that is a socket error (see above). For `$J`, `planner.notify_send_failed()` is called first (8.7 step 6).
- **Reading:** `recv()` non-blocking, everything available into the decoder.
- **Text lines** (`0x90`): Every line goes first to `RateQuery::on_line` (below). If a line starts with `ALARM` or `error` (case-insensitive), `planner.end_session(now)` is called, and the display shows the line for 2 s (truncated to 21 characters). Other lines are discarded, including the response to `$X` (`[Caution: Unlocked]`, `ok`, 8.10; test 107).
- **Machine rates** (ticket #38, `machine_rates.h`): After every connect, the handwheel reads the effective maximum rates of the axes before the jog gate can open (rule 7a).
  - **Query:** `M203.1` **without arguments** as a console line (`0xA2`, `RATE_QUERY_TEXT`). The firmware (v1.1.3 `Robot.cpp` lines 882–897) responds with a text line ` X: 50  Y: 50  Z: 16.6667  A: 30  B: 1.66667 ` (`%g`, mm/s or °/s), then `ok`. These are the values of `actuators[i]->get_max_rate()`, i.e. exactly those to which `$J` relates its `F`, including `config-override` (M500), corrections by `check_max_actuator_speeds` and the capping of A on some models (`Robot.cpp` lines 295–299). Hence `M203.1` and not `config-get sd alpha_max_rate` (that only reads the file). With arguments, `M203.1` would set the rates; the handwheel never sends arguments (test 95). In HALT the firmware rejects `M203` (not in `allowed_mcodes`): in grbl mode with `error:Alarm lock`, otherwise with `!!`. An `error` line would overwrite the alarm line (8.8 text lines). Therefore it is never queried in alarm (timing).
  - **Observed** on the user's Z1 Pro (firmware 1.1.3.0.1.13, 2026-11-11): The response arrives as **seven** `0x90` frames, one `printf` of the firmware each: `' X: 50 '`, `' Y: 50 '`, `' Z: 16.6667 '`, `' A: 30 '`, `' B: 1.66667 '`, `'\r\n'`, `'ok\r\n'`. `TextLines` assembles them into one line ` X: 50  Y: 50  Z: 16.6667  A: 30  B: 1.66667 ` (leading space, double spaces, `\r` removed) and one line `ok`; status frames (`0x81`) arriving in between do not disturb the line (test 97). **A reports 30 °/s**, although `delta_max_rate 3600` (60 °/s) is in the file: the capping to 1800 mm/min (`Robot.cpp` lines 295–299). Exactly this case shows why the file is not enough.
  - **Timing:** `RateQuery::poll(now, ready, refresh_ok)` in every iteration in state Online. `ready = RateQuery::ready(status)`: a status is present and reports neither `Alarm` nor `halted`; i.e. only after the first status report of the connection and **never in alarm**. At most one query at a time. Without a valid response after `RATE_QUERY_TIMEOUT_MS = 1000` or on an invalid response, the next one follows after `RATE_RETRY_MS` = 1 s, 2 s, 5 s, then always 5 s; if the machine is not `ready` (alarm) during the wait, the wait is dropped, and the next query goes out with the first status without alarm (the stages start over). An incomplete `send()` of the query is treated the same as for `?` (socket error). The `?` cadence and `LINK_STATUS_TIMEOUT_MS` keep running independently. Every disconnect (`fail`, `stop`) and every new Online resets the rates to unknown: **re-read on every connect**.
  - **Re-reading** (review): If the rates are known, a further query follows every `RATE_REFRESH_MS = 30000` (from the last valid response or the last failed re-read), but only with `refresh_ok = RateQuery::refresh_allowed(status, jog_active)`: `ready`, state `Idle` and no handwheel session (`machine::set_jog_active`, from `main.cpp` after every tick). A valid response replaces the rates. An invalid response or a timeout leaves the old, valid rates in place; the jog gate stays open, serial once per error series `Raten: erneutes Lesen ohne Antwort, alte Raten bleiben` (rates: re-read without response, old rates kept) or `Raten: Antwort verworfen: …` (rates: response discarded: …), a changed response as a new `Raten:` (rates:) line. While the re-read is in progress, the old rates continue to apply. **Within a session the planner never changes the rates** (`JogPlanner::set_rates` stores them and adopts them at session end); rates that no longer cover the session axis (disconnect) apply immediately and end the session (8.7 a).
  - **Evaluation** (`parse_rate_line`): Lines that do not start (after spaces) with `X:` are not a response (`NotRates`) and change nothing; thus status, `ok`, output of other clients (the Wi-Fi module mirrors everything to everyone, 8.1) do not interfere. A line with `X:` must consist entirely of pairs `<letter>: <number>`: X, Y, Z exactly once, A at most once, B and C are read and ignored, any other letter (e.g. `S:` from plain `M203`), duplicates, sign, exponent or garbage → `Invalid`. Number: at most 7 integer and 6 decimal digits, **rounded up** to µm/s or mdeg/s. Plausible per axis is `RATE_MIN_UM_S … RATE_MAX_UM_S`, i.e. X, Y, Z 60 … 10000 mm/min and A 60 … 36000 °/min; otherwise `Invalid`. If A is missing (no rotary axis configured), X, Y, Z are known and A is not (axis A stays locked).
  - **Valid response** is adopted as long as the rates are unknown, even late after a timeout (it carries the current values). After that, further lines are ignored, except during a re-read (see Re-reading). Serial: `Raten: X 50.000 Y 50.000 Z 16.667 mm/s, A 30.000 Grad/s (M203.1)`, discarded responses as `Raten: Antwort verworfen: …`, retries as `Raten: Abfrage <n>` (rates: query <n>).
  - **Why the ranges:** They are only meant to catch garbage and misoperation, not to limit the speed; that is done by `JOG_MAX_SPEED_UM_S` independently of the rate (8.7). 60 mm/min is far below any sensible axis rate (the firmware defaults are at 3000), 10000 mm/min well above the Z1 Pro (3000) and above what its stepper drive can sensibly achieve. At 10000 mm/min, `cap` is still 119 or 29 (Z) thousandths, so the resolution of `F` remains fine enough. A: 60 … 36000 °/min (Z1 Pro 3600). Within the ranges, `1 ≤ lo ≤ cap` holds (`static_assert`, test 93).
  - **Residual risk:** If someone changes the rates while the handwheel is connected (`M203.1 X…` from another client, configuration reloaded), the old values apply until the next re-read (at most about 30 s in idle, never within a session) or until the end of the running session. Consequence: only the ratio of turning speed to travel speed is off. If the rate rises, the machine moves faster, at most `F_MAX_MILLI / 1000 ×` the new rate instead of `JOG_MAX_SPEED`; the distance remains limited by window and direction lock. If it falls, it moves slower. A restart of the handwheel or a disconnect re-reads immediately.

### 8.9 Constants (`lib/core/core_config.h`)

Single source of all logic values. The file contains only `#include <cstdint>` and `constexpr` definitions. Everything from this table stays in `core_config.h`, even if only `src/` uses it. Only the following pure display and driver details are in `include/ui_config.h`: display durations (5 s "Akku leer" (battery empty), 1 s start, 1 s OTA result), LED blink frequencies (9.4), truncation to 21 characters, default port 2222, `AP_PASS_DIGITS = 8`, PCNT limits ±30000, `PCNT_GLITCH_NS = 1000`, travel and plate position of the position map (9.1, ticket #36).

| Constant | Value | Section |
|---|---|---|
| `FW_VERSION` | `"0.1.0"` | 10.2 |
| `BUTTON_ACTIVE_LOW` / `LED_ACTIVE_HIGH` | `true` / `true` | 7.3 / 7.4 |
| `AXIS_IDLE_CLEAR_MS` | `60000` | 8.4 |
| `START_GAP_MS` | `200` | 8.7 |
| `ENC_POWER_SETTLE_MS` | `10` | 7.2 |
| `ENC_INVERT` | `false` | 8.7 |
| `COUNTS_PER_DETENT` | `4` | 8.7 |
| `TICK_MS` | `20` | 8.7 |
| `DEBOUNCE_MS` | `30` | 8.4 |
| `STATUS_INTERVAL_MS` | `100` | 8.3 |
| `SEND_MAX_AGE_MS` / `START_MAX_AGE_MS` | `250` / `150` | 8.5 |
| `SPEED_WINDOW_MS` / `SPEED_RING` | `500` / `32` | 8.6 |
| `STEP_UM` | `{10, 50, 100}` | 8.6 |
| `LEVEL_UP_V` / `LEVEL_DOWN_V` | `{0, 8, 20}` / `{0, 4, 12}` (index 0 is never read) | 8.6 |
| `STEP_RESET_MS` | `300` | 8.6, 8.7 |
| `WINDOW_UM` | `{5000, 5000, 1000, 2000}` (X, Y, Z in µm; A in millidegrees; ticket #32) | 8.7 |
| `REVERSAL_EPS_UM` / `ARRIVE_UM` / `FOREIGN_TOL_UM` | `10` / `10` / `100` | 8.7 |
| `SESSION_IDLE_MS` / `SESSION_MAX_IDLE_MS` | `1000` / `3000` | 8.7 |
| `NODATA_HOLD_MS` | `1000` | 8.7 a0 |
| `JOG_MAX_SPEED_UM_S` | `{20000, 20000, 5000, 30000}` (X, Y or XY path, Z in µm/s; A in mdeg/s; maximum speed, ticket #38) | 8.7 |
| `JOG_MIN_SPEED_UM_S` | `{400, 400, 200, 1200}` (minimum speed, lower bound of F) | 8.7 |
| `F_MAX_MILLI` | `1000` (F never above the machine rate) | 8.7 |
| `FEED_WINDOW_MS` / `FEED_RING` / `FEED_LEAD_PCT` / `FEED_XY_PATH_PERMILLE` | `200` / `16` / `92` / `1414` | 8.7 |
| `BUNDLE_MS` / `JOG_MAX_QUEUED` | `40` / `12` | 8.7 e1, e2 |
| `RATE_QUERY_TEXT` | `"M203.1"` (without arguments) | 8.8 |
| `RATE_MIN_UM_S` / `RATE_MAX_UM_S` | `{1000, 1000, 1000, 1000}` / `{166667, 166667, 166667, 600000}` (X, Y, Z µm/s, A mdeg/s: 60 … 10000 mm/min, 60 … 36000 °/min) | 8.8 |
| `RATE_QUERY_TIMEOUT_MS` / `RATE_RETRY_MS` | `1000` / `{1000, 2000, 5000}` (then 5000) | 8.8 |
| `RATE_REFRESH_MS` | `30000` (re-read in idle without session) | 8.8 |
| `UNLOCK_TEXT` | `"$X"` (console line `0xA2`) | 8.10 |
| `UNLOCK_HOLD_MS` / `UNLOCK_MIN_INTERVAL_MS` / `UNLOCK_SHOW_MS` | `5000` / `2000` / `2000` | 8.10 |
| `TX_MAX_PAYLOAD` / `MAX_RX_PAYLOAD` / `TEXT_LINE_MAX` | `513` / `600` / `256` | 8.1, 8.2 |
| `WIFI_TIMEOUT_MS` / `TCP_TIMEOUT_MS` / `LINK_STATUS_TIMEOUT_MS` | `15000` / `3000` / `3000` | 8.8 |
| `BACKOFF_MS` | `{1000, 2000, 5000}` (then 5000) | 8.8 |
| `ALARM_SHOW_MS` / `CLIPPED_SHOW_MS` | `2000` / `500` | 8.8, 10.1 |
| `BAT_EMPTY_MV` / `BAT_FULL_MV` / `BAT_WARN_MV` / `BAT_CRIT_MV` | `3300` / `4150` / `3400` / `3300` | 9.3 |
| `BAT_SAMPLES` / `BAT_INTERVAL_MS` / `BAT_CRIT_COUNT` | `16` / `5000` / `3` | 9.3 |
| `PORTAL_TIMEOUT_MS` | `600000` | 9.1 |
| `NEXT_MODE_MAGIC` / `NEXT_MODE_DIAG` | `0x4D4B5231` / `0xD1` | 9.2 |
| `BOOT_DEBOUNCE_MS` | `50` | 9.2 |
| `BAT_ABSENT_LOW_MV` / `BAT_ABSENT_HIGH_MV` | `2800` / `4400` | 9.3 |
| `POWER_SW_ACTIVE_LOW` / `POWER_SW_DEBOUNCE_MS` / `POWER_SW_BOOT_MAX_MS` | `true` / `100` / `1000` | 9.2, 9.3 (ticket #49) |
| `LED_PWM_HZ` / `LED_DUTY_PERCENT` | `1000` / `60` | 7.4 |
| `DISPLAY_MAX_HZ` | `5` | 10 |
| `WEB_PORT` / `WEB_HOSTNAME` | `80` / `"handrad"` (mDNS `handrad.local`) | 9.1 |
| `WEB_REQ_MAX` / `WEB_REQ_TIMEOUT_MS` | `2048` / `2000` | 9.1 |
| `WEB_AUTH_LOCK_MS` / `WEB_RESTART_FLUSH_MS` | `2000` / `300` | 9.1 |
| `WEB_SLOTS` / `WEB_LISTEN_BACKLOG` / `WEB_LISTEN_RETRY_MS` | `2` / `4` / `1000` | 9.1 |
| `WEB_JSON_MAX` / `WEB_FIELD_MAX` | `1536` / `80` | 9.1 |
| `WEB_LOG_MIN_MS` | `5000` | 9.1 |
| `OTA_PORT` / `OTA_SLOW_HANDLE_MS` | `3232` / `100` | 9.5 |
| `OTA_AUTH_LOCK_MS` / `OTA_LISTEN_RETRY_MS` | `5000` / `5000` | 9.5 |
| `SCREENSAVER_MS` / `DISPLAY_OFF_MS` / `SAVER_MOVE_MS` | `60000` / `600000` / `5000` | 10.4 |
| `WAKE_SWALLOW_MS` / `WAKE_MIN_COUNTS` | `150` / `COUNTS_PER_DETENT / 2` (= 2) | 10.4 |

### 8.10 Unlock after alarm (`unlock.h`, ticket #41)

If the machine is in alarm or HALT, the user can unlock it at the handwheel, as with the unlock dialog in Makera Studio or CarveraController (`Controller.unlock()` sends `$X`, `Controller.py` lines 1041–1046, `sendGCode` → `executeCommand` → `executeMultiCharCommand`: frame `0xA2` without `\n`, lines 284–315, 370–377, 1008–1009). The decision lies with the user, **independent of the alarm reason**: no list of permitted reasons, no "homing required" lock.

- **Trigger:** An axis button (X, Y, Z; A only with `axis_a`, 8.4) is held continuously for `UNLOCK_HOLD_MS = 5000` (debounced level) while the machine reports alarm. Never automatically, never via the wheel, never via the web UI (it stays read-only, 9.1). The press edge selects the axis as before (`AxisSelector` unchanged); the hold is added on top.
- **Conditions** (`UnlockRequest::blocked`, mandatory at the start of the hold and in the send tick, the first applicable one is the reason): AppMode `Normal` (`Betriebsart` (operating mode)), no OTA transfer (`OTA`), MachineLink `Online` (`keine Verbindung` (no connection)), status present and at most `SEND_MAX_AGE_MS` old, the same limit as gate rule 4 (`kein frischer Status` (no fresh status)), `state == Alarm || halted` (`kein Alarm` (no alarm)).
- **Start of the hold:** only with a **press edge**, while all conditions hold and the hint is visible (`hint_visible`: display `On` and no alarm/error line with priority, 10.1) and "Entsperren..." (unlocking...) is not currently shown. A button pressed before the alarm only counts after release and a new press. The hold belongs to the button that started it (with several edges in one tick, order X, Y, Z, A).
- **Abort** (checked in every tick of the hold): different operating mode (`Betriebsart`), OTA (`OTA`), not `Online` (`keine Verbindung`), no status (`kein Status` (no status)), a **fresh** status without alarm (`kein Alarm`), a new alarm/error line from the machine (8.8, `alarm_ms` changes: `neue Meldung` (new message)) or the button is released (`losgelassen` (released)); serial `Entsperren: abgebrochen (<Grund>)` (unlock: aborted (<reason>)). The time only restarts with a new press, even if the condition returns and the button is still held. **A stale status alone does not abort** (Wi-Fi stalls, status gaps up to 430 ms on the real Z1, 8.7 a0): `$X` moves nothing, and sending only happens on a fresh status with alarm (below).
- **Sending:** In the first tick with at least `UNLOCK_HOLD_MS` of holding in which `blocked()` reports nothing (fresh status with alarm); if the status is stale when the 5 s are reached, it waits as long as the button stays held and no abort occurs, at most until `LINK_STATUS_TIMEOUT_MS = 3000` after the last status: then MachineLink drops the connection (8.8), and the hold ends with `keine Verbindung`. Exactly one `$X` (`UNLOCK_TEXT`) as console line `0xA2` without `\n`, the same path as `M203.1` and `$J` (`machine::send_line`); serial `Entsperren: $X gesendet` (unlock: $X sent). After that the hold is consumed: a further `$X` only after release and a new hold for the full time, additionally no earlier than `UNLOCK_MIN_INTERVAL_MS = 2000` after the last one (second safeguard; with a 5 s hold time it never takes effect first). An incomplete `send()` is a socket error as with `?` (disconnect, serial `Entsperren: $X nicht vollstaendig gesendet` (unlock: $X not completely sent)).
- **After unlocking nothing moves by itself** (jog gate, planner and overrun window 8.5–8.7 unchanged):
  - Detents turned in alarm or during the hold expire: the gate locks (rule 7, without axis rule 5), `main.cpp` calls `accumulator.clear()` in every locked tick, the planner starts no session (8.7 a) and collects nothing (`pending` only exists within a session).
  - When sending, `main.cpp` calls `planner.end_session(now)` and `accumulator.clear()`. Thus 8.7 c.3 applies from the `$X` on: a new session only starts on an Idle status received at least `START_GAP_MS` after the `$X`, and only with new turning. Without this call, an Idle a few ms after the `$X` (generated before the machine's response or delayed) could have started a session, provided no alarm line came before (e.g. when connecting in HALT) — this is the only addition to the existing logic, a tightening (test 106).
  - On HALT, the firmware empties the motion queue (`Conveyor::on_halt` → `flush_queue()`, `Conveyor.cpp` lines 87–92). After `$X`, no old block remains in the machine.
  - After `$X`, the machine resets its position from the motor positions (`Kernel.cpp` lines 766–772, `reset_position_from_current_actuator_position()`). The rates are read as before: never in alarm (`RateQuery::ready`), immediately after the first status without alarm if still unknown (8.8).
- **Firmware** (v1.1.3, read only): `0xA2` = `PTYPE_CTRL_MULTI` (`PublicData.h` line 15) goes as a console line to `ON_CONSOLE_LINE_RECEIVED` (`SerialConsole.cpp` lines 190–201), even in HALT (no halt filter on this path). `GcodeDispatch` skips lines with `$` (`GcodeDispatch.cpp` lines 74–76), `SimpleShell::on_console_line_received` accepts them (`SimpleShell.cpp` line 329) and handles `$X` in lines 347–351: only if `is_halted()` is the HALT cleared (`call_event(ON_HALT, (void*)1)`) and `[Caution: Unlocked]\nok\n` output, one `printf` = one `0x90` frame (`SerialConsole::printf`, lines 473–491). Without HALT, the firmware does **not respond at all** to `$X`. The state `Alarm` in the status report is exactly `halted` (`Kernel::get_state`, `Kernel.cpp` lines 291–292), the field `H:` is present when `halted` (lines 525–526). A `Ctrl-X` or a limit switch reports `HALTED, M999 or $X to exit HALT state` (`SerialConsole.cpp` line 519) or `ALARM: …` respectively.
- **Response lines:** `[Caution: Unlocked]` and `ok` start neither with `ALARM` nor with `error` and are discarded (8.8 text lines); `RateQuery::on_line` returns `NotRates` for both in every phase (start, query in progress, known, re-read in progress) and changes neither schedule nor rates (test 107).
- **Display** (10.1): As long as unlocking is possible, the bottom line shows `Entsperren: Taste 5 s` (unlock: hold button 5 s) (21 characters, without unit); during the hold `Entsperren` (unlock) with a progress bar (while waiting for a fresh status it stays at 999 ‰, full only when sending); after sending `Entsperren...` (unlocking...), until a status without alarm arrives, the connection ends or `UNLOCK_SHOW_MS = 2000` have passed (then the hint again, if still in alarm). An alarm/error line from the machine has 2 s priority over the hint; during this time no hold begins. The web UI shows in the lock reason `Alarm, Taste 5 s: Entsperren` (alarm, button 5 s: unlock), during the hold `Alarm, Entsperren läuft` (alarm, unlocking in progress), after sending `Entsperren...`, read-only (only `/status.json`, the status page itself is unchanged by this). LEDs unchanged (9.4: selected axis blinks because the gate locks).
- **Screensaver** (10.4): The alarm wakes the display as attention without a wake window; the next input acts normally, the hint is then visible. If the display is asleep anyway (alarm lasts longer than `SCREENSAVER_MS` without input), the screensaver swallows the waking edge; the hold does not begin, only a new press with the hint visible counts.
- **Residual risk:** `$X` clears the HALT regardless of the reason, even after a limit switch or without homing; the machine then knows its position only from the motor positions. This is the user's deliberate decision (as in Studio). After that, the handwheel only moves on new turning, with all rules of 8.5–8.7. If the machine runs into the same limit switch after unlocking, a new alarm follows. The press edge of the held button acts like any press on the `AxisSelector` (8.4): it selects the axis, deselects an already selected one (X or Y with X or Y selected), forms XY with another selected axis (Y with X, X with Y), or with Z/A deselects everything else. After unlocking, the selected state is therefore the one this press produces; whoever turns then moves it. LED and display show this. Documented in `DECISIONS.md` and `WIRING.md` "Safety"; checked on the machine with H-21.

## 9. Further building blocks

### 9.1 Setup portal (web UI)

- **Starting the web UI** (captive portal):
  - **hold X at power-on**, i.e. hold X, switch the on/off button on and keep X pressed until the portal screen appears,
  - or hold the board button (GPIO27) at power-on,
  - or automatically when no credentials are stored.
- **Access point:** `Makera-Handrad-<letzte 4 Hex der MAC>` (last 4 hex digits of the MAC), WPA2 password of 8 random digits. It is generated on first start and stored as `ap_pass`. The display shows SSID, password and `192.168.4.1`.
- **Form:**
  - Wi-Fi SSID with scan list,
  - Wi-Fi password,
  - machine IP (required, checked as IPv4),
  - port (default 2222),
  - checkbox "Drehachse A vorhanden" (rotary axis A present) (default off),
  - checkbox "Update über WLAN erlauben" (allow update over Wi-Fi) (default off, 9.5, ticket #35),
  - button "Diagnose starten" (start diagnostics).
- **Look:** Portal, `/settings` and status page share style and header from `src/web_pages.h` (`WEB_PAGE_HEAD`, ticket #36), without external resources and without manufacturer logo.
- **Saving** leads via `esp_restart()` into normal operation. "Diagnose starten" sets `rtc_next_mode = NEXT_MODE_DIAG` and `rtc_next_mode_magic = NEXT_MODE_MAGIC` (9.2) and leads via `esp_restart()` into the diagnostics screen.
- **Timeout:** After `PORTAL_TIMEOUT_MS = 600000` (10 min) without a page request, an `esp_restart()` follows if credentials are stored. Without credentials the web UI stays active.
- **NVS keys** (namespace `handrad`): `ssid`, `pass`, `host`, `port`, `ap_pass`, `axis_a`, `ota` (bool, default `false`, 9.5), `board_rev` (uint8, board revision guard 9.2), no others.
- **No auto-discovery:** The UDP broadcasts on port 3333 do not cross VLAN boundaries.

**Web UI in normal operation** (ticket #17):

- **Start:** Only once Wi-Fi has connected for the first time in normal operation. Reachable at `http://<IP>/` (the IP is printed on serial and on the status page) or via mDNS at `http://handrad.local/`. It does not run in portal and diagnostics mode; the captive portal stays unchanged.
- **Server:** Own small HTTP server on lwIP sockets in `O_NONBLOCK` mode (as in 8.8), **not** `WebServer`: its `handleClient()` waits up to 5 s for header lines and POST data and stalled the loop while doing so. Two connections at once (so an idle pre-connection from the browser does not block polling), per loop iteration at most one `accept` and per connection one `recv` or one `send`. Request including body at most `WEB_REQ_MAX = 2048` bytes (otherwise 413), duplicate `Content-Length` → 400. For reading the request and for sending the response at most `WEB_REQ_TIMEOUT_MS = 2000` each, then the connection is closed; exception since ticket #37 are the static files under `/a/` (up to about 170 KB): there the send timeout is **progress-based** (`web_send_expired`): the connection is closed when no `send()` has accepted a byte for `WEB_REQ_TIMEOUT_MS` or the response is older than `WEB_ASSET_SEND_MAX_MS = 30000` (pages and JSON: upper limit `WEB_REQ_TIMEOUT_MS`, i.e. unchanged 2 s; reading the request stays at 2 s); all timestamps of a connection come from the `now` of the loop iteration (ticket #29). The firmware reports failed or rejected requests on serial as `Web: Anfrage verworfen (<Grund>)` (web: request discarded (<reason>)), at most one line per `WEB_LOG_MIN_MS = 5000`; an idle pre-connection without a single byte does not count as an error. Every response with `Connection: close` and `X-Content-Type-Options: nosniff`. Form and error pages use the settings from `main.cpp` loaded at start; the web UI reads NVS only when saving.
- **Pages:**
  - `GET /` status page, read-only, 14,072 bytes (as of ticket #48, measured with `tools/dump_web_pages.cpp`; target below 14 KiB = 14,336 bytes, previously 12 KiB; the 2D/3D toggle and the loader cost about 1.4 KB, the desktop grid and the A rule from ticket #39 166 bytes, the red colour also for the unlock hint from ticket #41 3 bytes, the CAD rotary module of ticket #48 285 bytes: 316 bytes geometry in the flash part, 31 bytes less in the dynamic head), without external resources (no CDN, no web fonts, system font, inline SVG only). A small script polls `/status.json` once per second (the next poll only after the response, abort after 3 s). Structure since ticket #36 (`src/web_pages.h`): a dynamic head of 183 bytes (doctype and calibration object `C` from `ui_config.h`) as `String`, followed by the constant remainder of 13,889 bytes directly from flash (`kStatusTail`, no copy to the heap); `send_some()` sends the `String` first, then the flash part.
    - Content: machine state large in state colour (Ready green, Running/Job blue, Halted/Pause/Tool change yellow, Alarm red) with the handwheel's lock reason (padlock when locked) and last alarm line; position X/Y/Z (A only with `axis_a_enabled && has_a`, ticket #39: the machine always reports five values, `has_a` alone says nothing about the module; the setting "Drehachse A vorhanden" is decisive) large as WPos, below it small the MPos, selected axis highlighted, age of the status (yellow from 1.5 s); position map; cards Connection, Handwheel (battery, axis, session, rotary axis A, settings saveable) and collapsible link statistics. Light/dark according to `prefers-color-scheme`, mobile first (390 px, single column). From 900 px two columns (ticket #39): columns 3 : 2, left the position map (2D at most 70 vh high, 3D 70 vh, at least 24 rem), right stacked state, position, connection, handwheel, link statistics; width at most 92 rem (1472 px). If `/status.json` does not respond, the top shows in red "Keine Verbindung zum Handrad" (no connection to handwheel) with the age of the last response, the rest turns grey.
    - **Position map** (2D, inline SVG): top view of the spoilboard (outline and holes from the Makera STEP, `include/spoilboard_geometry.h`, generated with `python3 -I tools/make_spoilboard_geometry.py <STEP>`), the travel dashed, crosshair at the MPos, work zero (MPos − WPos) as quarter circle; next to it Z as vertical bar (tool and work zero Z), A as small disc only with `axis_a_enabled && has_a`. The markers glide to the new position in 0.9 s (not with `prefers-reduced-motion`), the numbers always show the last real value. All views are functions in `V` and receive the same `/status.json` object; the 3D view (ticket #37) hooks in there.
    - **3D view** (ticket #37, `web/view3d.js`, three.js 0.160.0): toggle "2D | 3D" in the header of the position map; 2D is the default. The browser remembers the choice (`localStorage`, in `try/catch`). Only on toggling (or on load, if 3D is remembered) does the page load the 3D module via `import()`; the status page is complete immediately without it. If WebGL is missing or loading fails, the map stays 2D with the hint "3D-Ansicht nicht verfügbar (…)" (3D view not available (…)). Scene in machine mm, Z up: spoilboard procedurally from `spoilboard_geometry.h` (6 mm, countersunk holes 2 mm deep), travel as dashed cuboid, tool (tip at the MPos) with plumb line and ring on the board top surface, work zero (MPos − WPos) as quarter disc with short X/Y/Z axes, track of the last 240 positions (fading). With `axis_a_enabled && has_a` (ticket #39) additionally the rotary axis module, simplified from Makera's CAD model (ticket #48, `include/a_axis_geometry.h`, generated with `python3 -I tools/make_a_axis_geometry.py <STEP>` from `Makera-Z1_Simplified_4Axis.step` in `hardware/vendor/a_axis/`, private repository only): rail, headstock and tailstock as boxes, motor housing, chuck with four jaws, tailstock quill with live centre; axis parallel to X, 93.26 mm behind the front left board corner and 45.00 mm above the board top; stock Ø 30 mm (not in the model) from the jaws to the live centre, the jaws open to it. Chuck, jaws and stock rotate with A (MPos). Bottom left a chip like the position display: work angle (WPos) large, machine angle small, e.g. "A −90,000° · Maschine 0,000°" (A −90.000° · machine 0.000°), while `UI_A_POS_CALIBRATED = false` and the module is shown, with "Lage der Drehachse aus CAD" (rotary axis position from CAD). Switch "A-Modul" (A module) (title "A-Modul anzeigen" (show A module), left of "Ansicht zurücksetzen" (reset view), only visible with rotary axis enabled and reported) hides only the module, the chip stays; default on, the browser remembers the choice (`localStorage`, in `try/catch`). If the rotary axis is deselected (or the status is missing), row, disc, module, chip and switch disappear with the next status. Rotate/zoom/pan (OrbitControls), button "Ansicht zurücksetzen". Tool and A glide as in 2D in 0.9 s (not with `prefers-reduced-motion`). Rendering only on change or interaction, pixel ratio at most 2, colours from the page tokens (light/dark). Read-only.
    - **Calibration** (`include/ui_config.h`): `UI_TRAVEL_{X,Y,Z}_{MIN,MAX}_UM` = travel in MPos, read at the machine (`soft_endstop.*_min` = −207/−206/−102 mm, maximum −1 mm). `UI_BOARD_ORIGIN_MPOS_{X,Y}_UM` = MPos of the front left board corner, **measured** (ticket #40: anchor 1 − 15/15 mm = −206.580/−209.140 mm, DECISIONS.md; the board protrudes 3.14 mm in front of the Y minimum of the travel, map and 3D view show both), `UI_POS_CALIBRATED = true`; with `false` the map would show below it "Plattenlage geschätzt, noch nicht eingemessen" (board position estimated, not yet measured). Check: H-18 (d). For the 3D view additionally `UI_BOARD_TOP_MPOS_Z_UM` (board top surface in MPos Z, still estimated −102 mm = Z minimum, not covered by `UI_POS_CALIBRATED`, without hint on the page) and for the rotary axis module the flag `UI_A_POS_CALIBRATED = false` (position from CAD, not verified on the machine, check H-22). The module position has no MPos constants of its own since ticket #48: `a_axis_geometry.h` is relative to the board corner and the board top like `spoilboard_geometry.h`, so it follows `UI_BOARD_ORIGIN_MPOS_{X,Y}_UM` and `UI_BOARD_TOP_MPOS_Z_UM` (registration chain and comparison with `coordinate.rotation_offset` in DECISIONS.md, 2026-11-11, ticket #48).
  - `GET /a/three-0.160.0.js`, `GET /a/oc-<Hash>.js` (OrbitControls), `GET /a/v3d-<Hash>.js` (3D view): gzip-precompressed in the firmware image (`src/web_assets.h`, `.rodata` in flash, together 178,885 bytes), so that an OTA update (only `U_FLASH`, 9.5) brings them along; no file system. Response directly from flash via the same mechanism as the status page (`Conn::tail`, one `lwip_send` per iteration and connection, no heap), with `Content-Encoding: gzip` (regardless of `Accept-Encoding`), `Content-Type: text/javascript` and `Cache-Control: public, max-age=31536000, immutable`; for three.js the path contains the version (content bound to the npm file via SHA-256), for OrbitControls (rewritten import) and the 3D view the first 8 hex characters of the SHA-256 of the content. While the files load, both connection slots can be occupied for up to `WEB_ASSET_SEND_MAX_MS`; `/status.json` and a `POST /settings` then wait for a free slot (the page briefly shows "Keine Verbindung zum Handrad"). Irrelevant for safety: the web UI never talks to the machine, and `settings_allowed` is checked only in the iteration of the save. Generate: `python3 -I tools/make_web_assets.py` (after changes to `web/view3d.js`; checks the SHA-256 of the files in `web/vendor/` on every run), re-import three.js with `--three <entpacktes npm-Paket>` (unpacked npm package). The pipeline (job `web-assets`) regenerates and fails if `src/web_assets.h` or `src/web_asset_paths.h` differs.
  - `GET /status.json`: connection state, last error reason, RSSI, IP, machine state, WPos/MPos in mm (A in degrees, 4th value only with `has_a`; always from the µm values, also with `inch_mode`), age of the status, lock reason (texts from 10.1), axis selection, session active, battery (mV, %, no battery, warning), last alarm line, link statistics of the last 5 s window (like the serial statistics line, including loop duration).
  - `GET /settings` form with SSID, Wi-Fi password (never prefilled), machine IP, port, "Drehachse A vorhanden", "Update über WLAN erlauben" and the field "Handrad-Passwort" (handwheel password). Fields, validation and storage are the same as in the portal (`settings.cpp`, `settings_rules`), including the rule for the empty password field (DECISIONS.md).
  - `POST /settings` in this order: (1) locked after wrong password → 429. (2) field `auth` ≠ `ap_pass` → 403, lock for `WEB_AUTH_LOCK_MS = 2000`. (3) axis selected or session active (`settings_change_allowed`) → **409**, nothing saved. (4) invalid input → 400 with error text, nothing saved. (5) save, send response, close socket to the machine in an orderly way, wait `WEB_RESTART_FLUSH_MS`, `esp_restart()`. Steps 3 to 5 run in the same loop iteration; no tick can select an axis in between.
- **No motion commands:** The web UI sends nothing to the machine, neither `$J` nor real-time bytes. `webui.cpp` does not include `machine_link.h` and receives the state only via read-only hooks from `main.cpp` (snapshot, acceptance rule, restart).
- **Loop:** `webui::loop()` runs after tick and display, as the last step of `loop_normal()` before OTA (9.5). Order and content of the tick (8.7) are unchanged. The serial statistics line additionally reports the mean loop duration and the longest duration of a web step. **[MANUAL] H-12** (function) and **H-13** (loop duration before/after); loading the 3D files **H-19**.

### 9.2 AppMode and start

```cpp
enum class AppMode : uint8_t { Normal, Portal, Diagnose, Shutdown };

RTC_NOINIT_ATTR uint32_t rtc_next_mode_magic;  // survives esp_restart(), undefined after power-on
RTC_NOINIT_ATTR uint8_t  rtc_next_mode;        // valid only if rtc_next_mode_magic == NEXT_MODE_MAGIC; value NEXT_MODE_DIAG
RTC_NOINIT_ATTR uint32_t rtc_brownout_magic;   // brownout counter, valid only with NEXT_MODE_MAGIC
RTC_NOINIT_ATTR uint8_t  rtc_brownout_count;
```

- **On/off** (soft power, ticket #49): the latching button is an input (7.3). Off → deep sleep (9.3), on → wake-up, which is a reset: the start below runs as after power-on. There is no deep sleep in normal operation otherwise.
- **`RTC_NOINIT_ATTR`:** The diagnostics target from the web UI must survive an `esp_restart()`. `RTC_DATA_ATTR` is re-initialised in that case, `RTC_NOINIT_ATTR` is not.
- **Portal and diagnostics:** Planner and gate do not run, no `$J` ever goes out, and no TCP is established. The normal-operation web UI (9.1) and OTA (9.5) do not run there. The encoder supply is on (for diagnostics). The LEDs follow 9.4.

**Start** (after every power-on or reset), in this order:
0. **Board revision guard** (ticket #49, `lib/core/board_rev`, tests 113–114), before any output pin is configured: read NVS `board_rev`. Absent (first start, erased flash) → store the compiled revision `BOARD_REV`, serial `Platine Rev. n gespeichert`, go on. Equal → go on. Different (or an unreadable value) → **safe mode**: no output pin is configured (LED, wheel and on/off pins stay inputs as after reset, the wheel stays off by its gate pull-up), no Wi-Fi, no machine, no OTA (flash the right firmware over USB; the socket is reachable in the enclosure). Display "Falsche Firmware / Platine Rev. <NVS> / Firmware Rev. <compiled> / Platine gewechselt? / Board-Taste 3 s", serial every 5 s `Falsche Firmware: Platine Rev. <NVS>, Firmware Rev. <compiled>` and the hint. Holding the board button (GPIO27, the same on every revision) for `BOARD_REV_CONFIRM_MS = 3000` confirms a deliberate board change; counting starts only after the button has been seen released once in safe mode (a button held since power-on never confirms). Then store the compiled revision, `Platine Rev. n bestaetigt, Neustart`, display "Taste loslassen, dann Neustart", wait until the button is released (otherwise the next start would open the portal), restart. After `BOARD_REV_SAFE_SLEEP_MS = 600000` without confirmation: display off, deep sleep without wake source (reset or power cycle). Residual risk: the guard only knows what the previous firmware stored; it cannot see a FireBeetle moved to another board together with its old firmware.
1. LED pins as output "off", `PIN_ENC_PWR` as output HIGH (wheel off), initialise display. Read and clear the diagnostics target: `want_diag = (rtc_next_mode_magic == NEXT_MODE_MAGIC && rtc_next_mode == NEXT_MODE_DIAG)`, then `rtc_next_mode_magic = 0`.
2. Read buttons and board button (set `INPUT_PULLUP` for Z and the board button), debounced for `BOOT_DEBOUNCE_MS`. The state determined this way **initialises the debouncing**. A button held at power-on therefore produces no press edge and selects no axis.
   2a. **On/off button** (`PowerSwitch`, 9.3): sample until the level has been stable for `POWER_SW_DEBOUNCE_MS = 100`, at most `POWER_SW_BOOT_MAX_MS = 1000`. **Off** (USB plugged in, after flashing, watchdog or brownout reset, wake-up after a battery shutdown by switching off): serial `Ein/Aus: aus beim Start, Tiefschlaf` and deep sleep at once (9.3, reason `SwitchOff`), without Wi-Fi; the display is only initialised so that it can be switched off. No stable level within 1 s (noise): serial `Ein/Aus: kein stabiler Pegel, normaler Start`, start as with "on"; the loop keeps debouncing.
3. **Brownout:** If `esp_reset_reason() == ESP_RST_BROWNOUT`, the brownout counter is incremented (set it to 0 first if `rtc_brownout_magic ≠ NEXT_MODE_MAGIC`). For any other reset reason it is set to 0. `Shutdown` (9.3) follows only if **two brownouts in a row** have occurred or the start measurement in step 4 is below `BAT_WARN_MV`. Reason: a loose battery contact (pogo pins, J1) also produces brownouts, even with a full battery.
4. **Measure battery** (9.3, start measurement). If it is critical → `Shutdown`.
5. Encoder supply on (`PIN_ENC_PWR` LOW, then wait `ENC_POWER_SETTLE_MS`, then start PCNT).
6. **Diagnostics**, if `want_diag`. It takes precedence over the portal so that H-1, H-2, H-4 and H-6 can be checked even without stored credentials.
7. **Portal**, if X or the board button was pressed in step 2 or no credentials are stored.
8. **Otherwise Normal:** start screen 1 s, establish Wi-Fi. The axis selection is `None`.

**Leaving diagnostics:** On/off button off, then on again (deep sleep and wake-up, also on USB), or the reset button.


### 9.3 Battery (`battery.h`) and shutdown on empty battery

- **Divider:** According to the DFRobot forum, the FireBeetle 2 ESP32-E has a **1 MΩ/1 MΩ divider on GPIO34** from the factory. **[MANUAL] H-6:** Confirm for the UE variant (the diagnostics screen shows the mV). If it is missing, add an external 1M/1M divider with 100 nF.
- **Measurement:** `analogReadMilliVolts(PIN_BAT_ADC) × 2`, mean of `BAT_SAMPLES = 16` measurements, every `BAT_INTERVAL_MS = 5000` and once at start (`startup = true`). This applies in all AppModes except `Shutdown`.
- **No battery:** The battery is always connected; only without a battery in J1 (on USB) does the divider measure the charging circuit, without battery. Readings below `BAT_ABSENT_LOW_MV = 2800` or above `BAT_ABSENT_HIGH_MV = 4400` count as **"no battery"**: never critical, display `USB` instead of percent. This is acceptable because the protection circuit of a LiPo battery cuts off before 2.8 V.
- **Percent:** linear between `BAT_EMPTY_MV = 3300` (0 %) and `BAT_FULL_MV = 4150` (100 %), rounded to 5 % and clamped to 0–100. This is a rough approximation.
- **Warning symbol** below `BAT_WARN_MV = 3400`.
- **Critical** (`critical()`): three consecutive measurements below `BAT_CRIT_MV = 3300`; at start one is enough. The value stays set until the next start.
- **Shutdown** (`power::shutdown(reason)`), one path for both reasons: **`BatteryEmpty`** (triggered by `critical()` or brownout at start) and **`SwitchOff`** (on/off button off, soft power, ticket #49). The caller first ends the jog session (`planner.end_session`) and sets `AppMode::Shutdown`; serial `Akku leer, Tiefschlaf` or `Ein/Aus: aus, Tiefschlaf`. Then:
  1. Immediately stop sending commands, close socket in an orderly way (`machine::stop()`: `shutdown(SHUT_RDWR)`, then close), wait `POWER_OFF_FLUSH_MS = 150` so the FIN leaves, then Wi-Fi off (`esp_wifi_stop()`).
  2. `PIN_ENC_PWR` HIGH (wheel off), LEDs off. In deep sleep GPIO18 is high-impedance and the 100 kΩ gate pull-up keeps the AO3401 off; the pin is never held LOW (`gpio_hold`).
  3. Only `BatteryEmpty`: display "Akku leer – bitte ausschalten" (battery empty – please switch off) for 5 s. Then in both cases `setPowerSave(1)`.
  4. LED pins to LOW, then `rtc_gpio_isolate()` for the RTC-capable LED GPIOs (according to `pins.h` currently all: 12, 14, 15, 25), then under the hold an RTC pull-down (`hold_dis`, `pulldown_en`, `hold_en`): LEDs off, strapping pins LOW. Do **not** use `gpio_reset_pin()`; it enables pull-ups and causes leakage current.
  5. Only wake source: ext0 on `PIN_PWR_SW` (level triggered), RTC pull-up on, `RTC_PERIPH` powered. Wake level (`sleep_wake_when_on`, test 112): `SwitchOff` → button **on**; if it is already on again, the chip wakes at once and starts normally. `BatteryEmpty` → the **opposite** of the current level: with the button still on it waits for "off"; that wake-up finds the button off at boot (9.2 step 2a) and sleeps waiting for "on"; switching on then starts normally and measures the battery again (still critical → `Shutdown` again). So as before, an empty battery needs off and on to retry, and never restarts by itself. Serial `Ein/Aus: Tiefschlaf, Wecken mit Taste ein|aus`, then `esp_deep_sleep_start()`.

  The device then draws only the quiescent current (about 0.5 mA from the board, buttons open, wheel off; **[MANUAL] H-5:** measure, target below 1 mA). With an empty battery and the button still on, the RTC pull-up adds about 75 µA through the closed contact until it is switched off.

  Rationale: Without shutdown, Wi-Fi (up to ~250 mA) and handwheel (up to 50 mA) would deep-discharge the battery if the device is left switched on.
- **On/off button** (`PowerSwitch`, `lib/core/power_switch`, ticket #49): raw level of `PIN_PWR_SW` once per loop pass in every AppMode, before the battery measurement (closed = on, `POWER_SW_ACTIVE_LOW`). A level counts once it has been stable for `POWER_SW_DEBOUNCE_MS = 100` (polling only, like the buttons; contact bounce and short dips never switch off). The first debounced "off" (`turned_off`, tests 110, 111) → `Shutdown` with `SwitchOff`. The machine never moves because of switching: off ends the session before anything else and closes the socket; on is a normal start (axis `None`, no session). During an OTA transfer (9.5) the loop is blocked in `handle()`: the button is not read, the transfer ends (success or error) with the usual restart, and the start finds the button off and sleeps; the socket to the machine was already closed at `onStart`. Portal and diagnostics: off sleeps as well.
- **Residual risk (soft power):** "Off" is not a disconnect: the battery stays connected and the board draws about 0.5 mA (H-5), i.e. a 3000 mAh battery is empty after months. For storage unplug J1. The battery's own protection board (3.0 V cut-off) prevents a deep discharge.
- **[MANUAL] H-7:** On USB **without battery in J1**, read the measured voltage in the diagnostics screen. It must be outside 2800–4400 mV or above 3400 mV; otherwise adjust the limits in `core_config.h` and record this in `DECISIONS.md`.

### 9.4 LEDs

| LED | Normal | Portal | Diagnostics | Shutdown |
|---|---|---|---|---|
| X / Y / Z / A | **on:** axis selected and gate = None · **blinks 2 Hz:** axis selected, gate ≠ None · **off:** not selected | all blink 1 Hz (A only if enabled) | on while the button is pressed | off (also when switched off, 9.3) |

### 9.5 OTA update over Wi-Fi (ticket #35)

Flash firmware over Wi-Fi with ArduinoOTA (espota). Partition table `default_16MB.csv` unchanged (`app0`/`app1` 6.25 MB each, firmware about 1.3 MB).

- **Enabling:** Only with the checkbox "Update über WLAN erlauben" (NVS `ota`, default off) in the portal or under `/settings`. If it is off, ArduinoOTA is not started, port 3232 stays closed.
- **Password:** the handwheel password `ap_pass` (9.1), no second secret. ArduinoOTA (Arduino-ESP32 3.3.x) stores only its SHA-256 and checks via PBKDF2-HMAC-SHA256 challenge (10,000 rounds); the password never goes over the network in plain text. The check blocks the loop once per attempt (duration **[MANUAL] H-17**, serial `OTA: handle() dauerte … ms` (OTA: handle() took … ms) from `OTA_SLOW_HANDLE_MS`). **Lock:** After a wrong password (`OTA_AUTH_ERROR`) the UDP port stays closed for `OTA_AUTH_LOCK_MS = 5000` (`AuthThrottle(OTA_AUTH_LOCK_MS)`, test 91; the acceptance rule counts as violated for that long, closing and opening as in step 2 below). So a client on the LAN can stall the loop with a check at most once per 5 s. espota's automatic second attempt (MD5 password hash) then gets no response.
- **Start:** Only in normal operation, as soon as the web UI has started (Wi-Fi connected). Never in portal and diagnostics mode.
- **mDNS:** The web UI owns the responder (`MDNS.begin("handrad")`). ArduinoOTA runs with `setMdnsEnabled(false)` (its own `MDNS.begin()` would call `mdns_init()` a second time and fail); the service `_arduino._tcp` (port 3232, `auth_upload=yes`) is registered with the existing responder via `MDNS.enableArduino()`. Flash via `handrad.local` or the IP.
- **Acceptance rule** (`ota_update_allowed`, `lib/core/settings_rules`): no axis selected, no session (`settings_change_allowed`, 9.1) **and** `!battery.warn()` (a brownout in the middle of writing wastes the update).
- **No tick between check and transfer.** ArduinoOTA handles invitation (UDP), password check and transfer in different `handle()` calls; the transfer (`_runUpdate`, TCP) blocks inside `handle()` until the end. Therefore:
  1. `ota::step(ota_allowed())` is the **last step** of `loop_normal()`, after tick, display and web UI. The rule is evaluated directly before it, i.e. with `axis` and session after this iteration's tick.
  2. If it does not hold, ArduinoOTA is stopped (`ArduinoOTA.end()`: UDP port closed, state back to `IDLE`). An invitation then stays unanswered (espota aborts), and a half-finished authentication from an earlier iteration does not survive. When it holds again, the port is reopened. `handle()` runs only in iterations in which the rule holds. `ArduinoOTA.begin()` does not report a failed port bind; therefore `ota.cpp` first test-binds the same port (same call as the library). If that fails, the port stays closed, new attempt at most every `OTA_LISTEN_RETRY_MS = 5000`, serial once `OTA: UDP-Port 3232 nicht geoeffnet …` (OTA: UDP port 3232 not opened …) and after success `OTA: UDP-Port 3232 offen` (OTA: UDP port 3232 open).
  3. `onStart` (after `Update.begin()`, before the transfer) checks the rule once more. If it does not hold (must not happen), restart without transfer.
  4. Firmware only (`U_FLASH`); a file system update is rejected in `onStart` with a restart.
- **During the transfer:** `onStart` ends the session and closes the socket to the machine in an orderly way (`machine::stop()`, after which every `send_line()` fails). The loop sits in `handle()`, no tick runs, hence no `$J`, no axis selection, no web UI. Display "Update" with progress in percent (only on change, at most `DISPLAY_MAX_HZ`; switches a display turned off by the screensaver back on), serial `OTA: Update startet …` (OTA: update starting …), `OTA: <n> %` per 10 %, `OTA: Fehler (…)` (OTA: error (…)).
- **End:** `setRebootOnSuccess(false)`; after returning from `handle()` a restart **always** follows once `onStart` has run: after success with "Fertig, Neustart" (done, restarting) into the new firmware, after error or abort with "Fehlgeschlagen" (failed) (safe initial state). Without a successful `Update.end()` (MD5 checked) the boot partition is not switched, the old firmware starts again. Errors before `onStart` (wrong password, `Update.begin` fails) change nothing and do not restart.
- **Battery, screensaver, watchdog:** During the transfer the battery is not measured (loop blocked); therefore the acceptance rule accepts no update with `warn()`. If the voltage drops anyway, the brownout detector resets and the old firmware starts. The screensaver does not run (no tick), `show_ota` switches the display on. The loop task is not registered with the task watchdog (Arduino default `loopTaskWDTEnabled = false`); only the idle task of CPU 0 is checked; the loop runs on CPU 1 and yields in `_runUpdate` via `delay(1)`. No watchdog intervention of our own.
- **Rollback:** Arduino-ESP32 marks the new firmware as valid immediately at start (`verifyRollbackLater()` = false). There is no automatic fallback if the new firmware later fails to join Wi-Fi; then only USB helps.
- **Flashing** (computer on the same network, handwheel in normal operation, no axis selected): `HANDRAD_OTA_PASS=<Handrad-Passwort> pio run -e firebeetle-ota -t upload`, or with IP: `… -t upload --upload-port <IP>`. The password comes only from the environment, never into the repo.

## 10. User interface (display 128×64)

The display is updated at most at 5 Hz. A frame takes about 23 ms over I2C (1024 B × 9 bit / 400 kHz). The tick logic therefore depends on `now_ms − last_tick_ms ≥ TICK_MS`, not on fixed iterations. A lengthened tick does no harm, because the encoder delivers the difference.

### 10.1 Main screen

```
┌────────────────────────────┐
│ XY  0.05     ▂▄▆  [███ 80%]│  axis mode, step size (level), Wi-Fi signal, battery
│ X  123.456                 │  large font: WPos of the active axis/axes
│ Y   45.010                 │  Z: Z only · A: A only (degrees) · None: X, Y, Z small (+ A, if enabled)
│ Bereit                  mm │  state/lock reason · unit
└────────────────────────────┘
```

- **Unit:** With `inch_mode` it shows `in`, the values have 4 decimal places (µm / 25,400). With axis A selected it shows `deg`, the value has 3 decimal places.
- **Lock reason texts:**

  | BlockReason | Text |
  |---|---|
  | None | `Bereit` (ready) (with active session `Fährt` (moving)) |
  | BatteryLow | `Akku leer` (battery empty) |
  | NoWifi | `WLAN…` (Wi-Fi…) |
  | NoMachine | `Maschine…` (machine…) |
  | NoData | `Keine Daten` (no data) |
  | AxisNone | `Achse wählen` (select axis) |
  | NoAxisA | `Keine A-Achse` (no A axis) |
  | JobRunning | `Job läuft` (job running) |
  | Alarm | `Alarm` |
  | NoRates | `Raten unbekannt` (rates unknown) (also in the web UI as lock reason) |
  | MachineBusy | `Maschine belegt` (machine busy) |

- If `clipped = true` occurred within the last 500 ms, it shows `Begrenzt` (limited). An alarm or error line (8.8) takes precedence for 2 s.
- **Unlock** (8.10, ticket #41), precedence from bottom to top: lock reason or `Begrenzt` → `Entsperren: Taste 5 s` (unlock: hold button 5 s) (as long as unlocking is possible; replaces the lock reason, which in alarm without axis would otherwise read `Achse wählen`) → alarm/error line (2 s) → `Entsperren` (unlock) with progress bar to the right edge (while holding; just short of full while waiting for a fresh status) or `Entsperren...` (unlocking...) (after sending). These lines use all 21 characters; the unit is then omitted (nothing moves in alarm).

### 10.2 Other screens

- **Start:** name and firmware version (`FW_VERSION` in `core_config.h`, format `"0.1.0"`), 1 s
- **Portal:** SSID, password, `192.168.4.1`
- **Battery empty:** "Akku leer – bitte ausschalten", see 9.3
- **Switched off** (soft power): no text, the display goes dark at once (9.3)
- **Update:** "Update", status line and progress bar in percent, see 9.5

### 10.3 Diagnostics screen

Reachable only via the portal (9.1). It shows:
- the accumulated PCNT count after `ENC_INVERT`,
- the four buttons X, Y, Z, A (raw and debounced),
- the board button,
- the battery voltage in mV,
- the GPIO level of `PIN_ENC_A` and `PIN_ENC_B`.

It serves the checks H-1, H-2, H-4 and H-6. There is no network connection to the machine.

### 10.4 Screensaver (ticket #28)

Against OLED burn-in, only in normal operation. State logic in `lib/core/screensaver` (`ScreenSaver::feed(now, press, counts, busy, attention)` → `On`, `Saver` or `Off` as well as `swallow`, `woke` and `move`), called once per tick before step 0 (8.7).

- **Input** = press edge of any button (X, Y, Z, A, board) or encoder counts. In `On` every `counts ≠ 0` counts. While asleep (`Saver`/`Off`) the wheel wakes only once the sum of the counts reaches `|Σ counts| ≥ WAKE_MIN_COUNTS = COUNTS_PER_DETENT / 2` (half a detent); the sum restarts after `STEP_RESET_MS` without counts. A single glitch pulse or ±1 jitter therefore does not wake. Input and attention restart the idle timer.
- **`On`:** main screen (10.1). After `SCREENSAVER_MS = 60000` without input → **`Saver`:** saver image (1-bit, 40 × 32 px, see "Logo" below) at a random position, rest black; new position every `SAVER_MOVE_MS = 5000`, no frame redraw in between. After `DISPLAY_OFF_MS = 600000` without input → **`Off`:** `setPowerSave(1)` (DISPLAYOFF). `Off` stays `Off` until the next input or attention, also across the overflow of `millis()`.
- **Never `Saver`/`Off` while `busy`:** axis selected (`axis` of the previous tick ≠ None) or session active. `busy` only holds the display; the timer keeps running from the last input. After the auto-deselection (8.4, also 60 s) the screensaver therefore starts immediately.
- **Waking swallows the input:** While the display is asleep, in the wake tick and until `WAKE_SWALLOW_MS = 150` afterwards, `feed` returns `swallow`: edges and `counts` are discarded (`accumulator.clear()`). No axis change, no jog step; even someone who wakes with two buttons (second edge a few ms later) deterministically achieves nothing. Because the screensaver starts only with `axis == None` and without session, the gate (`AxisNone`) locks anyway; swallowing mainly prevents the waking press from selecting an axis. Afterwards inputs act normally. With `busy` nothing is ever swallowed.
- **Attention** wakes without a wake window (there was no input; the next input acts normally): MachineLink leaves `Online`, battery warning (`warn()` becomes true), new alarm/error line (8.8), machine status becomes `Alarm` or `halted` (rising edge of `have_status && (state == Alarm || halted)`, observed directly on the status: in the screensaver `axis == None`, so the gate reports `AxisNone` and never `Alarm`). `Shutdown` switches the display back on itself for "Akku leer" (9.3).
- **Unlock** (8.10): Holding starts only with an edge the screensaver has not swallowed. Whoever wakes a sleeping display in alarm with a button never unlocks with it; only a new press with the hint visible counts.
- **Serial** (operation without display): `Anzeige: Bildschirmschoner` (display: screensaver), `Anzeige: aus` (display: off), `Anzeige: an` (display: on), `Anzeige: geweckt, Eingabe verworfen` (display: woken, input discarded).
- **Logo (tickets #45, #50):** The Makera logo is a trademark of Makera, and this project has no affiliation with Makera. `src/logo_bitmap.h` is committed in the private GitLab repository only; the public export (`tools/strip_private.py`) leaves it out. Without the file the screensaver shows the neutral "MPG" from `src/logo_fallback.h` (own pixel drawing, same size, same movement). Anyone who wants the logo for private use in the public version generates it locally from their own source image (PNG, white background, Pillow required): `python3 -I tools/logo_to_xbm.py <logo.png> src/logo_bitmap.h [vorschau.png]` and does a clean rebuild (`pio run -e firebeetle -t clean && pio run -e firebeetle`; without `clean` the build does not notice the newly created file). `display.cpp` picks the file up automatically via `__has_include`; do not distribute the file or firmware built this way. The same applies to the enclosure engraving: `hardware/enclosure/README.md` (DECISIONS.md, 2026-11-11).

## 11. Tests (`pio test -e native`)

Each case is its own Unity test (one test function). Positions are compared in µm, i.e. exactly; float appears only in parsing. The placeholder test from step 0 is deleted. The Arduino adapters in `src/` (including `LinkState`, portal, sleep) are not tested natively. The [MANUAL] items apply to them.

**Frame / TextLines**
1. `crc16("123456789") == 0x31C3`
2. `build(0x90, "model = Z1, 4, 1, 0, Idle\n")` yields exactly the test vector from 8.1.
3. `build(0xA1, "?")`: len field = 4, valid CRC. `build` with n = 514 → returns 0.
4. Two frames fed byte by byte, preceded by garbage → exactly 2 frames.
5. Two frames in a single `feed` → 2 frames.
6. Corrupted CRC → discarded, the following valid frame is recognised.
7. Wrong footer → discarded. len field = 2 → discarded. len field = 604 (= `MAX_RX_PAYLOAD` + 4) → discarded. len field = 603 with valid CRC → accepted, n = 600. In all three error cases a subsequent valid frame is recognised.
8. TextLines: `"ALARM: Hard"` + `"limit\n"` in two feeds → one line `"ALARM: Hardlimit"`.
9. TextLines: line with 300 characters + `\n` → one line with exactly 256 characters.

**Status**
10. Real report from 8.1 → Idle, `mpos_um = {-1000,-1000,-1000}`, `wpos_um = {190540,193490,73164}`, `inch_mode = false`, `playing = false`, `halted = false`.
11. `<Run|MPos:1,2,3,0,0|WPos:1,2,3,0,0|P:120,42,305|C:4,1,0,1>` without `\n` → `playing = true`, `mpos_um[0] == 1000`.
12. `<Alarm|MPos:0,0,0,0,0|WPos:0,0,0,0,0|H:3|C:4,1,0,1>` → `halted = true`.
13. `<Idle|MPos:1.0000,0,0,0,0|WPos:0,0,0,0,0|C:4,1,1,1>` → `inch_mode = true`, `mpos_um[0] == 25400`.
14. Report without `C:` → `false`. `ok`, empty line, `<Idle` → `false`, no crash.

**AxisSelect**
15. AxisSelector, all 15 transitions of table 8.4 individually (initial state × pressed button).
16. AxisSelector auto-deselect: `on_press(X, 0)`. `mode(59999)` → X, `mode(60000)` → None. Second run: `on_press(X, 0)`, `on_activity(30000)`, then `mode(89999)` → X and `mode(90000)` → None. Third run: start at `0xFFFFF000` (overflow) → same results. Fourth run: `on_press(X, 0)`, then without a `mode()` call `on_press(X, 60000)` → `mode(60000)` returns X (auto-deselect before toggling).
17. AxisSelector: after `clear()`, `mode()` returns None, and the next press edge X yields X.

**Gate**
18. Each rule 1–10 individually.
19. Order: `playing` and `axis == None` at the same time → `AxisNone`.
20. `status_age_ms = 250` → not `NoData`. 251 → `NoData`.
21. Run without session → `MachineBusy`. Run with session, consistent → None. Run with session, inconsistent → `MachineBusy`. Hold with session → `MachineBusy`.

**StepPolicy**
22. `level_for`: (2,0)→0 · (6,0)→0 · (8,0)→1 · (20,0)→2 · (20,1)→2 · (14,2)→2 · (11,2)→1 · (6,2)→1 · (3,2)→0 · (3,1)→0 · (5,1)→1
23. `update` on a fresh instance: `update(0,0)` → 10. Then `update(20,1)`, `update(40,1)`, `update(60,1)` → 10 each (v = 2, 4, 6). `update(80,1)` → 50 (v = 8, level 1).
24. Fresh instance: `update(0,10)` → 50 (v = 20, target 2, but only +1). `update(20,1)` → 100 (v = 22, level 2).
25. Fresh instance, set up as in test 24 up to level 2 (last detent at t = 20). `update(319,0)` → 100. `update(320,0)` → 10 (`now − t_last = 300`). Then `update(340,1)` → 10 (v = 2, the ring buffer was emptied).
26. As test 23 and test 25, but all timestamps shifted by `0xFFFFFFE0` (overflow between the 2nd and 3rd call) → identical results.

**DetentAccumulator** (`counts_per_detent = 4`)
27. `take(0,+6)` → 1, `remainder() == 2`. `take(20,0)` … `take(280,0)` → 0. `take(300,+2)` → 0, `remainder() == 2` (remainder was discarded before adding; without the reset it would return 1 with remainder 0). Separately: `take(0,+6)`, then `take(20,+2)` → 1, `remainder() == 0`.
28. `take(0,−6)` → −1, `remainder() == −2` (division rounds toward 0).

**JogPlanner** (status fresh, age 0, Idle, `mpos = {0,0,0}`, gate None, Z1 Pro rates `{50000, 50000, 16667, 60000}` (test values, 8.8), unless stated otherwise; time starts at 0, +20 per tick)
29. Gate ≠ None → no command, `session_active() == false`.
30. X, detents = +3, step = 10 → `$J X0.030 F0.008` (slow: lower bound 0.4 mm/s), `dir() == +1`, `target_um(0) == 30`.
31. detents = −3 → `$J X-0.030 F0.008`, `dir() == −1`.
32. XY, +3, step 10 → `$J X0.030 Y0.030 F0.008`. Z, +3, step 10 → `$J Z0.030 F0.012`.
33. Session start with status 151 ms old → no command, no session. 150 ms → command.
34. Continuation: command as in test 30, then mpos = 30. Tick at t = 200 (status at least `START_GAP_MS` after the block, block finished, nothing queued) with +3 → immediately `$J X0.030 F0.008`, `target_um(0) == 60`.
35. **Window holds with frozen mpos:** mpos stays 0, +10 detents per tick (t = 0, 20, …) with step 100 (`F = ⌈0.092 × µm in 200 ms⌉`). t = 0 `$J X1.000 F0.092` (immediately). t = 20, 40 no command (bundled). t = 60 `$J X3.000 F0.368`, not `clipped`. t = 80, 100 no command. t = 120 `$J X1.000 F0.400` (only 1000 left free, `clipped`, F capped). t = 140, 160 no command, t = 180 no command and `clipped`. `target_um(0) == 5000`.
36. Window: set up as in test 35 up to t = 120 (`target = 5000`), mpos = 50. t = 140 +1 × 100 → no command (bundled), t = 160 nothing, t = 180 → `$J X0.050 F0.400`, `clipped`. Then (`target − mpos = 5000`) t = 200 +1, t = 240 → no command, `clipped`.
37. Window negative: set up as in test 35 with −10 (`target = −5000`), mpos = −10, t = 140 −10 × 10, t = 180 → `$J X-0.010 F0.400`.
38. XY, set up as in test 35 (`target` X and Y 5000), mpos = (40, 100): t = 140 +10 × 10, t = 180 → X allows 40, Y allows 100 → `$J X0.040 Y0.040 F0.400`, `clipped`.
39. Direction reversal: t = 0 +5 × 100 → `$J X0.500 F0.046` (`dir = +1`). t = 20 −3 × 10 → no command, `clipped`, `dir() == +1`. mpos = 490 (`target − mpos = 10`), t = 40 −3 × 10 → no command (the last block may still be queued, bundled), `dir() == −1`. t = 80 → `$J X-0.030 F0.052`.
40. Session end: last command at t, mpos = target. Tick at t + 999 → still active. Tick at t + 1000 → ended.
41. Session end at t + 3000, even though mpos never reaches the target (`target − mpos = 30`).
42. `notify_send_failed()` directly after test 30 → `target_um(0) == 0`, `session_active() == false`.
43. **Axis change within the session:** session with `axis = X` running (test 30). Next tick with `axis = XY`, detents +3 → no command, session ended.
44. At most one command per tick: detents = +100, step 10 → exactly one command `$J X1.000 F0.092`.
45. **No restart on old status** (all ticks with detents +3, step 10): session running (test 30 at t = 0), then `end_session(40)`. Tick at t = 60 with status Idle, age 50 (i.e. received before the end) → no command, no session. Tick at t = 300 with status Idle, age 0 → new session, command (`F0.020`).
46. **Boundary `START_GAP_MS`:** as test 45 up to `end_session(40)`. Tick at t = 260 with age 21 → no command (221 > 220). New instance, same sequence, tick at t = 260 with age 20 → command (220 ≤ 220).
47. **No permanent lock without session:** fresh instance, ten ticks (t = 0 … 180) with detents 0, then tick at t = 200 with detents +3 and age 0 → command.
48. `session_consistent`: session active with `target_um(0) == 5000`. mpos −100 (distance 5100) → true. mpos −101 (distance 5101) → false. Without session → false.

**fixed_fmt / Battery**
49. `format_um`: 0 → `"0.000"`, 5 → `"0.005"`, −5 → `"-0.005"`, 123456 → `"123.456"`, −2000 → `"-2.000"`.
50. `format_inch_e4`: 25400 → `"1.0000"`, −1270 → `"-0.0500"`, 127 → `"0.0050"`, 1 → `"0.0000"`, −1 → `"0.0000"`.
51. Battery: percent for 3300 → 0, 4150 → 100, 3725 → 50, 5000 → 100, 3000 → 0. `critical()` after three readings < 3300 (not start) → true, after two → false. One start reading < 3300 → true. `warn()` at 3399 → true, at 3400 → false. `absent()` at 2799 and 4401 → true, `critical()` stays false (also at start). At 2800 and 4400 → `absent()` false.

**Holding on NoData** (ticket #19; rig as in tests 29–48)
57. Session running (test 30, t = 0). Tick t = 20 with `gate = NoData`, age 300, detents +3 → no command, `session_active()`, `target_um(0) == 30`. Tick t = 40 with gate None, age 0, detents +3 → no command (a1: 0 + 200 > 20), session active. t = 219 → no command. mpos = 30 (status 220 ms after the block: finished, nothing queued), tick t = 220 with gate None, age 0, detents +3 → `$J X0.030 F0.008`, `target_um(0) == 60`.
58. As 57, but age 1000 → session stays held; age 1001 → session ended. With `status == nullptr` and `gate = NoData` → session ended.
59. Holding does not change other lock reasons: running session, `gate = MachineBusy` → ended. Axis change during NoData → ended.
60. Holding ends via (g): session with `target − mpos = 30`, then only NoData ticks with age ≤ 1000 (status is artificially kept young) → ended at t = last_send + 3000.

**Axis A** (ticket #14)
52. Status with 5 values (`MPos:1,2,3,45.5,0`) → `has_a = true`, `mpos_um[3] == 45500`. Same report with `C:4,1,1,1` (inch) → `mpos_um[3] == 45500` (no conversion). Report with only 3 values → `has_a = false`, `mpos_um[3] == 0`.
53. AxisSelector: button A from all 6 states (table 8.4) as well as X, Y, Z from state A.
54. Gate: `axis == A`, `has_a = false` → `NoAxisA`. With `has_a = true` → None. Order: `axis == None` and `has_a = false` → `AxisNone`.
55. Planner A: +3 detents, step 10 → `$J A0.030 F0.020`, `target_um(3) == 30`. Window A: mpos A frozen, +10 × 100 per tick → t = 0 `$J A1.000 F0.077`, t = 20/40 no command, t = 60 `$J A1.000 F0.307` (`clipped`), t = 120 no command and `clipped`, `target_um(3) == 2000`.
56. Planner: X session with `has_a = false` and arbitrary `mpos_um[3]` → A counts neither for `session_consistent` nor for ARRIVE.

**Web UI** (ticket #17)
61. `parse_http_head`: without empty line → `Incomplete`. `GET /status.json?t=1` → Get, path without query. POST with `content-LENGTH: 23` → 23, `head_len` up to after the empty line. `PUT` → Other. `Content-Length: 12a`, duplicate `Content-Length`, path with 32 characters, target without `/` → `Bad`. Path with 31 characters → Ok.
62. `form_value`: `My+Net%21` → `"My Net!"`. `pass` is not confused with `pass2`. Key without `=` → length 0. Missing key → −1. `%4`, `%zz`, `%00`, value too long → −2. Exactly fitting value → accepted. Search ends at `n`.
63. `valid_ipv4` (bounds 0/255, 3 and 5 parts, trailing dot, empty parts, 4 digits, spaces, empty, nullptr) and `parse_port` (empty → default, 1, 65535, 0, 65536, 6 digits, letters, sign).
64. `validate_settings`: valid with default port and with custom port, SSID empty / 33 / 32 characters, password 64 / 63 characters, host not IPv4, port 0.
65. `settings_change_allowed`: only `axis == None` and no session → true; every selected axis (X, Y, XY, Z, A) with and without session → false; None with session → false.
66. `secret_equals` (equal, one character different, shorter, longer, empty, nullptr) and `AuthThrottle` (fresh: not locked; after a failed attempt at t locked until t + 1999, free from t + 2000; the same across the overflow).
67. `format_status_json`: fields and escaping (`"` and line break in `last_error`), WPos/MPos with 3 or 4 values depending on `has_a`, `rssi: null` without Wi-Fi, no positions without status, `stats: null` without statistics, buffer too small → 0, never a truncated object. Worst case (error reason with 47 and alarm line with 21 control characters, longest IP, all 8 positions including A at `INT32_MIN`, all counters at maximum) fits in `WEB_JSON_MAX`.

**Screensaver** (ticket #28)
68. Timing: input at t0 → `On` until t0 + 59999, `Saver` from t0 + 60000 (`move`), next `move` only at +5000, `Saver` until t0 + 599999, `Off` from t0 + 600000. Same with t0 shortly before the overflow (between input and `Saver`, or between `Saver` and `Off`). `Off` stays `Off` when the idle time overflows. Fresh instance: timer starts at the first `feed`. Over real ticks exactly one `move` per 5 s. In `On` a single count is input.
69. `busy` for 2 × `DISPLAY_OFF_MS` → always `On`, no `move`, no `swallow`; afterwards immediately `Off` (timer ran from the last input). Input while `busy` is never swallowed. `busy` in `Saver` wakes without swallowing.
70. Press in `Saver` and in `Off` → `On`, `woke`, `swallow`; a second press 20 ms later and counts at +149 ms → `swallow`; from +150 ms no longer, timer restarted. Input in `On` → never `swallow`. Input and attention at the same time in `Saver` → `woke`, wake window open.
71. Attention in `Saver` and `Off` → `On` without `woke`, the following input is not swallowed. Attention in `On` extends by `SCREENSAVER_MS`.
72. From `Off`, one rotation (4 counts per tick) over ten ticks → `woke` exactly once, `swallow` in the 8 ticks within 150 ms.
73. Noise: asleep +1 → no wake, `swallow`; −1, +1 → no wake; another +1 (sum 2 within `STEP_RESET_MS`) → wakes. Single counts spaced `STEP_RESET_MS` apart never wake, `Saver` goes to `Off`. −2 in one tick wakes. A count before falling asleep does not count.
74. Asleep without input → `swallow` (a count below the threshold does not reach the accumulator). With `busy` → no `swallow`.
75. **Ticket #29:** The request from curl (`GET / HTTP/1.1`, `Host`, `User-Agent: curl/8.7.1`, `Accept: */*`, no body) and the same for `/status.json` → Ok, Get, correct path, `head_len` = whole request, length 0. `web_conn_expired`: same age → no, +1999 → no, +2000 → yes; timestamp 3 ms **after** `now` → no (the old unsigned comparison overflowed here and closed); the same across the overflow.

**Feed, bundling and window** (ticket #32)
76. `feed_milli` with the Z1 Pro rates (X/Y: `⌈v × 92 / 5000⌉`, bounds `[8, 400]`; Z `⌈v × 920 / 16667⌉`, `[12, 299]`; A at 60 °/s `⌈v × 92 / 6000⌉`, `[20, 500]`): X v = 0 and 1 → 8 (lower bound). X 434 → 8, X 435 → 9. X 3100 → 58. X 10000 → 184, Y 10000 → 184. X 21684 → 399, X 21685 → 400, X 21740 → 400 (cap, 20 mm/s). XY 10000 → 261 (×√2), XY 15335 → 399, XY 15336 → 400. Z 217 → 12, Z 218 → 13, Z 2000 → 111, Z 5398 → 298, Z 5399 → 299 (cap). A 1304 → 20, A 1305 → 21, A 10000 → 154, A 32543 → 499, A 32544 → 500. XY, Z, A with `UINT32_MAX` → 400, 299, 500 (no overflow). `None` → 0 (no F).
77. `FeedEstimator`: `update(0,1000)` → 5000. `update(20,0)` and `update(199,0)` → 5000, `update(200,0)` → 0. `update(300,100)` → 500, `update(400,100)` → 1000, `update(500,0)` → 500. After `reset()` → 0. The same from `0xFFFFFF80` (overflow). 20 entries of 100 each spaced 10 ms apart → 8000 (only 16 entries, estimate too small, never too large). Twice `UINT32_MAX` → `UINT32_MAX` (saturation).
78. Window Z: +10 × 100 per tick → t = 0 `$J Z1.000 F0.276`, t = 20/40/60 no command, at t = 60 `clipped`, `target_um(2) == 1000`. XY: +10 × 100 → `$J X1.000 Y1.000 F0.131`.
79. `F` follows the turning speed: +10 × 100 at t = 0 → `F0.092`; mpos = 1000, +1 × 10 at t = 400 → `$J X0.010 F0.008`. Detents discarded by the direction lock count toward the turning speed: +10 × 100, −10 × 100 (`clipped`), mpos = 1000, t = 219 +10 × 100 → `$J X1.000 F0.184` (2000 µm in 200 ms).
80. Bundling, mpos stays 0: t = 0 +3 × 10 → immediately `$J X0.030 F0.008`. t = 20, 40 +3 → no command. t = 60 +3 → `$J X0.090 F0.012`, `target_um(0) == 120`. Release: t = 80 +3, t = 100 nothing, t = 120 → `$J X0.030 F0.014`, `target_um(0) == 150`.
81. Collected detents are never sent after: gate lock (t = 0 command, t = 20 +3, t = 40 `MachineBusy` → session ended, t = 60 no command, `target_um(0) == 30`); axis change (t = 40 XY → no command, session ended); `end_session(30)` (t = 40, 60 no command); holding (t = 40 NoData, then until t = 300 gate None without detents → no command, session active, `target_um(0) == 30`); reversal (t = 40 −3 → no command, `clipped`, t = 60/80 no command, `target_um(0) == 30`, `dir() == +1`).
82. Upper limit e2: mpos stays 0, from t = 0 +2 × 10 per tick until t = 1200 → exactly 12 commands, the last at t = 660, none afterwards, `target_um(0) == 680`. mpos = 20 (first block finished), t = 1220 +2 → `$J X0.560 F0.019` (everything since t = 680).
83. New session with empty turning speed: t = 0 +10 × 100 → `F0.092`, `end_session(20)`, t = 240 +1 × 10 → `$J X0.010 F0.008`. Axis change: X session, t = 20 Y → no command, t = 240 Y +1 × 10 → `$J Y0.010 F0.008`.
84. Count e2 with moving mpos and reversal: +3 × 10 at t = 0 … 60 (ends 30, 120), mpos = 120, t = 300 −3 → immediately `$J X-0.030 F0.008`; t = 320, 340 no command, t = 360 `$J X-0.090 F0.012` (end 0); mpos = 0, t = 600 −1 → immediately `$J X-0.010 F0.008` (no phantoms of the old direction). Second case: +46 × 10 (end 460), mpos = 450, t = 300 −1 → `$J X-0.010 F0.008` (end 450); t = 320 with age 20, −1 → no command (the new block counts even though mpos is at its end).
85. New session after a full list: mpos 0, +2 × 10 per tick until t = 700 → 12 commands, `end_session(720)`, new session from t = 940 to t = 2000 → again exactly 12 commands; `2 × (JOG_MAX_QUEUED + 2) < 32`.
86. `notify_send_failed` after a bundled command: test 80 up to t = 60 (`$J X0.090 F0.012`, target 120), then `notify_send_failed()` → `target_um(0) == 30`, session ended.
87. Remainder after a pause: t = 0 +3 (target 30), mpos = 27, t = 1000 +1 → immediately `$J X0.010 F0.008`, session active, `target_um(0) == 40`.
88. Collected remainder without status: t = 0 command, t = 20 +3, t = 40 `status == nullptr` (NoData) → no command, session ended, t = 60 no command, `target_um(0) == 30`. Likewise with NoData and age `NODATA_HOLD_MS + 1`.
89. Bound of count e2: twelve blocks of 10 µm each (ends 10 … 120, every 60 ms, mpos stays 0) → `queued() == 11` at t = 700 (block 1 with 10 µm remaining counts as finished); status mpos = 10 at t = 720 → `queued() == 10` (exactly one too few: 11 queued). Window full except 5 µm (set up as test 35, mpos = 5): +1 × 100 → no command, `clipped`, `target_um(0) == 5000` (block below `ARRIVE_UM` discarded).

**OTA** (ticket #35)
90. `ota_update_allowed`: only `axis == None`, no session and no `battery_warn` → true; None with session, None with warning → false; every selected axis (X, Y, XY, Z, A) in all combinations of session and warning → false. Without warning equal to `settings_change_allowed` for all axes and sessions.
91. `AuthThrottle(OTA_AUTH_LOCK_MS)`: fresh: not locked; after a failed attempt at t locked until t + 4999 (also beyond `WEB_AUTH_LOCK_MS`), free from t + 5000; a new failed attempt restarts the lock; the same across the overflow. Without argument the lock stays `WEB_AUTH_LOCK_MS`.

**Static files of the web UI** (ticket #37)
92. `web_send_expired`: pages/JSON (`max_ms = WEB_REQ_TIMEOUT_MS`): +1999 → no, +2000 → yes, even if something was still sent at +1500. File (`WEB_ASSET_SEND_MAX_MS`): last progress 1999 ms ago → no, 2000 ms ago → yes (even without any byte sent); with steady progress +29999 → no, +30000 → yes. Timestamp 3 ms after `now` (#29) → no. The same across the overflow of `millis()`.

**Machine rates** (ticket #38)
93. `feed_limits`: Z1 Pro rates (test values, A 60 °/s) → X, Y, XY `[8, 400]`, Z `[12, 299]`, A `[20, 500]`. The rates assumed by #32 `{20000, 20000, 10000, 60000}` → exactly the old bounds `[20, 1000]`, Z and A `[20, 500]`. `RATE_MAX` → X `[3, 119]`, Z `[2, 29]`, A `[2, 50]`. `RATE_MIN` → X `[400, 1000]`, Z `[200, 1000]`, A `[1000, 1000]` (lower bound clamped to the cap). XY with X 50000, Y 30000 → `[14, 666]`, v 10000 → 434. Over the whole plausible range of each axis: `1 ≤ lo ≤ cap ≤ 1000`, `cap × rate ≤ JOG_MAX_SPEED × 1000`, `lo × rate ≥ JOG_MIN_SPEED × 1000` (unless clamped), `feed_milli(UINT32_MAX) == cap`, `feed_milli(0) == lo`. Without rates: `feed_limits` false (0, 0), `feed_milli` 0 for X, XY, Z; without A: A 0, X 184. `rates_cover`: None → false, A without `a` → false.
94. `parse_rate_line`: ` X: 50  Y: 50  Z: 16.6667  A: 60  B: 1.66667 ` → Valid `{50000, 50000, 16667, 60000}`, `a`. Without A → Valid, `a == false`. Without spaces, `16.6661` → 16667, `16.666` → 16666. Bounds `X: 1`, `Y: 166.667`, `Z: 0.9991` (→ 1000), `A: 600` → Valid. `NotRates` (out unchanged): `ok`, empty, only spaces, `!!`, a status report, `sd: alpha_max_rate is set to 3000.0`, line with `Y:` first, lowercase letters, `ALARM: Hard limit`. `Invalid` (out unchanged): `S:` (plain `M203`), Z missing, X or A duplicated, `5e+01`, `-50`, `abc`, `.`, only `X:`, trailing garbage, commas, Z 0, X 0.999, X 166.6671, A 600.001, A 0.5, 8 integer digits, 7 decimal digits, `B: x`, `D: 1`; `n` is respected.
95. `RateQuery`: `RATE_QUERY_TEXT == "M203.1"`. Without status no query; first one with the first status; no second one meanwhile; `ok` changes nothing; timeout after 1000 ms, retry after 1 s, 2 s, 5 s, 5 s (due without status → waits for it); invalid response during the query → next one after the next wait time without timeout; valid → known, never queried again, later responses change nothing; `reset()` → unknown, all flags false, new query immediately. The same from `0xFFFFF000` (overflow). Invalid line while waiting → schedule unchanged. Late valid response while waiting → accepted. Fresh instance as after `reset()`.
96. Gate rule 7a and planner: `rates_known == false` → `NoRates`, also with session (rule 8 does not apply); `AxisNone`, `NoData`, `JobRunning`, `Alarm` come before 7a; `GateInputs{}` without rates → `NoRates`. Planner without rates: gate None over 11 ticks → no command, no session, `target_um(0) == 0`; after `set_rates(Z1 Pro)` +10 × 100 → `$J X1.000 F0.092`. Rates lost during a session → session ended, no command, `target_um(0) == 30`. Rates without A → A no command, Z `$J Z0.030 F0.012`. Old rates → `$J X1.000 F0.230` (as #32). `RATE_MAX` → `$J X1.000 F0.028`.
97. Real response of the Z1 Pro (firmware 1.1.3.0.1.13): the seven `0x90` payloads `' X: 50 '`, `' Y: 50 '`, `' Z: 16.6667 '`, `' A: 30 '`, `' B: 1.66667 '`, `'\r\n'`, `'ok\r\n'` as frames through `frame::Decoder` → `TextLines` → `RateQuery::on_line` (as in `machine_link.cpp`), in one piece, byte by byte, and each with a status frame after the third fragment → exactly two lines (response and `ok`), one of them Valid, equal to ` X: 50  Y: 50  Z: 16.6667  A: 30  B: 1.66667 `; rates `{50000, 50000, 16667, 30000}`, `xyz`, `a`, no further query. A at 30 °/s → `feed_limits` `[40, 1000]` (1.2 … 30 °/s), A v 10000 → 307.
98. No querying in alarm (review): `RateQuery::ready` → false without status, on `Alarm` and on `halted`, true on Idle and Run; `refresh_allowed` only on Idle without session (not Run, Alarm, halted, without status, with session). Connected in alarm: no query for 20 s, `error:Alarm lock` and `!!` → `NotRates`, unknown; first status without alarm → query immediately. Alarm during a query: timeout, wait time is skipped, query immediately with the first status without alarm, after that the steps start at 1 s.
99. Re-reading: known at t0 + 100 → no query before `RATE_REFRESH_MS`, none without `refresh_ok`, then one; meanwhile known with old rates, no second one, `ok` changes nothing; changed valid response (A 30) replaces. Invalid response during re-reading → old rates, known, `refresh_failures() == 1`; line outside re-reading changes nothing. Timeout during re-reading → old rates, `refresh_failures() == 2`; next valid response → 0. `reset()` → unknown, 0. The same from `0xFFFF0000`. Planner: `set_rates` with X 25 mm/s during a session → `rates()` stays 50, bundled command `$J X3.000 F0.368`; after `end_session` 25, new session `$J X1.000 F0.184`; without session `set_rates` applies immediately.

**Unlock** (ticket #41)
100. `UNLOCK_TEXT == "$X"`, `UNLOCK_HOLD_MS == 5000`. Alarm, edge X at t0: Holding, `permille` 0, at t0 + 2500 500, at t0 + 4999 999 and no sending; at t0 + 5000 exactly one `send`, view `Sent`; held further until t0 + 25000 never again, `Sent` until t0 + 5000 + `UNLOCK_SHOW_MS` − 1, then `Hint`. The same from `0xFFFFF000` (overflow). Released after 4980 ms → `losgelassen` (released), nothing; a new press again needs the full 5 s.
101. After sending, release and new hold (Z, after the end of `Sent`) → after the full 5 s again exactly one `send`, at least `UNLOCK_MIN_INTERVAL_MS` after the first (`static_assert(UNLOCK_HOLD_MS ≥ UNLOCK_MIN_INTERVAL_MS)`). Pressing during `Sent` starts no hold (held 15 s, no `send`). Button A counts like X, Y, Z if it is passed in.
102. Button pressed in Idle (edge without alarm), then alarm while the button is held: 15 s no `send`, view `Hint`; after release and new press exactly one `send`. An edge with `hint_visible == false` (alarm line takes precedence) does not count; a level alone without an edge (swallowed by the screensaver) never counts.
103. `blocked`: alarm with fresh status → nullptr, also at age exactly `SEND_MAX_AGE_MS`, with only `Alarm` and with only `halted` (Idle with `H:`). Each held for 10 s without `send` and with view `None`: offline (`keine Verbindung` (no connection)), without status and age `SEND_MAX_AGE_MS + 1` (`kein frischer Status` (no fresh status)), Idle and Run (`kein Alarm` (no alarm)), OTA (`OTA`), other AppMode (`Betriebsart` (operating mode)).
104. During holding, after 4020 ms one condition at a time drops out for one tick (offline, no status, fresh status without alarm, OTA, other operating mode, new message) → `aborted` with the reason; condition back, button still held → 10 s no `send`; after release and new press exactly one `send` after 5 s. Hold started with X, X released, Y still held → `losgelassen`, no `send`.
105. `Sent` ends with the first status without alarm (view `None`), otherwise after `UNLOCK_SHOW_MS` (`Hint` again, still alarm) or immediately on connection loss (`None`).
106. Sequence as in `main.cpp` (accumulator, StepPolicy, gate, planner, unlock with `end_session` on sending): in alarm turned for 0.9 s, then X pressed and held for 5 s while turning continues → no `$J`, `remainder() == 0`, `$X` exactly at press + 5000; then 5 s Idle without turning → no `$J`, no session. Second case: Idle received 50 ms after the `$X`, one detent 60 ms after the `$X` → no `$J`, no session; Idle received `START_GAP_MS` after the `$X`, a new detent → `$J X0.010 F0.008`.
107. `build(0xA2, UNLOCK_TEXT)`: length `2 + OVERHEAD`, type `0xA2`, payload `$X` without `\n`. The firmware response `[Caution: Unlocked]\nok\n` as one `0x90` frame through `Decoder` → `TextLines` → `RateQuery::on_line` in the phases start, query in flight, known and re-reading in flight: exactly 2 lines, both `NotRates`, none starts with `ALARM`/`error`; queries, `refresh_failures()` and rates unchanged; in flight, timeout and retry (1000 / 2000 ms) stay the same, a valid response during re-reading afterwards is accepted.
108. Status gaps while holding: status stale 2000–2580 ms (age `SEND_MAX_AGE_MS + 1`, alarm) → no abort, view `Holding`, `send` at 5000. Stale from 4800 to 5580 → no `send`, no abort, `permille` 999 from 5000 (full only on sending); first fresh alarm status at 5600 → `send`. In the waiting state (stale from 4800, at 5400) offline → `keine Verbindung`, likewise `new_message` → `neue Meldung` (new message), each without `send`, also not on a later fresh status with the button held. Released while waiting → `losgelassen`, no `send` afterwards. Stale status with Idle → no abort, no `send`; fresh Idle → `kein Alarm`. An edge on a stale status starts no hold.
109. New alarm/error line (`new_message`) at 3000 ms → `neue Meldung`, then with the button held 10 s no `send`; new press → exactly one `send` after 5 s. New message exactly in the tick of the 5 s → no `send`, `neue Meldung`.

**Soft power** (ticket #49)
110. `PowerSwitch(POWER_SW_DEBOUNCE_MS)`: on from t0 → `Unknown` until t0 + 99, `On` at t0 + 100. Five bounces off for 99 ms each → stays `On`, never `turned_off`. Off held: `On` until +99, `Off` with `turned_off` exactly at +100, later samples no second edge; on again → `On` after 100 ms without an edge. The same from `0xFFFFFF00` (overflow).
111. Boot: off from the first sample → `Off` with `turned_off` after 100 ms. Contact bouncing every 3 ms for 60 ms, then on → `Unknown` until stable for 100 ms, then `On`, no edge. Toggling every 50 ms for `POWER_SW_BOOT_MAX_MS` → always `Unknown`, never an edge. `POWER_SW_BOOT_MAX_MS > POWER_SW_DEBOUNCE_MS`.
112. `sleep_wake_when_on`: `SwitchOff` → true with the button off and on; `BatteryEmpty` with the button on → false (wait for off), off → true.
113. `board_rev_action`: no key → `Store` (value ignored); stored 1/compiled 1 and 2/2 → `Run`; 2/1, 1/2, 0/2, 255/2 → `WrongBoard`.
114. `BoardRevConfirm(BOARD_REV_CONFIRM_MS = 3000)`: held from the first sample for 12 s → never true (not armed); after a release a new hold → true after the full 3000 ms. Released → false; held 2999 ms and released → false; a new hold → true exactly at 3000 ms, only once per hold; released and held again → true again after 3000 ms. The same from `0xFFFFF000` (overflow).

## 12. Order of work

0. **Project skeleton** (ticket #1): directory tree from 6.1, `platformio.ini` from 6.2, `DECISIONS.md`, `HARDWARE_CHECKLIST.md` with H-1 to H-9 and the MANUAL acceptance checks from chapter 13. Build and placeholder test green, the pipeline runs. `SPEC.md`, `CLAUDE.md`, `.claude/` and `.gitlab-ci.yml` are already on `main` (initial commit). This step also goes through a branch and merge request.
1. `frame`, `text_lines`, tests 1–9.
2. `status`, tests 10–14.
3. `axis_select`, `jog_gate`, tests 15–21.
4. `step_policy`, tests 22–26.
5. `detents`, `jog_planner`, `fixed_fmt`, tests 27–50.
6. `battery`, test 51.
7. `encoder`, `inputs`, `display` (skeleton), `portal` including diagnostics screen: this makes H-1, H-2, H-3, H-4 and H-6 checkable on the device.
8. `machine_link`: the main screen shows live position and state, **still without jogging**.
9. **Jogging:** connect the planner to MachineLink.
10. `power` (battery shutdown) and `leds`.
11. `WIRING.md` complete: netlist, bill of materials from 7.1, terminal assignment of the handwheel, section "Safety" (residual risk job start 8.5, residual risk foreign commands 8.7, note on soft limits).
12. **Addendum (ticket #14):** optional rotary axis A in core logic, adapters, portal and docs, tests 52–56.
13. **Addendum (ticket #17):** web UI in normal operation (9.1), tests 61–67.
14. **Addendum (ticket #28):** screensaver (10.4), tests 68–74.
15. **Addendum (ticket #29):** web UI closed without response, test 75.
16. **Addendum (ticket #32):** feed from turning speed (`feed_rate`), bundling and queue upper limit, window 5 mm XY / 1 mm Z, tests 76–89.
17. **Addendum (ticket #35):** OTA update over Wi-Fi (9.5), tests 90–91.
18. **Addendum (ticket #37):** 3D view of the position with three.js from flash, progress-based send limit (9.1), test 92.
19. **Addendum (ticket #38):** read the machine's maximum rates via `M203.1` after every connect (8.8), jog gate locked until then (7a), `F` bounds from fixed maximum and minimum speeds (8.7), read tool `tools/z1_config_download.py`, re-reading when idle, tests 93–99.
20. **Addendum (ticket #41):** unlock after alarm via axis button held for 5 s (`$X`, 8.10), hint on display and in the web UI (read-only), tests 100–109.
21. **Addendum (ticket #49):** soft power: on/off button as an input on GPIO4, battery always connected (charges when off), deep sleep with ext0 wake (9.2, 9.3), carrier board re-laid out, board revision `HANDRAD_BOARD_REV` with the NVS guard `board_rev` (9.2), tests 110–114.


## 13. Acceptance criteria

**Machine-checkable:**
- [ ] `pio run -e firebeetle` ends with exit code 0.
- [ ] `pio test -e native` ends with exit code 0.
- [ ] Generated web files up to date: `python3 -I tools/make_web_assets.py && git diff --exit-code src/web_assets.h src/web_asset_paths.h` ends with exit code 0 (CI job `web-assets`, ticket #37).
- [ ] All mandatory tests present: `pio test -e native -v | grep -c ':PASS'` ≥ 114.
- [ ] `lib/core` is platform-free: `grep -rnE '#include *[<"](Arduino|WiFi|Network|esp_|driver/|freertos/|FreeRTOS|soc/|hal/|lwip/|U8g2|pins\.h|ui_config\.h)' lib/core` returns nothing.
- [ ] Pins only in `pins.h`: `grep -rnE '(pinMode|digitalRead|digitalWrite|analogRead|analogReadMilliVolts|ledcAttach|gpio_[a-z_]+)\( *(\(gpio_num_t\))? *[0-9]+|GPIO_NUM_[0-9]+|Wire\.begin\( *[0-9]' src lib` returns nothing.
- [ ] No old PCNT API: `grep -rn 'driver/pcnt.h' src lib include` returns nothing.
- [ ] No continuous jog: `grep -rn '\$J -c' src lib` returns nothing.
- [ ] No `WiFiClient` for the machine connection: `grep -rnE 'WiFiClient|NetworkClient' src` returns nothing.
- [ ] Web UI without access to the machine: `grep -nE '#include *"machine_link\.h"|machine::' src/webui.cpp src/webui.h src/settings.cpp` returns nothing.
- [ ] Checklist complete and open: `grep -cE '^- \[ \] H-[0-9]+' HARDWARE_CHECKLIST.md` ≥ 9, and `grep -cE '^- \[x\]' HARDWARE_CHECKLIST.md` = 0.

**[MANUAL]** (by the human at the real machine, in `HARDWARE_CHECKLIST.md`):
- [ ] H-1 to H-14 and H-16 to H-23 done.
- [ ] Job running in Studio: the handwheel shows `Job läuft` (job running), turning moves nothing.
- [ ] X alone, XY diagonal and Z alone move in the positive direction when turned clockwise. A press on Z with X selected deselects X and selects Z (LEDs show it).
- [ ] 60 s without turning: all axis LEDs go off, turning moves nothing.
- [ ] Turn the wheel fast for 1 s and let go: the machine stops after at most 5 mm (XY) or 1 mm (Z) of overrun.
- [ ] Turn steadily at medium and fast speed (ticket #32): the machine moves without strong stop-and-go, `Begrenzt` (limited) does not appear at medium turning speed (H-16).
- [ ] Turn fast back and forth: after letting go, no overrun beyond the window.
- [ ] Studio and Home Assistant stay connected while the handwheel is in use.
- [ ] On/off button off during motion (with and without USB): the movement ends within the window, after that the machine receives no more commands; the handwheel sleeps (H-23 e).
- [ ] Switch the handwheel off and on three times within 1 min, each time until "Online": Studio and Home Assistant stay connected, and the handwheel reconnects each time. Checks half-open connections on the machine's Wi-Fi module, result in `WIRING.md`.
- [ ] X held at power-on → web UI. "Diagnose starten" (start diagnostics) → diagnostics screen. Y or Z held at power-on → no axis selected.
- [ ] Let the battery contact J1 bounce briefly (simulate a loose contact) with a full battery: no shutdown. Tap the on/off button contact for under 0.1 s: no sleep.
- [ ] Battery shutdown (slowly regulate a bench supply instead of the battery below 3.3 V): display "Akku leer – bitte ausschalten" (battery empty – please switch off), afterwards quiescent current < 1 mA (H-5).
- [ ] Battery operation without USB, wheel at 3.3 V: one revolution turned briskly yields exactly 100 detents (H-4 under load), also at about 3.5 V battery voltage.
- [ ] Spindle running: the handwheel shows `Maschine belegt` (machine busy).
- [ ] Axis A enabled and rotary module connected: button A deselects and selects A, turning clockwise rotates A in the positive direction, overrun at most 2°. Axis A disabled: button A has no effect, LED A off.
- [ ] Web UI (ticket #17): status page reachable at `http://handrad.local/` and at the IP, values update about every second. Saving without or with a wrong handwheel password is rejected. Saving with an axis selected → 409, nothing changed. Without axis → saved, restart, new values active.
- [ ] Screensaver (ticket #28): 60 s without input → logo jumps every 5 s; 10 min → display off. Waking via button or wheel selects no axis and does not move, not even with two buttons. A glitch pulse does not wake. Never screensaver with an axis selected. Connection loss and alarm wake (H-14).
- [ ] OTA (ticket #35): off by default (port 3232 closed). Enabled: update via `pio run -e firebeetle-ota -t upload` works, display shows the progress, afterwards the new firmware runs. Rejected with an axis selected, the machine never moves. Abort in the middle of the upload → restart, the old firmware runs (H-17).
- [ ] Unlock (ticket #41): machine in alarm (e.g. `Ctrl-X`/HALT from Studio): display shows `Entsperren: Taste 5 s` (unlock: hold button 5 s); hold X for 4 s and release → nothing; hold X for 5 s → bar full, `Entsperren...` (unlocking...), machine `Idle`, nothing moves; detents turned during the alarm are not executed afterwards (H-21).
- [ ] 3D view (ticket #37): loads on phone and desktop without internet, shows tool and workpiece zero like the 2D map, the status page stays usable meanwhile, the loop duration does not rise above the H-13 measure while loading; after an OTA update the new 3D view comes along (H-19).

## 14. Explicitly out of scope

- B axis, tool change, spindle, dust extraction (the A axis is optionally in scope since ticket #14)
- Setting zero, homing, job control (unlocking an alarm with `$X` is in scope since ticket #41, only on the handwheel via held button, 8.10)
- **Emergency stop.** The handwheel is not a safety component; the machine's emergency stop remains authoritative.
- **Soft limits:** in the machine's default config they are off (`soft_endstop.enable false`). The handwheel can therefore drive into the hardware limit switches, which triggers an alarm. Recommendation in `WIRING.md`: enable soft limits in the machine. The handwheel itself does not check travel limits.
- Auto-discovery, multiple machines, Bluetooth, Home Assistant integration (OTA is in scope since ticket #35, 9.5)
- Jog commands, job control, unlock or realtime commands via the web UI (9.1); it is read-only, except for the settings
- Boards other than the FireBeetle 2 ESP32-UE

No preparatory abstraction is done for any of these items.

## 15. Subagents during the build

Features and bugfixes are always implemented by a subagent; the main context plans, checks and merges (CLAUDE.md). The table governs what may run **in parallel**.

| May be delegated in parallel | Only by **one** subagent at a time, never in parallel |
|---|---|
| `display.cpp` layouts | `jog_gate`, `jog_planner`, `feed_rate`, `machine_rates`, `step_policy`, `detents`, `battery`, `power_switch`, `board_rev`, `axis_select`, `screensaver`, `unlock`, `encoder.cpp`, `inputs.cpp` |
| Portal HTML | `frame`, `status`, `settings_rules` (acceptance rules 9.1, 9.5), `ota.cpp` (9.5) |
| `WIRING.md` | `core_config.h`, `pins.h` |
| Review of every diff before the merge (fresh context) | AppMode state machine in `main.cpp`, `power.cpp` |

The right column is safety-relevant. Anyone working there in parallel creates exactly the contradictions this spec is meant to avoid. A subagent working there gets the affected SPEC section in full in its briefing; its diff is checked against that section by another fresh context before the merge.
