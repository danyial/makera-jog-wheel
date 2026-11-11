#!/usr/bin/env python3
"""Erzeugt die SolidCraft-Skripte des Handrad-Gehaeuses (Ticket #30).

  enclosure.json        Frontschale + Bodenschale (die druckbaren Teile)
  assembly_check.json   Platzhalter der Einbauteile fuer die Kollisionspruefung
                        (laeuft mit --in handrad_enclosure.sc)

Alle Lagen unten stehen im Ticket-Rahmen: x 0..92 von links nach rechts (Frontansicht),
y 0..200 von der Displayseite (oben) nach unten, z 0..40 vom Boden zur Front.
Dieser Rahmen ist linkshaendig (wie die KiCad-Draufsicht). SolidCraft ist rechtshaendig,
deshalb gilt im CAD-Modell Y_CAD = L - y: Von vorn (+Z) gesehen liegt das Display oben,
die USB-C-Buchse links, wie in der Wirklichkeit. Ohne die Spiegelung waere das Gehaeuse
seitenverkehrt und passte nicht zur Platine.
"""
import json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# --- Hauptmasse (auch als SolidCraft-Parameter, siehe PARAMS) ---
W, L, H = 92.0, 200.0, 40.0
WALL, FLOOR, FRONT = 2.5, 3.0, 3.0
SPLIT = 12.0
R_CORNER = 10.0
LIP_OFF, LIP_W, LIP_H, GAP = 1.5, 1.2, 2.0, 0.2
EDGE_CHAMFER = 1.5   # 45 Grad an der Front- und der Bodenkante (die senkrechten Ecken bleiben r 10)

PARAMS = [("W", W), ("L", L), ("H", H), ("wall", WALL), ("floor_t", FLOOR), ("front_t", FRONT),
          ("split", SPLIT), ("r_corner", R_CORNER), ("lip_off", LIP_OFF), ("lip_w", LIP_W),
          ("lip_h", LIP_H), ("gap", GAP), ("edge_c", EDGE_CHAMFER)]


def Y(y):
    """Ticket-y (von der Displayseite) -> CAD-Y."""
    return round(L - y, 4)


def cmd(c, **p):
    return {"command": c, "params": p}


def cyl(name, x, y, z0, h, d, op, target, axis="Z"):
    return cmd("solid.cylinder", name=name, diameter=d, height=h, base=[x, Y(y), z0], axis=axis,
               operation=op, targets=[target])


def box(name, x0, x1, y0, y1, z0, z1, op, target):
    """Quader im Ticket-Rahmen (y0<y1 im Ticket-Rahmen)."""
    return cmd("solid.box", name=name, length=round(x1 - x0, 4), width=round(y1 - y0, 4),
               height=round(z1 - z0, 4), corner=[x0, Y(y1), z0], operation=op, targets=[target])


def rrect_sketch(name, z_expr, offs):
    """Skizze mit gerundeten Rechtecken, je um `o` (Ausdruck) von der Aussenkontur eingerueckt."""
    out = [cmd("sketch.create", plane="XY", offset=z_expr, name=name)]
    for o in offs:
        out.append(cmd("sketch.rectangle.rounded", p0=[o, o], p1=[f"W - ({o})", f"L - ({o})"],
                       radius=f"r_corner - ({o})"))
    out.append(cmd("sketch.finish"))
    return out


SCREWS = [(8, 8), (84, 8), (8, 192), (84, 192)]
STANDOFFS = [(8, 16.5), (77, 16.5), (8, 73.5), (77, 73.5)]
# Display (SH1106 1.3", Masse siehe DECISIONS): waagerecht mittig, alle x relativ zur Modulmitte
DISPLAY_CX = W / 2                  # 46
OLED_MOD_W, OLED_GLASS_W, OLED_WIN_W = 35.4, 34.4, 30.4
OLED_PEG_DX = 15.2                  # Stifte bei Modulmitte +-15.2 (Locher des Moduls)
OLED_HDR = (-5.3, 5.7)              # Tasche fuer die Loetspitzen, x relativ zur Mitte (11 breit)
# Flexband des Displays: liegt laut STEP-Modell am unteren Glasrand 0,1 ueber dem Glas (x Mitte +-11,
# y 33,4..43,5) und laeuft um die Modulplatine. Freistellung in der Innenseite der Front, damit die Platte
# nicht auf Flexband und Bondstelle drueckt (Ticket #31).
OLED_FLEX_RELIEF = (11.5, 33.0, 44.0, 0.4)   # halbe Breite um DISPLAY_CX, y0, y1 (Ticket-Rahmen), Tiefe
OLED_PEGS = [(DISPLAY_CX + dx, py) for py in (12.5, 41) for dx in (-OLED_PEG_DX, OLED_PEG_DX)]
BUTTONS = [(16, 86, "X"), (36, 86, "Y"), (56, 86, "Z"), (76, 86, "A")]
WHEEL = (46, 150)
PCD = 50.8
SWITCH = (108, 25)  # y, z an der rechten Seitenwand
# Frontstruktur "A - Felder": drei flache Felder in der Front (Ticket-Rahmen)
FIELD_DEPTH, FIELD_CHAMFER = 0.6, 0.3
FIELD_DISPLAY = (22.0, 70.0, 9.0, 40.5, 4.0)     # x0, x1, y0, y1, r
FIELD_BUTTONS = (5.5, 86.5, 74.0, 105.0, 5.0)
FIELD_WHEEL_D = 72.0                             # Kreis um die Handrad-Tasche
# Gravur "MPG" zwischen Display- und Tastenfeld (Ticket #45). Standard (eingecheckte Ausgaben, auch im
# oeffentlichen Export): "MPG" ganz in Helvetica fett. Mit --logo (Ticket #50) wird aus logo_polygons.json
# (Makera-"M", Marke, nur im privaten Repo, aus trace_logo.py) Makera-"M" + "PG", geschrieben nach
# enclosure_logo.json statt enclosure.json. Gleiche Lage, Hoehe und Tiefe.
LOGO_FILE = os.path.join(HERE, "logo_polygons.json")
USE_LOGO = False                                 # setzt nur der Aufruf mit --logo
LOGO_CAP_H, LOGO_CY, LOGO_DEPTH, LOGO_GAP = 14.0, 57.0, 0.6, 1.5
PG_FONT = "Helvetica"
PG_INK = (1.548, 26.794)   # Tinte von "PG" bei Hoehe 14 relativ zu at.x (gemessen, fuer die Zentrierung)
MPG_INK = (1.434, 42.997)  # Tinte von "MPG" bei Hoehe 14 relativ zu at.x (gemessen, Ticket #45)
# Griffrillen in beiden Laengswaenden: senkrecht, Enden rund, ueber die ganze gerade Wand.
# Unten 1.1 ueber dem Falz (dort ist die Aussenhaut nur 1.3, an der Falzecke bleiben so >= 1.2), oben
# 1.5 unter der Kantenfase.
GRIP_W, GRIP_DEPTH, GRIP_PITCH = 1.0, 0.8, 4.0
GRIP_Z = (SPLIT + LIP_H + GAP + 1.1, H - EDGE_CHAMFER - 1.5)      # 15.3 .. 37.0
GRIP_Y = (R_CORNER + 2.0, L - R_CORNER - 2.0)                      # 12 .. 188
GRIP_KEEP = 2.0            # Abstand zu Schalterloch, I/O-Gravur und USB-C-Senkung
IO_TEXT_W = 7.5            # Breite von "I/O" bei 4 mm Versalhoehe (Liberation Sans, aufgerundet)
# Gravuren: Abstand zur Auflage von Tasterkopf bzw. Schalterring
BUTTON_HOLE_D = 12.2   # M12-Gewinde
BUTTON_HEAD_D = 14.0   # Kopf/Ring der Taster (auch Ein/Aus) liegt in einer Tasche
# Ein/Aus ist ein rastender Taster derselben 12-mm-Serie wie die Achs-Taster: gleiches Loch, gleiche Tasche
# Display-Fenster: Fase 2:1, 2.0 auf der Front, 1.0 in die Tiefe
WIN_CHAMFER = (2.0, 1.0)
LABEL_CLEAR = 1.5      # freier Rand zwischen Ring und Schrift
AXIS_TEXT_H, AXIS_TEXT_DEPTH = 5.0, 0.6
# Tasterkoepfe und Handrad-Front liegen in flachen Taschen der Front
BUTTON_POCKET_D = BUTTON_HEAD_D + 2 * 0.5   # 15.0
WHEEL_POCKET_D = 60.0 + 2 * 0.5             # Front des Handrads 60 -> 61.0
POCKET_DEPTH = 1.0
POCKET_CHAMFER = 0.3
# Lochmitte -> Schriftmitte: Taschenrand inkl. Fase + freier Rand + halbe Schrifthoehe (11.8)
LABEL_OFFSET = BUTTON_POCKET_D / 2 + POCKET_CHAMFER + LABEL_CLEAR + AXIS_TEXT_H / 2
SWITCH_KEEP_R = BUTTON_POCKET_D / 2 + POCKET_CHAMFER   # Taschenrand des Ein/Aus-Tasters inkl. Fase
# Handrad eingelassen: Auflage (Rueckseite des Flansches Oe 60 x 6,0) 6,4 unter der Front. Die Front ist
# nur 3 dick, deshalb ragt ein Topf nach innen: Schacht WHEEL_POCKET_D, Wand ringsum 2, Boden 2,5 mit
# Ausschnitt und den drei Bolzenloechern. Der Flansch steht dann 0,4 unter der Front (0,2 ueber dem Feldboden).
WHEEL_SEAT_DEPTH = 6.4                       # Front -> Auflageflaeche
WHEEL_POT_WALL, WHEEL_POT_FLOOR = 2.0, 2.5
WHEEL_CUTOUT_D = 43.0                        # Koerper Oe 42,5 laut STEP-Modell, 0,25 Luft je Seite
WHEEL_HOLE_D = 3.5                           # Bolzen M3
WHEEL_BODY_D = 42.5                          # Koerper hinten laut STEP-Modell (Platzhalter)
WIRE_BUNDLE, WIRE_BEND_R = 3.0, 3.0          # Litzenbuendel des Handrads, Biegeradius (Platzhalter)
WHEEL_SEAT_Z = H - WHEEL_SEAT_DEPTH                 # 33.6: Auflage des Handrads, Oberseite des Topfbodens
WHEEL_POT_Z = WHEEL_SEAT_Z - WHEEL_POT_FLOOR        # 31.1: Unterseite des Topfbodens
SWITCH_TEXT_H, SWITCH_TEXT_DEPTH = 4.0, 0.5
# Display-Stifte: Schulter (Abstand Glas/Platine) und Stift bis 2.2 mm hinter die Modulplatine
OLED_SHOULDER_H, OLED_PCB_T, OLED_PIN_PAST = 1.5, 1.2, 2.2
BOSS_D = 8.0           # Dom fuer die Einschmelzmutter
# Klettband: Schlitze ausserhalb der Akkutasche, links durch eine Luecke in RibLeft
STRAP_Y = (91.0, 108.0)
STRAP_SLOTS = [(10.0, 12.5), (79.5, 82.0)]
STRAP_CHANNEL_D = 1.5     # Tiefe des Kanals aussen im Boden
# USB-C: Buchse des FireBeetle laut KiCad-STEP (Ticket #34): Stirn 8,94 x 3,16 r 1,3, Mitte y 32,0,
# z 22,875, Stirn buendig mit der Wandinnenseite (x 2,5). Am Probedruck sitzt die echte Buchse 0,8 mm
# naeher am Display (Foto des Nutzers), deshalb USB_Y 31,2; das Modell zeigt keinen Versatz.
# Durchbruch = Buchse + 0,5 ringsum (9,94 x 4,16 r 1,8). USB_Z bleibt 22,8 (Nutzer: in z mittig).
# Aussen Senkung 15 x 9 r 2 auf Restwand 1.2 fuer umspritzte Stecker (Groesse unveraendert, mittig auf USB_Y).
USB_Y, USB_Z = 31.2, 22.8
USB_JACK, USB_JACK_R, USB_CLEAR = (8.94, 3.16), 1.3, 0.5
USB_HOLE = (USB_JACK[0] + 2 * USB_CLEAR, USB_JACK[1] + 2 * USB_CLEAR)   # 9.94 x 4.16
USB_HOLE_R = USB_JACK_R + USB_CLEAR                                    # 1.8
USB_RECESS, USB_R, USB_REST = (15.0, 9.0), 2.0, 1.2
# PCB-Klebeantenne 50 x 15 (Ticket #44) innen an der oberen Stirnwand (Innenflaeche y = wall): Mulde
# 51 x 16 x 0.3 (0.5 Spiel je Seite), freie Flaeche 55 x 19 (Rand 2 seitlich, 1.5 oben/unten). Mitte x 46 statt 55:
# Bei 55 stiesse die freie Flaeche (bis x 82.5) an den Dom rechts oben (x 80..88); bei 46 (Gehaeusemitte)
# hat die Antenne (x 21..71) zu beiden Einschmelzmuttern (x 8 und 84) je etwa 10.5 mm Abstand.
# z 25: freie Flaeche z 15.5..34.5 bleibt 1.3 ueber dem Falz (bis z 14.2) und 2.5 unter der Frontplatte (z 37).
ANT_X, ANT_Z = W / 2, 25.0
ANT_SIZE, ANT_CLEAR, ANT_MARGIN, ANT_DEPTH = (50.0, 15.0), 0.5, (2.0, 1.5), 0.3
ANT_RECESS = (ANT_SIZE[0] + 2 * ANT_CLEAR, ANT_SIZE[1] + 2 * ANT_CLEAR)              # 51 x 16
ANT_FREE = (ANT_RECESS[0] + 2 * ANT_MARGIN[0], ANT_RECESS[1] + 2 * ANT_MARGIN[1])    # 55 x 19


def web_box(sx, sy, z0, z1, op, target, name):
    """Steg von der Schraubensaeule zur Gehaeuseecke (bleibt innerhalb der Wand)."""
    x0, x1 = (3, sx) if sx < W / 2 else (sx, W - 3)
    y0, y1 = (3, sy) if sy < L / 2 else (sy, L - 3)
    return box(name, x0, x1, y0, y1, z0, z1, op, target)


def enclosure():
    c = [cmd("parameters.add", name=n, expression=f"{v} mm") for n, v in PARAMS]
    # Aussenform
    c += [cmd("solid.box", name="Outer", length="W", width="L", height="H", corner=[0, 0, 0],
              body_name="Shell"),
          cmd("solid.fillet", name="CornerRadius", radius="r_corner",
              edges=[[0, 0, 20], [W, 0, 20], [0, L, 20], [W, L, 20]]),
          cmd("solid.chamfer", name="FrontEdge", distance="edge_c", edges=[[W / 2, 0, H]]),
          cmd("solid.chamfer", name="BottomEdge", distance="edge_c", edges=[[W / 2, 0, 0]]),
          cmd("solid.split_body", name="Split", body="Shell",
              plane={"origin": [0, 0, SPLIT], "normal": [0, 0, 1]}),
          cmd("body.rename", body="Shell", name="FrontShell"),
          cmd("body.rename", body="Shell (1)", name="BottomTray")]
    F, T = "FrontShell", "BottomTray"
    # Hohlraeume
    c += rrect_sketch("TrayCavitySk", "floor_t", ["wall"])
    c += [cmd("solid.extrude", name="TrayCavity", sketch="TrayCavitySk", distance="split",
              operation="cut", targets=[T])]
    c += rrect_sketch("FrontCavitySk", "split - 1", ["wall"])
    c += [cmd("solid.extrude", name="FrontCavity", sketch="FrontCavitySk",
              distance="H - front_t - split + 1", operation="cut", targets=[F])]
    # Lippe (Schale unten) und Nut (Frontschale)
    c += rrect_sketch("LipSk", "split", ["lip_off", "lip_off + lip_w"])
    c += [cmd("solid.extrude", name="Lip", sketch="LipSk", distance="lip_h", operation="join",
              targets=[T])]
    c += rrect_sketch("GrooveSk", "split", ["lip_off - gap", "lip_off + lip_w + gap"])
    # Felder in der Front (0.6 tief, Rand 0.3 gefast)
    fx0, fx1, fy0, fy1, fr = FIELD_DISPLAY
    bx0_, bx1_, by0_, by1_, br = FIELD_BUTTONS
    wx, wy = WHEEL
    c += [cmd("sketch.create", plane="XY", offset="H", name="FieldsSk"),
          cmd("sketch.rectangle.rounded", p0=[fx0, Y(fy1)], p1=[fx1, Y(fy0)], radius=fr),
          cmd("sketch.rectangle.rounded", p0=[bx0_, Y(by1_)], p1=[bx1_, Y(by0_)], radius=br),
          cmd("sketch.circle.center", center=[wx, Y(wy)], diameter=FIELD_WHEEL_D),
          cmd("sketch.finish"),
          cmd("solid.extrude", name="Fields", sketch="FieldsSk", distance=FIELD_DEPTH,
              direction="negative", operation="cut", targets=[F]),
          cmd("solid.chamfer", name="FieldChamfer", distance=FIELD_CHAMFER, body=F,
              edges=[[(fx0 + fx1) / 2, Y(fy0), H], [(bx0_ + bx1_) / 2, Y(by0_), H],
                     [wx + FIELD_WHEEL_D / 2, Y(wy), H]])]
    ZF = H - FIELD_DEPTH   # Feldboden
    # --- Frontschale: Dome, Stege ---
    for i, (sx, sy) in enumerate(SCREWS, 1):
        c += [cyl(f"FrontBoss{i}", sx, sy, SPLIT, H - FRONT - SPLIT, BOSS_D, "join", F),
              web_box(sx, sy, SPLIT, H - FRONT, "join", F, f"FrontWeb{i}")]
    c += [cmd("solid.extrude", name="Groove", sketch="GrooveSk", distance="lip_h + gap",
              operation="cut", targets=[F])]
    for i, (sx, sy) in enumerate(SCREWS, 1):
        c += [cyl(f"InsertHole{i}", sx, sy, SPLIT - 1, 7.0, 4.0, "cut", F)]
    # Display
    cx = DISPLAY_CX
    c += [box("OledWindow", cx - OLED_WIN_W / 2, cx + OLED_WIN_W / 2, 16.85, 32.55, H - FRONT - 1, H + 1, "cut", F),
          box("HeaderPocket", cx + OLED_HDR[0], cx + OLED_HDR[1], 10.3, 13.3,
              H - FRONT - 0.5, H - FRONT + 0.8, "cut", F)]  # 0.8 tief in die Platte
    fw, fy0, fy1, fd = OLED_FLEX_RELIEF
    c += [box("OledFlexRelief", cx - fw, cx + fw, fy0, fy1, H - FRONT - 0.5, H - FRONT + fd, "cut", F)]
    for i, (px, py) in enumerate(OLED_PEGS, 1):
        pin_l = OLED_PCB_T + OLED_PIN_PAST   # durch die Modulplatine und 2.2 mm dahinter
        c += [cyl(f"OledShoulder{i}", px, py, H - FRONT - OLED_SHOULDER_H, OLED_SHOULDER_H, 5.0, "join", F),
              # Stift reicht bis 0.5 in die Platte: keine koplanaren Flaechen beim Vereinigen
              cyl(f"OledPin{i}", px, py, H - FRONT - OLED_SHOULDER_H - pin_l, pin_l + OLED_SHOULDER_H + 0.5,
                  2.8, "join", F)]
    # Fenster-Fase 2:1 als verjuengter Schnitt (die Fase mit zwei Abstaenden scheitert an den Ecken)
    cw, cd = WIN_CHAMFER
    c += [cmd("sketch.create", plane="XY", offset=ZF, name="WindowChamferSk"),
          cmd("sketch.rectangle.two_point", p0=[cx - OLED_WIN_W / 2 - cw, Y(32.55 + cw)],
              p1=[cx + OLED_WIN_W / 2 + cw, Y(16.85 - cw)]),
          cmd("sketch.finish"),
          cmd("solid.extrude", name="WindowChamfer", sketch="WindowChamferSk", distance=cd,
              direction="negative", taper=f"-{round(math.degrees(math.atan2(cw, cd)), 6)} deg",
              operation="cut", targets=[F])]
    # Taster, Handrad
    for i, (bx, by, _) in enumerate(BUTTONS, 1):
        c += [cyl(f"ButtonHole{i}", bx, by, H - FRONT - 1, FRONT + 2, BUTTON_HOLE_D, "cut", F)]
    wx, wy = WHEEL
    # Topf: Aussenzylinder von der Unterseite des Bodens bis 0,5 in die Frontplatte (keine koplanaren Flaechen)
    pot_d = WHEEL_POCKET_D + 2 * WHEEL_POT_WALL
    c += [cyl("WheelPot", wx, wy, WHEEL_POT_Z, H - FRONT + 0.5 - WHEEL_POT_Z, pot_d, "join", F)]
    c += [cyl("WheelCutout", wx, wy, WHEEL_POT_Z - 1, H - WHEEL_POT_Z + 2, WHEEL_CUTOUT_D, "cut", F)]
    for i, a in enumerate([0, 120, 240], 1):
        # 12 Uhr = zu den Tastern = kleineres Ticket-y
        hx = wx + PCD / 2 * math.sin(math.radians(a))
        hy = wy - PCD / 2 * math.cos(math.radians(a))
        c += [cyl(f"WheelHole{i}", round(hx, 4), round(hy, 4), WHEEL_POT_Z - 1, WHEEL_POT_FLOOR + 2,
                  WHEEL_HOLE_D, "cut", F)]
    # Taschen fuer Tasterkoepfe und Handrad-Front, Rand 0.3 gefast
    for i, (bx, by, _) in enumerate(BUTTONS, 1):
        c += [cyl(f"ButtonPocket{i}", bx, by, H - POCKET_DEPTH, POCKET_DEPTH + 1, BUTTON_POCKET_D, "cut", F)]
    c += [cyl("WheelPocket", wx, wy, WHEEL_SEAT_Z, H - WHEEL_SEAT_Z + 1, WHEEL_POCKET_D, "cut", F)]
    c += [cmd("solid.chamfer", name="PocketChamfer", distance=POCKET_CHAMFER, body=F,
              edges=[[bx + BUTTON_POCKET_D / 2, Y(by), ZF] for bx, by, _ in BUTTONS]
              + [[wx + WHEEL_POCKET_D / 2, Y(wy), ZF]])]
    # Ein/Aus-Schalter rechte Seitenwand
    sy, sz = SWITCH
    c += [cmd("solid.cylinder", name="SwitchHole", diameter=BUTTON_HOLE_D, height=WALL + 3,
              base=[W - WALL - 1.5, Y(sy), sz], axis="X", operation="cut", targets=[F])]
    # USB-C linke Seitenwand (Skizze auf YZ: x = CAD-Y, y = Z)
    (hw, hh), (rw_, rh_) = USB_HOLE, USB_RECESS
    c += [cmd("sketch.create", plane="YZ", offset=-1, name="UsbSk"),
          cmd("sketch.rectangle.rounded", p0=[Y(USB_Y + hw / 2), USB_Z - hh / 2],
              p1=[Y(USB_Y - hw / 2), USB_Z + hh / 2], radius=USB_HOLE_R),
          cmd("sketch.finish"),
          cmd("solid.extrude", name="UsbCutout", sketch="UsbSk", distance=WALL + 2,
              operation="cut", targets=[F]),
          # aussen gesenkt, damit ein umspritzter Stecker ganz einfaehrt
          cmd("sketch.create", plane="YZ", offset=-1, name="UsbRecessSk"),
          cmd("sketch.rectangle.rounded", p0=[Y(USB_Y + rw_ / 2), USB_Z - rh_ / 2],
              p1=[Y(USB_Y - rw_ / 2), USB_Z + rh_ / 2], radius=USB_R),
          cmd("sketch.finish"),
          cmd("solid.extrude", name="UsbRecess", sketch="UsbRecessSk", distance=1 + WALL - USB_REST,
              operation="cut", targets=[F])]
    # Tasche fuer den Schalterkopf in der rechten Wand (wie die Taster), Rand gefast
    c += [cmd("solid.cylinder", name="SwitchPocket", diameter=BUTTON_POCKET_D, height=POCKET_DEPTH + 1,
              base=[W - POCKET_DEPTH, Y(sy), sz], axis="X", operation="cut", targets=[F]),
          cmd("solid.chamfer", name="SwitchPocketChamfer", distance=POCKET_CHAMFER, body=F,
              edges=[[W, Y(sy) + BUTTON_POCKET_D / 2, sz]])]
    # Antennenmulde (Positionierhilfe) in der Innenflaeche der oberen Stirnwand
    rw, rh = ANT_RECESS
    c += [box("AntennaRecess", ANT_X - rw / 2, ANT_X + rw / 2, WALL - ANT_DEPTH, WALL + 0.5,
              ANT_Z - rh / 2, ANT_Z + rh / 2, "cut", F)]
    # Gravuren
    c += [cmd("sketch.create", plane="XY", offset=ZF, name="AxisLabelSk")]   # im Tastenfeld
    for bx, by, t in BUTTONS:
        c += [cmd("sketch.text", text=t, at=[bx, Y(by + LABEL_OFFSET) - AXIS_TEXT_H / 2],
                  height=AXIS_TEXT_H, align="center")]
    c += [cmd("sketch.finish"),
          cmd("solid.extrude", name="AxisLabels", sketch="AxisLabelSk", distance=AXIS_TEXT_DEPTH,
              direction="negative", operation="cut", targets=[F])]
    c += [cmd("sketch.create", plane="YZ", offset="W", name="SwitchLabelSk"),
          # linksbuendig zum Display hin (beim Handrad liegen die Griffrillen): beginnt LABEL_CLEAR
          # ausserhalb des Schalterrings; von rechts gesehen steht die Schrift rechts neben dem Schalter
          cmd("sketch.text", text="I/O", at=[Y(sy - SWITCH_KEEP_R - LABEL_CLEAR), sz - SWITCH_TEXT_H / 2],
              height=SWITCH_TEXT_H, align="left"),
          cmd("sketch.finish"),
          cmd("solid.extrude", name="SwitchLabel", sketch="SwitchLabelSk", distance=SWITCH_TEXT_DEPTH,
              direction="negative", operation="cut", targets=[F])]
    # Gravur "MPG", gemeinsame Grundlinie, Tinte waagerecht mittig
    base = Y(LOGO_CY + LOGO_CAP_H / 2)          # Unterkante (CAD-Y)
    c += [cmd("sketch.create", plane="XY", offset="H", name="LogoSk")]
    if USE_LOGO:
        # Private Fassung: Makera-"M" als Polygone, dahinter "PG" in fetter Schrift
        with open(LOGO_FILE) as f:
            logo = json.load(f)
        lw = logo["width"] * LOGO_CAP_H
        total = lw + LOGO_GAP + (PG_INK[1] - PG_INK[0])
        lx0 = W / 2 - total / 2
        for shp in logo["shapes"]:
            pts = [[round(lx0 + x * LOGO_CAP_H, 4), round(base + y * LOGO_CAP_H, 4)] for x, y in shp["points"]]
            c += [cmd("sketch.line", points=pts, closed=True, infer=False)]
        c += [cmd("sketch.text", text="PG", at=[round(lx0 + lw + LOGO_GAP - PG_INK[0], 4), base],
                  height=LOGO_CAP_H, font=PG_FONT, bold=True)]
    else:
        # Neutrale Fassung: "MPG" ganz in fetter Schrift
        c += [cmd("sketch.text", text="MPG", at=[round(W / 2 - (MPG_INK[0] + MPG_INK[1]) / 2, 4), base],
                  height=LOGO_CAP_H, font=PG_FONT, bold=True)]
    c += [cmd("sketch.finish"),
          cmd("solid.extrude", name="Logo", sketch="LogoSk", distance=LOGO_DEPTH,
              direction="negative", operation="cut", targets=[F])]
    # Griffrillen: Ausschluss aus den Lagen von Schalter, I/O-Gravur (rechts) und USB-C-Senkung (links)
    io_y = (sy - SWITCH_KEEP_R - LABEL_CLEAR - IO_TEXT_W, sy - SWITCH_KEEP_R - LABEL_CLEAR)
    keep = {"R": [(sy - SWITCH_KEEP_R, sy + SWITCH_KEEP_R), io_y],
            "L": [(USB_Y - USB_RECESS[0] / 2, USB_Y + USB_RECESS[0] / 2)]}
    for side, off, d in (("L", -1, "positive"), ("R", W + 1, "negative")):
        c += [cmd("sketch.create", plane="YZ", offset=off, name=f"Grip{side}Sk")]
        n = int(round((GRIP_Y[1] - GRIP_Y[0]) / GRIP_PITCH)) + 1
        for k in range(n):
            gy = GRIP_Y[0] + k * GRIP_PITCH
            if any(a - GRIP_KEEP - GRIP_W / 2 < gy < b + GRIP_KEEP + GRIP_W / 2 for a, b in keep[side]):
                continue
            c += [cmd("sketch.slot.overall", p0=[Y(gy), GRIP_Z[0]], p1=[Y(gy), GRIP_Z[1]], width=GRIP_W)]
        c += [cmd("sketch.finish"),
              cmd("solid.extrude", name=f"Grip{side}", sketch=f"Grip{side}Sk", distance=1 + GRIP_DEPTH,
                  direction=d, operation="cut", targets=[F])]
    # --- Bodenschale ---
    for i, (sx, sy) in enumerate(SCREWS, 1):
        c += [cyl(f"TrayFoot{i}", sx, sy, FLOOR, 3.5, 10.0, "join", T),
              cyl(f"TrayColumn{i}", sx, sy, 6.5, SPLIT - 6.5, 7.0, "join", T),
              web_box(sx, sy, FLOOR - 1, SPLIT, "join", T, f"TrayWeb{i}"),  # 1 mm in den Boden: keine koplanaren Flaechen
              cyl(f"Counterbore{i}", sx, sy, -1, 7.0, 6.5, "cut", T),
              cyl(f"ScrewHole{i}", sx, sy, -1, SPLIT + 2, 3.4, "cut", T)]
    for i, (px, py) in enumerate(STANDOFFS, 1):
        c += [cyl(f"Standoff{i}", px, py, FLOOR, 4.0, 6.0, "join", T),
              cyl(f"Pilot{i}", px, py, 1.5, 6.0, 2.5, "cut", T)]
    # Akkurahmen: Akku x 13.5..78.5, y 82..117, Spiel 0.5, Rippen 1.6 x 4
    bx0, bx1, by0, by1, cl, rw, rh = 13.5, 78.5, 82.0, 117.0, 0.5, 1.6, 4.0
    ix0, ix1, iy0, iy1 = bx0 - cl, bx1 + cl, by0 - cl, by1 + cl
    z1 = FLOOR + rh
    c += [box("RibTop", ix0 - rw, ix1 + rw, iy0 - rw, iy0, FLOOR, z1, "join", T),
          box("RibBottom", ix0 - rw, ix1 + rw, iy1, iy1 + rw, FLOOR, z1, "join", T),
          # links mit Luecke fuer das Klettband
          box("RibLeftA", ix0 - rw, ix0, iy0, STRAP_Y[0], FLOOR, z1, "join", T),
          box("RibLeftB", ix0 - rw, ix0, STRAP_Y[1], iy1, FLOOR, z1, "join", T),
          # rechts (Kabelseite) offen bis auf zwei 6-mm-Ecken
          box("RibRightA", ix1, ix1 + rw, iy0, iy0 + 6, FLOOR, z1, "join", T),
          box("RibRightB", ix1, ix1 + rw, iy1 - 6, iy1, FLOOR, z1, "join", T),
          box("StrapSlot1", *STRAP_SLOTS[0], *STRAP_Y, -1, FLOOR + 0.01, "cut", T),
          box("StrapSlot2", *STRAP_SLOTS[1], *STRAP_Y, -1, FLOOR + 0.01, "cut", T),
          # Kanal aussen im Boden zwischen den Schlitzen: das Klettband liegt buendig, 1.5 Boden bleiben
          box("StrapChannel", STRAP_SLOTS[0][0], STRAP_SLOTS[1][1], *STRAP_Y, -1, STRAP_CHANNEL_D, "cut", T)]
    return {"commands": c}


def assembly():
    """Platzhalter. Neue Koerper (operation new), jeweils mit body_name."""
    c = []

    def nb(name, x0, x1, y0, y1, z0, z1):
        return cmd("solid.box", name=name, body_name=name, length=round(x1 - x0, 4),
                   width=round(y1 - y0, 4), height=round(z1 - z0, 4), corner=[x0, Y(y1), z0],
                   operation="new")

    def nc(name, x, y, z0, h, d, axis="Z", base=None):
        return cmd("solid.cylinder", name=name, body_name=name, diameter=d, height=h,
                   base=base or [x, Y(y), z0], axis=axis, operation="new")

    c += [nb("P_PCB", 3.5, 81.5, 12, 78, 7, 8.6),
          nb("P_PCB_SMD", 3.5, 81.5, 14, 78, 4.6, 7),          # SMD/Pogo unten (2 mm + Luft); die oberen 2 mm Rand sind bauteilfrei
          nb("P_Sockets", 3.9, 63.9, 19.3, 44.7, 8.6, 19.6),    # Buchsenleisten-Stapel 11 mm
          nb("P_FireBeetle", 3.9, 63.9, 19.3, 44.7, 19.6, 24.5),
          nb("P_USBC", 2.5, 3.9, USB_Y - USB_JACK[0] / 2, USB_Y + USB_JACK[0] / 2, 21.295, 24.455),  # Ueberstand vor der FireBeetle-Kante, Lage wie am Probedruck (#34)
          nb("P_JST_J1J2", 66.0, 73.0, 19.0, 29.0, 8.6, 22.0),  # Akku-/Schalterstecker mit Kabelstecker
          nb("P_JST_rows", 3.5, 81.5, 51.0, 70.0, 8.6, 22.0),   # Steckerreihen J4..J9 mit Kabelsteckern
          nb("P_OLED_glass", DISPLAY_CX - OLED_GLASS_W / 2, DISPLAY_CX + OLED_GLASS_W / 2, 15.25, 38.25, 35.5, 37.0),
          nb("P_OLED_pcb", DISPLAY_CX - OLED_MOD_W / 2, DISPLAY_CX + OLED_MOD_W / 2, 10.0, 43.5, 34.3, 35.5),
          nb("P_OLED_wires", DISPLAY_CX - 6.0, DISPLAY_CX + 6.4, 10.0, 14.0, 30.3, 34.3),
          nb("P_Battery", 13.5, 78.5, 82, 117, 3, 13),
          # freie Antennenflaeche 55 x 19, 3 mm tief vor der Wand (Antennenplatine + Kleber + Kabelabgang)
          nb("P_AntennaZone", ANT_X - ANT_FREE[0] / 2, ANT_X + ANT_FREE[0] / 2, WALL, WALL + 3.0,
             ANT_Z - ANT_FREE[1] / 2, ANT_Z + ANT_FREE[1] / 2)]
    for i, (px, py) in enumerate(STANDOFFS, 1):  # unter den Montageloechern keine SMD-Teile
        c += [cmd("solid.cylinder", name=f"P_SmdKeepout{i}", diameter=7.0, height=3.0,
                  base=[px, Y(py), 4.5], operation="cut", targets=["P_PCB_SMD"])]
    for i, (px, py) in enumerate(OLED_PEGS, 1):
        c += [cmd("solid.cylinder", name=f"P_OledHole{i}", diameter=3.0, height=2.0,
                  base=[px, Y(py), 34.0], operation="cut", targets=["P_OLED_pcb"])]
    for i, (bx, by, t) in enumerate(BUTTONS, 1):
        c += [nc(f"P_Button{t}", bx, by, 14.0, 23.0, 12.0),   # Koerper 17 + Litzen 6
              nc(f"P_ButtonHead{t}", bx, by, H - POCKET_DEPTH + 0.3, 2.0, BUTTON_HEAD_D)]  # Kopf in der Tasche
    wx, wy = WHEEL
    # Koerper Oe 42,5 (STEP-Modell), 22 hinter der Auflage (Nutzer). Litzen (6 Stueck, Buendel ~3) treten
    # hinten aus und werden mit Biegeradius 3 abgeknickt: Knickzone unter dem ganzen Koerper 3 + 3 = 6 hoch
    # (Luft zum Boden 2,6), dann radial zu den Tastern hin aus dem Koerper heraus, zwischen Koerper und
    # Akkurippe (y 119,1) senkrecht hoch ueber den Akku samt Klettband (z 15) und ueber den Akku zur
    # Traegerplatine (Stecker J6, Platinenrand y 78). Kanal 6 breit, 3 hoch, mittig zwischen den Tastern Y und Z.
    body_z0 = WHEEL_SEAT_Z - 22.0                       # 11.6
    knick_z0 = body_z0 - WIRE_BUNDLE - WIRE_BEND_R      # 5.6
    run_z = (15.0, 15.0 + WIRE_BUNDLE)                  # ueber Akku (13) + Klettband (~1,5)
    rx0, rx1 = wx - 3.0, wx + 3.0                       # zwischen den Tastern Y und Z (Luecke x 42..50)
    c += [nc("P_WheelBody", wx, wy, body_z0, 22.0, WHEEL_BODY_D),
          nc("P_WheelWires", wx, wy, knick_z0, body_z0 - knick_z0, WHEEL_BODY_D),
          nb("P_WheelWiresOut", rx0, rx1, 121.0, wy, knick_z0, body_z0),
          nb("P_WheelWiresUp", rx0, rx1, 121.0, 121.0 + WIRE_BUNDLE + WIRE_BEND_R, knick_z0, run_z[1]),
          nb("P_WheelWiresRun", rx0, rx1, 78.0, 121.0, run_z[0], run_z[1]),
          nc("P_WheelDial", wx, wy, WHEEL_SEAT_Z + 0.3, 26.0, 60.0)]   # im Topf, 0.3 Luft
    sy, sz = SWITCH
    c += [nc("P_Switch", 0, 0, 0, 23.0, 12.0, axis="X", base=[W - WALL - 23.0, Y(sy), sz]),   # 17 + 6 Litzen
          nc("P_SwitchHead", 0, 0, 0, 2.0, BUTTON_HEAD_D, axis="X", base=[W - POCKET_DEPTH + 0.3, Y(sy), sz])]
    return {"commands": c}


if __name__ == "__main__":
    # Ohne Argument: enclosure.json + assembly_check.json, neutral. Mit --logo nur enclosure_logo.json
    # (private Fassung, git-ignoriert; die eingecheckten Ausgaben bleiben neutral, Ticket #50).
    args = sys.argv[1:]
    if args not in ([], ["--logo"]):
        sys.exit("Aufruf: python3 -I make_enclosure.py [--logo]")
    USE_LOGO = args == ["--logo"]
    if USE_LOGO and not os.path.exists(LOGO_FILE):
        sys.exit("--logo: logo_polygons.json fehlt (python3 -I trace_logo.py <logo.png>)")
    jobs = [("enclosure_logo.json", enclosure)] if USE_LOGO else [("enclosure.json", enclosure),
                                                                  ("assembly_check.json", assembly)]
    for fn, fun in jobs:
        with open(os.path.join(HERE, fn), "w") as f:
            json.dump(fun(), f, indent=1)
            f.write("\n")
    print("ok,", jobs[0][0], "Gravur:", "Makera-Logo (logo_polygons.json)" if USE_LOGO else "neutral \"MPG\"")
