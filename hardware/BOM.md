# Bill of Materials (ticket #43)

Everything needed for one handwheel. Sources: SPEC 7.1, `hardware/README.md` (carrier board), `hardware/enclosure/README.md` (enclosure). If anything conflicts, SPEC wins.

## Electronics

| Qty | Part | Details / notes | Ref |
|---|---|---|---|
| 1 | Microcontroller DFRobot **FireBeetle 2 ESP32-UE** (DFR1140) | ESP32-WROOM-32UE-N16R2, 16 MB flash, u.FL, LiPo charger, USB-C. With soldered pin headers 1 × 18 and 1 × 14 (2.54 mm) | U1 |
| 1 | Handwheel (MPG) **ZSS600-001-100B-5L** | 60 mm, 100 pulses/rev, 5 V, line driver; operated at 3.3 V (SPEC 7.2). Comes with its 3 M3 studs | J6 |
| 1 | Display **SH1106 1.3″** | 128 × 64, I2C, address 0x3C. Check the module's pin order (often GND, VCC, SCL, SDA) | J4 |
| 4 | 12 mm metal push button, **momentary**, green LED ring, IP65: Gebildet, pack of 4 ([Amazon B0HHJDQNP7](https://www.amazon.de/dp/B0HHJDQNP7)) | SPST 1 NO, 3 A, nickel-plated copper, pre-soldered leads. LED 3–9 V with built-in resistor, mounting nut M12; red = LED+, black = LED−, green/green = contact. X, Y, Z, A (A only with rotary axis) | J5, J7–J9 |
| 1 | 12 mm metal push button, **latching** (same series): Gebildet, pack of 4 ([Amazon B0HHJD2RKZ](https://www.amazon.de/dp/B0HHJD2RKZ)) | SPST 1 NO, self-locking, 3 A. On/off button, since ticket #49 only a **signal** (soft power, GPIO4 to GND, internal pull-up): no current from the battery flows through it, no extra parts (no resistor) needed. Only the contact is used (LED unconnected), 3 spare in the pack | J2 |
| 1 | Battery **YELUFT 3.7 V 3000 mAh LiPo 1S** with protection board ([Amazon B0F18QM8SJ](https://www.amazon.de/dp/B0F18QM8SJ)) | Protection at 3.0 V / 4.2 V, recommended charge current 0.6 A, 30 g. Ships with a **JST 1.25** plug; on the installed battery it was replaced with **JST-PH 2.0** because J1 is a PH socket. Polarity: J1 pin 1 (top) = +, pin 2 = −. Check with a multimeter before plugging in. Plug the battery into J1 only; it is then always connected to the FireBeetle (charges on USB even when switched off). The FireBeetle's own PH2.0 socket stays **empty**, otherwise two paths feed the board. For storage unplug J1 (the sleeping handwheel draws about 0.5 mA) | J1 |
| 1 | PCB adhesive antenna 2.4 GHz, 5 dBi, **u.FL**: sourcing map, pack of 2 ([Amazon B0816WKB5V](https://www.amazon.de/dp/B0816WKB5V)) | Board 50 × 15 mm, cable RF1.13 **195 mm**. Stuck inside the top end wall into the 51 × 16 × 0.3 mm positioning recess (centre x 46, z 25). Lay the surplus cable (~15 cm) as one loose flat loop on the free top edge of the carrier board, not over the antenna (`hardware/enclosure/README.md`) | — |

## Carrier board (single-sided, milled, `hardware/handrad_pcb`)

| Qty | Part | Details / notes | Ref |
|---|---|---|---|
| 1 | Single-sided copper-clad blank | at least 78 × 66 mm, 1.6 mm; copper on the bottom | — |
| 1 | Female header 1 × 18, 2.54 mm, 8.5 mm high | FireBeetle, bottom row | U1A |
| 1 | Female header 1 × 14, 2.54 mm, 8.5 mm high | FireBeetle, top row | U1B |
| 1 | P-channel MOSFET **AO3401** SOT-23, marking A19T: JXRSKJ, pack of 50 ([Amazon B0GXDJW88Q](https://www.amazon.de/dp/B0GXDJW88Q)) | Switches the handwheel supply (GPIO18); the pack also covers the conditional LED switches | Q1 |
| 4 | Resistor 10 kΩ 0805 | Pull-ups wheel A/B, buttons Z/A (X/Y use internal pull-ups) | R1, R2, R4, R5 |
| 1 | Resistor 100 kΩ 0805 | Gate pull-up Q1 | R6 |
| 2 | Pogo pin **P50-J1** ([Amazon B0988ZMG29](https://www.amazon.de/dp/B0988ZMG29), pack of 100, 3 A, 50 mΩ) | round head, barrel Ø 0.68, 16 mm, 2.6 mm travel; battery to the FireBeetle's "Li-ion +/−" pads | P1, P2 |
| 1 | **JST-PH** header 2-pin (2.0 mm), vertical | Battery | J1 |
| 1 | **JST-XH** header 2-pin (2.5 mm), vertical | On/off button (signal) | J2 |
| 6 | **JST-XH** header 4-pin (2.5 mm), vertical | Display, wheel, buttons X/Y/Z/A | J4–J9 |

## Cables and connectors

| Qty | Part | Notes |
|---|---|---|
| 1 | JST-XH housing 2-pin | On/off button |
| 6 | JST-XH housing 4-pin | Display, wheel, 4 buttons |
| approx. 30 | JST-XH crimp contacts | 26 needed, rest spare |
| 1 | JST-PH housing 2-pin + 2 crimp contacts | only if the battery plug still needs replacing |
| approx. 1 m | Stranded wire 0.14–0.25 mm² (AWG 26–24), several colours | Display, switch; buttons and wheel have their own leads |
| — | Heat-shrink tubing | Solder joints on buttons and wheel |

## Enclosure (`hardware/enclosure`)

| Qty | Part | Notes |
|---|---|---|
| 1 | Front shell `front_shell.stl` | PETG preferred (or PLA), 0.2 mm layers, 3 walls, 20 % gyroid |
| 1 | Bottom tray `bottom_tray.stl` | as above |
| 4 | Socket head cap screw M3 × 10 (ISO 4762) | Enclosure, from below |
| 4 | Heat-set insert M3, length ≤ 6 mm, for Ø 4.0 hole | Bosses of the front shell |
| 4 | Thread-forming screw M3 × 6 for plastic (or sheet-metal screw 2.9 × 6.5) | Carrier board |
| 3 | Nut M3 + washer | Handwheel studs at the bottom of the recess |
| approx. 20 cm | Hook-and-loop strap 16 mm | Battery |

## Conditional (only if a hardware check fails)

| Qty | Part | Needed when |
|---|---|---|
| 3 (4 with button A) | AO3401 SOT-23 + 100 kΩ 0805 each, high-side switch per button LED | H-3: LED current above 12 mA (SPEC 7.4) |
| 1 + 1 | 10 kΩ 0805 in series + 100 nF between gate and source of Q1 (soft start) | H-8: resets when switching on the wheel (SPEC 7.2) |
| 1 + 2 | 5 V step-up converter + BSS138 level shifter (A/B) | H-4: wheel loses steps at 3.3 V (SPEC 7.2) |

The battery voltage divider (H-6) is already on the FireBeetle (measured 2026-11-09, `DECISIONS.md`), so no extra parts are needed.

## Tools (not part of the build)

Milling machine for the board, 3D printer, soldering iron (also for the heat-set inserts), crimp tool for JST-XH/PH, multimeter, 12.3 mm gauge for the pogo pin height (`hardware/README.md`).
