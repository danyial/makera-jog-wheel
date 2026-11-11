#!/usr/bin/env python3
"""Baut die Baugruppe des Handrads mit den echten STEP-Modellen (Ticket #31).

  python3 -I make_assembly.py           schreibt handrad_assembly.json und baut daraus
                                        handrad_assembly.sc (bettet die Fremdmodelle ein, nur privates Repo)
  python3 -I make_assembly.py --check   zusaetzlich: Ueberschneidungen der Einbauteile mit den
                                        Gehaeuseschalen und untereinander (inspect.clearance)
  python3 -I make_assembly.py --no-run  nur handrad_assembly.json schreiben

Das Gehaeuse kommt verlinkt aus handrad_enclosure.sc (component.insert_design), die Einbauteile als
STEP-Import (file.insert_step). handrad_assembly.json enthaelt nur Pfade und Lagen, keine Fremdgeometrie.
Die STEP-Dateien liegen nur im privaten Repository (hardware/vendor/, fab/handrad.step) und nicht im
oeffentlichen Export (Lizenz unklar bzw. nicht weitergebbar, Ticket #50), siehe README.

Lagen: Ticket-Rahmen wie in make_enclosure.py, im CAD Y_CAD = L - y. Die Matrizen bilden die
Modellkoordinaten jedes STEP-Modells auf das Gehaeuse ab (zeilenweise 3 x 4, Drehung + Verschiebung).
"""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_enclosure as E  # noqa: E402  (Lagen und Masse des Gehaeuses, liest keine Fremddateien)

ENCLOSURE_SC = "handrad_enclosure.sc"
OUT_SC = "handrad_assembly.sc"
OUT_JSON = "handrad_assembly.json"
VENDOR = "../vendor/enclosure"
STEP = {
    "Traegerplatine": "../handrad_pcb/fab/handrad.step",   # aus KiCad (hardware/export.sh), mit FireBeetle
    "Display": f"{VENDOR}/oled_1p3.step",                  # SH1106 1,3"
    "Taster": f"{VENDOR}/button_12mm.step",                # 12-mm-Metalltaster (auch Ein/Aus)
    "Handrad": f"{VENDOR}/zss600.step",                    # Handrad 60 mm
}

Y = E.Y

# --- Modellmasse aus den STEP-Dateien (gemessen, siehe DECISIONS Ticket #31) ---
# Traegerplatine (KiCad-Export): Platine x 0..78, y -66..0 (KiCad-y nach unten = STEP -y), Oberseite +z,
# THT-Teile oben, SMD unten. Ticket-Rahmen = KiCad-Draufsicht: Ticket-x = 3.5 + x, Ticket-y = 12 - y.
PCB_ORIGIN = (3.5, 12.0, 7.0)          # linke obere Ecke der Platine (Ticket), Unterseite z 7
# Display: Platine x -2.5..32.9, y -2..31.5, z -1.2..0, Glas z 0..1.5 (+0.1 aktive Flaeche), Stiftleiste
# bei grossem y. Loecher bei x 0 / 30.4, y 0.5 / 29. Modulplatine liegt auf den Stift-Schultern.
OLED_HOLE = (0.0, 29.0)                # Modellpunkt -> Stift bei (DISPLAY_CX - 15.2, 12.5)
# Taster: Achse parallel zu Modell-y bei x 0, z 0.538141; Kopf zeigt nach -y. Kopf Oe 14 bis y -11.966,
# dahinter Dichtring Oe 12.7 bis y -11.166 (liegt auf dem Taschenboden), Mutter y -9.466..-7.166.
BTN_AXIS_Z = 0.538141082087404
BTN_SEAT_Y = -11.165729624825596
# Handrad: Achse Z, Montageflaeche (Rueckseite des Flansches) z 0, Front nach +z, Bolzen bei 90/-30/-150 Grad
# (Modell-y = 12 Uhr = zu den Tastern). Keine Drehung noetig.

# Farben/Transparenz der Schalen
LOOK = {"FrontShell": ("#3b82c4", 0.35), "BottomTray": ("#9aa0a6", 0.5)}


def cmd(c, **p):
    return {"command": c, "params": p}


def mat(r, t):
    """Zeilenweise 3x4-Matrix aus Drehung r (3x3) und Verschiebung t."""
    return [round(v, 6) for i in range(3) for v in (*r[i], t[i])]


I3 = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
# Taster in der Front: Modell -y -> +Z, Modell z -> CAD Y (Drehung -90 Grad um X)
R_BTN = [[1, 0, 0], [0, 0, 1], [0, -1, 0]]
# Ein/Aus in der rechten Wand: Modell -y -> +X, Modell x -> CAD Y, z bleibt (Drehung +90 Grad um Z)
R_SW = [[0, -1, 0], [1, 0, 0], [0, 0, 1]]


def placements():
    pocket_floor = E.H - E.POCKET_DEPTH                     # 39: Taschenboden der Taster
    out = []
    px, py, pz = PCB_ORIGIN
    out.append(("Traegerplatine", "Traegerplatine", mat(I3, (px, Y(py), pz))))
    ox, oy = OLED_HOLE
    peg_x, peg_y = E.OLED_PEGS[0]                           # (30.8, 12.5)
    glass_back = E.H - E.FRONT - E.OLED_SHOULDER_H          # 35.5: Modulplatine auf den Schultern
    out.append(("Display", "Display", mat(I3, (peg_x - ox, Y(peg_y) - oy, glass_back))))
    for bx, by, t in E.BUTTONS:
        out.append(("Taster", f"Taster_{t}",
                    mat(R_BTN, (bx, Y(by) - BTN_AXIS_Z, pocket_floor + BTN_SEAT_Y))))
    sy, sz = E.SWITCH
    out.append(("Taster", "EinAus", mat(R_SW, (E.W - E.POCKET_DEPTH + BTN_SEAT_Y, Y(sy), sz - BTN_AXIS_Z))))
    wx, wy = E.WHEEL
    out.append(("Handrad", "Handrad", mat(I3, (wx, Y(wy), E.WHEEL_SEAT_Z))))
    return out


def script():
    c = [cmd("component.insert_design", path=ENCLOSURE_SC, name="Gehaeuse")]
    for body, (col, op) in LOOK.items():
        c.append(cmd("appearance.assign", bodies=[body], color=col, opacity=op))
    for key, name, m in placements():
        c += [cmd("component.activate", component="root"),
              cmd("file.insert_step", path=STEP[key], name=name, matrix=m)]
    c += [cmd("component.activate", component="root")]
    return {"commands": c}


def check_script():
    return {"commands": [
        # Einbauteile gegen die Schalen
        cmd("inspect.clearance", against=["FrontShell", "BottomTray"], min_volume=0.001),
        # Einbauteile untereinander (Teile innerhalb eines Modells zaehlen nicht)
        cmd("inspect.clearance", skip_within="component", min_volume=0.001),
    ]}


class CliError(Exception):
    pass


def run(args, **kw):
    print("$", " ".join(args), flush=True)
    try:
        return subprocess.run(args, cwd=HERE, check=True, **kw)
    except FileNotFoundError:
        raise CliError(f"{args[0]} nicht gefunden: SolidCraft-CLI installieren bzw. in den PATH legen.")
    except subprocess.CalledProcessError as e:
        raise CliError(f"{args[0]} ist mit Status {e.returncode} abgebrochen.")


def parse_args(argv):
    ap = argparse.ArgumentParser(
        description="Baut handrad_assembly.sc aus handrad_enclosure.sc und den STEP-Modellen (Ticket #31).")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--no-run", action="store_true", help="nur handrad_assembly.json schreiben")
    g.add_argument("--check", action="store_true",
                   help="nach dem Bauen Ueberschneidungen messen (dauert etwa 20 min)")
    return ap.parse_args(argv)


def check(results_expected=2):
    chk = os.path.join(HERE, ".assembly_check_tmp.json")
    res = os.path.join(HERE, ".assembly_check_result.json")
    with open(chk, "w") as f:
        json.dump(check_script(), f)
    try:
        run(["solidcraft-cli", "run", chk, "--in", OUT_SC, "--results", "--json-out", res],
            stdout=subprocess.DEVNULL)
        with open(res) as f:
            results = json.load(f)
    finally:
        for p in (chk, res):
            if os.path.exists(p):
                os.remove(p)
    if not isinstance(results, list) or len(results) != results_expected:
        raise CliError(f"unerwartetes Pruefergebnis: {results_expected} Ergebnisse erwartet, "
                       f"{len(results) if isinstance(results, list) else type(results).__name__} erhalten")
    shells = set(LOOK)
    for n, (title, r) in enumerate(zip(("gegen die Schalen", "untereinander"), results)):
        keep = (lambda a, b: True) if n == 0 else (lambda a, b: a not in shells and b not in shells)
        ov = sorted((o for o in r.get("overlaps", []) if keep(o["a"], o["b"])),
                    key=lambda o: -o["volume_mm3"])
        print(f"\nUeberschneidungen {title}: {len(ov)}")
        for o in ov:
            (x0, y0, z0), (x1, y1, z1) = o["bbox"]["min"], o["bbox"]["max"]
            # Lage im Ticket-Rahmen (y = L - Y_CAD)
            print(f"  {o['a']}  <->  {o['b']}: {o['volume_mm3']:.3f} mm3, x {x0:.1f}..{x1:.1f}, "
                  f"y {E.L - y1:.1f}..{E.L - y0:.1f}, z {z0:.2f}..{z1:.2f}")
        for u in r.get("unchecked", []):
            if keep(u["a"], u.get("b")):
                print(f"  nicht geprueft: {u['a']}  <->  {u.get('b')}: {u['reason']}")


def main(argv=None):
    args = parse_args(argv)
    with open(os.path.join(HERE, OUT_JSON), "w") as f:
        json.dump(script(), f, indent=1)
        f.write("\n")
    print(f"{OUT_JSON} geschrieben")
    if args.no_run:
        return 0
    need = [ENCLOSURE_SC] + list(STEP.values())
    missing = [p for p in need if not os.path.isfile(os.path.join(HERE, p))]
    if missing:
        repo = os.path.dirname(os.path.dirname(HERE))
        print("Fehlende Dateien, Baugruppe nicht gebaut:", file=sys.stderr)
        for p in missing:
            print("  " + os.path.relpath(os.path.normpath(os.path.join(HERE, p)), repo), file=sys.stderr)
        print("STEP-Modelle nach hardware/vendor/enclosure/ legen, handrad.step mit hardware/export.sh "
              "erzeugen, handrad_enclosure.sc siehe README (Neu erzeugen).", file=sys.stderr)
        return 1
    try:
        run(["solidcraft-cli", "run", OUT_JSON, "--save", OUT_SC, "--quiet"])
        print(f"{OUT_SC} gebaut")
        if args.check:
            check()
    except CliError as e:
        print(f"Fehler: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
