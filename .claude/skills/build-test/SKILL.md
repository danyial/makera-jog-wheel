---
name: build-test
description: Use when building the firmware, running unit tests, reading test output, adding a test, flashing or monitoring the ESP32, or when a PlatformIO build or Unity test fails in the makera-handrad repo.
---

# Building and testing

## Commands

Board revision (ticket #49): `HANDRAD_BOARD_REV`, evaluated in `include/pins.h` and `src/`; `firebeetle`/`firebeetle-ota` build the soft power board. The boot line shows `Platine Rev. n`.


```bash
pio run -e firebeetle                 # build firmware  (espressif32 8.x, Arduino-ESP32 3.3.x)
pio test -e native                    # core logic tests on the host (Unity)
pio test -e native -v                 # one line per test "file:line:name:PASS"
pio test -e native -v | grep -c ':PASS'   # number of passed tests (target: SPEC ch. 13)
```

Flashing and monitoring are done by **the human** at the device (agents must not flash; in the private repository `.claude/settings.json` blocks it):

```bash
pio run -e firebeetle -t upload --upload-port /dev/cu.wchusbserial10
pio device monitor -b 115200
```

Over Wi-Fi (OTA, SPEC 9.5, ticket #35), only with "Update über WLAN erlauben" (allow update over Wi-Fi) and with no axis selected. Password = handwheel password (`ap_pass`), only from the environment, never into the repo:

```bash
HANDRAD_OTA_PASS=<Handrad-Passwort> pio run -e firebeetle-ota -t upload                        # handrad.local
HANDRAD_OTA_PASS=<Handrad-Passwort> pio run -e firebeetle-ota -t upload --upload-port <IP>     # by IP
```

Reading the machine configuration (ticket #38), read-only, no G-code, no upload. The downloaded file does not belong in the repo:

```bash
python3 -I tools/z1_config_download.py <machine-IP> /sd/config.txt /tmp/z1_config.txt
grep -E '^(alpha|beta|gamma|delta)_max_rate|^acceleration' /tmp/z1_config.txt
```

The firmware itself reads the effective rates via `M203.1` after every connect (serial `Raten: …` (rates: …), SPEC 8.8); the file shows only the configuration, without `config-override`.

## Rules

- Core logic belongs in `lib/core` and is tested natively. No `Arduino.h`, no ESP-IDF header there (check commands: SPEC ch. 13).
- Every test from SPEC ch. 11 is its own test function. Name with test number, e.g. `test_29_window_clip`.
- Time always as parameter `now_ms`, differences as `(uint32_t)(now - t)`.

## Typical errors

| Message | Cause |
|---|---|
| `UnknownPackageError: olikraus/U8g2 @ ^2.37.1` | registry only knows up to 2.36.18 → `^2.36.18` |
| `fatal error: Arduino.h` in the `native` build | Arduino code ended up in `lib/core` |
| `driver/pcnt.h: No such file` | old PCNT API → `driver/pulse_cnt.h` |
| Test hangs in the `native` build | endless loop without `now_ms` progress in the test |
| Board does not appear as `/dev/cu.*` (macOS) | CH340 with PID 0x7522 needs the WCH driver |
| espota: `No response from the ESP` / `No response from device` | OTA off in the settings, axis selected, session active or battery low (SPEC 9.5), or firewall blocks UDP 3232 / the reply connection |
| Display/web `Raten unbekannt` (rates unknown), serial `Raten: Antwort verworfen: …` (rates: answer discarded: …) | answer to `M203.1` does not match the parser (SPEC 8.8) or rate out of range; note the line, never bypass the lock |
| espota: `Authentication Failed` | `HANDRAD_OTA_PASS` empty or wrong (handwheel password: display at startup with the X button held) |
