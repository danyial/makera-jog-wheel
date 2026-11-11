#!/usr/bin/env python3
"""Generates the single-sided, millable carrier PCB for the handwheel (tickets #20, #25).

Run with KiCad's Python (pcbnew):
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
      hardware/generate_board.py

Copper is on B.Cu only (single-sided). Through-hole parts (FireBeetle sockets, JST-XH) are inserted
from the top and soldered on the copper side; SMD parts (0805, SOT-23) sit on the copper side.
All coordinates are in mm, top view, y pointing down. The routing below was planned by hand to be
crossing-free on one layer; see hardware/README.md for the topology. Pin assignment: include/pins.h.
"""
import os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "handrad_pcb", "handrad.kicad_pcb")
# Copy of the board with the vendor FireBeetle model, only for the assembly STEP (ticket #50, git-ignored).
OUT_VENDOR = os.path.join(HERE, "handrad_pcb", "handrad_vendor.kicad_pcb")
FPLIB = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"

# --- Parameters -----------------------------------------------------------------------------
# Compact layout (ticket #25): FireBeetle rotated 90 degrees (USB-C to the left, sticking 1 mm over
# the board edge), connectors in two rows along the bottom long side.
ROW = 22.5           # centre distance of the FireBeetle header rows (DFRobot footprint DFR0654: +-11.25)
FB_W, FB_L = 25.4, 60.0   # FireBeetle board (DFR0654)
FB_PIN1_FROM_TOP = 11.32  # left-row pin 1 (RST) from the USB edge (DFR0654: pin 1 at y = -18.68)
USB_OVERHANG = 1.4   # USB-C sticks out of the FireBeetle edge by this much (DFR0654 outline)
USB_PAST_EDGE = 1.0  # ... and out of the carrier edge by this much
P = 2.54             # header pitch
CX = USB_OVERHANG - USB_PAST_EDGE + FB_L / 2   # FireBeetle centre x
CY = 20.0                                      # FireBeetle centre y
BOARD_W, BOARD_H, CORNER_R = 78.0, 66.0, 3.0
W_SIG, W_PWR = 0.8, 1.0          # track widths (user request: 0.8 mm signals)
CLEARANCE = 0.4                  # mill: >= 0.4
PITCH = 1.4                      # lane pitch (>= 0.4 mm copper gap at these widths)
CHAMFER = 1.5                    # every 90 degree bend becomes two 45 degree bends
HOLE_INSET = 4.5                 # mounting holes, same distance from every corner
# Pogo pins to the FireBeetle's Li-ion pads (underside), relative to the left-row pins (user
# measurement); across the board ~2.4 mm inside the FireBeetle edge (photo, pads ~3.7 mm wide).
POGO_X_FROM_RIGHT_EDGE = 2.4
POGO_PAD, POGO_DRILL = 2.4, 0.8   # P50-J1 pogo pins: barrel 0.68 mm, snug fit for soldering
POGO_SPREAD = 0.5    # each pogo pin 0.5 mm outward from the measured spot (user, ticket #25): +1 mm apart
Y_ROW1, Y_ROW2, Y_BUS = 43.0, 54.0, 60.0   # connector rows and bottom GND bus

# FireBeetle header positions after the rotation: the former right row (VCC..D10, 14 pins) is the
# top row, the former left row (RST..RX, 18 pins) the bottom row; pin numbers keep their order.
Y_TOP = CY - ROW / 2
Y_BOT = CY + ROW / 2


def xb(k):  # bottom row (former left row) pin k = 1..18, RST .. RX
    return CX - (FB_L / 2 - FB_PIN1_FROM_TOP) + (k - 1) * P


def xt(k):  # top row (former right row) pin k = 1..14, VCC .. D10, starts 4 pins later
    return xb(k + 4)


board = pcbnew.BOARD()
mm = pcbnew.FromMM
nets = {}


def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]


def place(lib, name, ref, x, y, rot=0.0, bottom=False, value=""):
    fp = pcbnew.FootprintLoad(os.path.join(FPLIB, lib + ".pretty"), name)
    fp.SetReference(ref)
    fp.SetValue(value or name)
    board.Add(fp)
    fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    if rot:
        fp.SetOrientationDegrees(rot)
    if bottom:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        fp.Reference().SetVisible(False)
    return fp


MODELS = "${KIPRJMOD}/../models"


VENDOR_FB_STEP = os.path.join(HERE, "vendor", "DFR0654.step")  # SnapMagic model, private repo only (licence)


def add_model(fp, filename, rot=(0.0, 0.0, 0.0), offset=(0.0, 0.0, 0.0), path=None):
    m = pcbnew.FP_3DMODEL()
    m.m_Filename = path or f"{MODELS}/{filename}"
    m.m_Rotation = pcbnew.VECTOR3D(*rot)
    m.m_Offset = pcbnew.VECTOR3D(*offset)
    fp.Add3DModel(m)


def pad_xy(fp, num):
    p = fp.FindPadByNumber(str(num)).GetPosition()
    return (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y))


def setnet(fp, num, name):
    fp.FindPadByNumber(str(num)).SetNet(net(name))


def chamfer(pts):
    """Replaces each right-angle bend by a 45 degree chamfer (at most half of either segment)."""
    pts = [p for i, p in enumerate(pts) if i == 0 or abs(p[0] - pts[i - 1][0]) + abs(p[1] - pts[i - 1][1]) > 1e-6]
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        (ax, ay), (bx, by), (cx, cy) = pts[i - 1], pts[i], pts[i + 1]
        d1 = (bx - ax, by - ay)
        d2 = (cx - bx, cy - by)
        l1, l2 = (d1[0] ** 2 + d1[1] ** 2) ** 0.5, (d2[0] ** 2 + d2[1] ** 2) ** 0.5
        if l1 < 1e-6 or l2 < 1e-6 or abs(d1[0] * d2[0] + d1[1] * d2[1]) > 1e-6 * l1 * l2:
            out.append((bx, by))  # not a right angle
            continue
        c = min(CHAMFER, l1 / 2, l2 / 2)
        out.append((bx - d1[0] / l1 * c, by - d1[1] / l1 * c))
        out.append((bx + d2[0] / l2 * c, by + d2[1] / l2 * c))
    out.append(pts[-1])
    return out


def track(name, pts, w=W_SIG):
    pts = chamfer(pts)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if abs(x0 - x1) < 1e-6 and abs(y0 - y1) < 1e-6:
            continue
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(mm(x0), mm(y0)))
        t.SetEnd(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        t.SetWidth(mm(w))
        t.SetLayer(pcbnew.B_Cu)
        t.SetNet(net(name))
        board.Add(t)


def silk(text, x, y, size=1.0, layer=pcbnew.F_SilkS):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(size * 0.15))
    board.Add(t)


# --- Outline with rounded corners -----------------------------------------------------------
def edge_line(x0, y0, x1, y1):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(mm(x0), mm(y0)))
    s.SetEnd(pcbnew.VECTOR2I(mm(x1), mm(y1)))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(mm(0.1))
    board.Add(s)


def edge_arc(cx, cy, sx, sy, deg):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetCenter(pcbnew.VECTOR2I(mm(cx), mm(cy)))
    s.SetStart(pcbnew.VECTOR2I(mm(sx), mm(sy)))
    s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(deg, pcbnew.DEGREES_T), True)
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(mm(0.1))
    board.Add(s)


W, H, r = BOARD_W, BOARD_H, CORNER_R
edge_line(r, 0, W - r, 0)
edge_line(W, r, W, H - r)
edge_line(W - r, H, r, H)
edge_line(0, H - r, 0, r)
edge_arc(W - r, r, W - r, 0, 90)
edge_arc(W - r, H - r, W, H - r, 90)
edge_arc(r, H - r, r, H, 90)
edge_arc(r, r, 0, r, 90)

# --- Parts ----------------------------------------------------------------------------------
JST = "Connector_JST"
XH2 = "JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical"
XH4 = "JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical"
PH2 = "JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical"

# FireBeetle sockets, rotated so the pins run +x (pin k at xb(k) / xt(k)).
uB = place("Connector_PinSocket_2.54mm", "PinSocket_1x18_P2.54mm_Vertical", "U1A", xb(1), Y_BOT, rot=90,
           value="FireBeetle unten")
uT = place("Connector_PinSocket_2.54mm", "PinSocket_1x14_P2.54mm_Vertical", "U1B", xt(1), Y_TOP, rot=90,
           value="FireBeetle oben")
for k in range(1, 19):
    px, py = pad_xy(uB, k)
    assert abs(px - xb(k)) < 1e-3 and abs(py - Y_BOT) < 1e-3, ("bottom row", k, px, py)
for k in range(1, 15):
    px, py = pad_xy(uT, k)
    assert abs(px - xt(k)) < 1e-3 and abs(py - Y_TOP) < 1e-3, ("top row", k, px, py)

# FireBeetle itself: footprint without pads, only for the 3D model. Rotated 90 degrees so the USB end
# points left (to -x).
fb = pcbnew.FOOTPRINT(board)
fb.SetReference("U1")
fb.SetValue("FireBeetle 2 ESP32-UE")
fb.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_BOARD_ONLY)
fb.Reference().SetVisible(False)
fb.Value().SetVisible(False)
board.Add(fb)
fb.SetPosition(pcbnew.VECTOR2I(mm(CX), mm(CY)))
# The board file always carries the simplified model, so that it and the renders never depend on the
# licensed vendor model (ticket #50); the vendor model goes only into OUT_VENDOR (see the end).
add_model(fb, "firebeetle2_esp32ue.step")
fb.SetOrientationDegrees(90)

# FireBeetle header nets, board revision 2 (tickets #49; see include/pins.h and WIRING.md). The bottom row's
# 3V3 pin feeds only the encoder switch (R6, Q1) and is called +3V3_ENC, its GND pin (pin 4) only the wheel's
# 0V and is called GND_ENC: on one layer they cannot join the top-row 3V3/GND (the display lanes lie between
# them); on the FireBeetle they are the same rails.
# Top row: 6 = A4 (GPIO15), 7 = A3 (35), 9 = A1 (39, unused), 10 = A0 (36), 11 = D13 (GPIO12), 12 = D12 (GPIO4)
# (DFRobot pin table; physical order A4 ... A0 like the pins around it, H-1).
TOP_NETS = {2: "+3V3", 3: "GND", 4: "SCL", 5: "SDA", 6: "LED_Z", 7: "BTN_Z", 10: "BTN_A", 11: "LED_A",
            12: "PWR_SW"}
# Bottom row: 4 = GND, 6 = SCK (18), 7 = MOSI (23), 8 = MISO (19), 12 = D7 (13), 13 = D6 (14), 15 = D3 (26),
# 16 = D2 (25).
BOT_NETS = {2: "+3V3_ENC", 4: "GND_ENC", 6: "ENC_PWR", 7: "ENC_A", 8: "ENC_B", 12: "BTN_X", 13: "LED_X",
            15: "BTN_Y", 16: "LED_Y"}
for k, n in TOP_NETS.items():
    setnet(uT, k, n)
for k, n in BOT_NETS.items():
    setnet(uB, k, n)
POWER = {"+3V3", "+3V3_ENC", "GND", "GND_ENC", "ENC_VCC", "BAT+", "BAT-"}


def T(k):
    return (xt(k), Y_TOP)


def B(k):
    return (xb(k), Y_BOT)


def wire(name, pts):
    track(name, pts, W_PWR if name in POWER else W_SIG)


def xh(ref, n, x, y, names, value, fpname=None, rot=0.0):
    """Connector with pin 1 at (x, y); unrotated pins run +x in 2.5 mm steps."""
    fp = place(JST, fpname or (XH4 if n == 4 else XH2), ref, x, y, rot=rot, value=value)
    for i, name in enumerate(names, start=1):
        setnet(fp, i, name)
    return fp


def pogo(ref, x, y, name):
    """Single through-hole pad for a pogo pin pressing on a FireBeetle underside pad."""
    fp = pcbnew.FOOTPRINT(board)
    fp.SetReference(ref)
    fp.SetValue("Pogo " + name)
    board.Add(fp)
    pad = pcbnew.PAD(fp)
    pad.SetNumber("1")
    pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    pad.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
    pad.SetLayerSet(pad.PTHMask())
    pad.SetSize(pcbnew.VECTOR2I(mm(POGO_PAD), mm(POGO_PAD)))
    pad.SetDrillSize(pcbnew.VECTOR2I(mm(POGO_DRILL), mm(POGO_DRILL)))
    fp.Add(pad)
    fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    pad.SetNet(net(name))
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    add_model(fp, "pogo_p50_j1.step")
    return (x, y)


# Pogo pins to the Li-ion pads: along the FireBeetle measured from RST (bottom row pin 1), across
# POGO_X_FROM_RIGHT_EDGE inside the former right edge, which is now the top edge.
POGO_Y = CY - FB_W / 2 + POGO_X_FROM_RIGHT_EDGE
POGO_M_X = xb(1) + P / 2 - POGO_SPREAD          # "-": between RST and 3V3, pushed outward
POGO_P_X = xb(3) - 1.0 + POGO_SPREAD            # "+": top edge of the NC pad, pushed outward
pogo_minus = pogo("P1", POGO_M_X, POGO_Y, "BAT-")
pogo_plus = pogo("P2", POGO_P_X, POGO_Y, "BAT+")   # unswitched (soft power, ticket #49)

for i, (hx, hy) in enumerate([(HOLE_INSET, HOLE_INSET), (W - HOLE_INSET, HOLE_INSET),
                               (HOLE_INSET, H - HOLE_INSET), (W - HOLE_INSET, H - HOLE_INSET)]):
    hole = place("MountingHole", "MountingHole_3.2mm_M3", f"H{i + 1}", hx, hy)
    hole.Reference().SetVisible(False)
    hole.Value().SetVisible(False)

# Lanes (x) along the left edge: display bundle and the ENC_PWR loop around the bottom row start.
LX = {n: 2.8 + i * PITCH for i, n in enumerate(["+3V3", "GND", "SCL", "SDA", "ENC_PWR"])}
# Lanes (x) right of the FireBeetle rows, inner to outer (= top pins 6, 7, 10, 11, 12 from left to right).
RX = {n: xb(18) + 2.0 + i * PITCH for i, n in enumerate(["LED_Z", "BTN_Z", "BTN_A", "LED_A", "PWR_SW"])}

# Connectors (rev 2, ticket #49). Upper row (Y_ROW1): display, handwheel, on/off button. Lower row (Y_ROW2,
# next to the board edge towards the buttons): the four buttons X, Y, Z, A from left to right.
XP = 2.5
CON_PITCH = 13.5                 # pin 1 to pin 1 of neighbouring 4-pin connectors (courtyards 13.4 wide)
J4X = 4.0
jDISP = xh("J4", 4, J4X, Y_ROW1, ["+3V3", "GND", "SCL", "SDA"], "Display")
jENC = xh("J6", 4, J4X + CON_PITCH, Y_ROW1, ["GND_ENC", "ENC_A", "ENC_B", "ENC_VCC"], "Handrad")
J2X = 67.5                       # right end of the upper row: next to the on/off button in the right enclosure wall
jSW = xh("J2", 2, J2X, Y_ROW1, ["PWR_SW", "GND"], "Ein/Aus")
BX = 22.0                        # button X pin 1; X, Y, Z, A at CON_PITCH
jX = xh("J9", 4, BX, Y_ROW2, ["GND", "GND", "BTN_X", "LED_X"], "Taster X")
jY = xh("J8", 4, BX + CON_PITCH, Y_ROW2, ["GND", "GND", "BTN_Y", "LED_Y"], "Taster Y")
jZ = xh("J5", 4, BX + 2 * CON_PITCH, Y_ROW2, ["GND", "GND", "LED_Z", "BTN_Z"], "Taster Z")
jA = xh("J7", 4, BX + 3 * CON_PITCH, Y_ROW2, ["BTN_A", "LED_A", "GND", "GND"], "Taster A")
assert abs(pad_xy(jSW, 2)[0] - pad_xy(jA, 4)[0]) < 1e-6, "J2 GND drops straight onto J7 pin 4"
# Battery at the right edge directly above J2, pins running downward (short wide tracks
# along the top edge). J1 is JST-PH (the LiPo's own plug), so it cannot be confused with the XH connectors.
BAT_X = 66.0
J1_Y = 31.5                      # pin 1; directly above J2 (ticket #49): short battery cable to the right side
jBAT = xh("J1", 2, BAT_X, J1_Y, ["BAT+", "BAT-"], "Akku PH2.0", PH2, rot=-90)
assert pad_xy(jBAT, 2)[1] > pad_xy(jBAT, 1)[1], "battery connector pins must run downward"


def resistor(ref, x, y, nets_, value, vertical=False):
    """0805 on the copper side; nets_ = (left, right) or (top, bottom). Returns the pad positions."""
    fp = place("Resistor_SMD", "R_0805_2012Metric", ref, x, y, rot=90 if vertical else 0, bottom=True,
               value=value)
    key = (lambda p: p.GetPosition().y) if vertical else (lambda p: p.GetPosition().x)
    pads = sorted([fp.FindPadByNumber("1"), fp.FindPadByNumber("2")], key=key)
    for p, n in zip(pads, nets_):
        p.SetNet(net(n))
    return [(pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)) for p in pads]


R_HALF = 0.9125   # 0805 pad centre from the part centre

# Button pull-ups above the top row (GPIO35/36 are input-only), top pad on the 3V3 rail. There is no R3
# (button X is GPIO13 with the internal pull-up).
RAIL_Y = 5.6
rZ = resistor("R4", xt(7), RAIL_Y + R_HALF, ("+3V3", "BTN_Z"), "10k", vertical=True)
rA = resistor("R5", xt(10), RAIL_Y + R_HALF, ("+3V3", "BTN_A"), "10k", vertical=True)

# Encoder switch: R6 horizontal below bottom pins 1/2 (ENC_PWR left, 3V3 right), Q1 below it.
R6_Y = 35.0
R6_X = 14.0
r6 = resistor("R6", R6_X, R6_Y, ("ENC_PWR", "+3V3_ENC"), "100k")
# Q1 AO3401 on the copper side (mirrored): G top-left, S top-right, D bottom is the only orientation
# with D at the bottom. That fixes the order ENC_PWR | 3V3 from left to right, hence the ENC_PWR loop.
QX, QY = (r6[0][0] + r6[1][0]) / 2, R6_Y + 3.3
q1 = None
for rot in (0, 90, 180, 270):
    cand = place("Package_TO_SOT_SMD", "SOT-23", "Q1", QX, QY, rot=rot, bottom=True, value="AO3401")
    g, s, d = (pad_xy(cand, n) for n in (1, 2, 3))
    if g[1] < d[1] - 0.5 and s[1] < d[1] - 0.5 and g[0] < s[0]:
        q1 = cand
        break
    board.Remove(cand)
assert q1 is not None, "no SOT-23 orientation with G top-left, S top-right, D bottom"
setnet(q1, 1, "ENC_PWR")
setnet(q1, 2, "+3V3_ENC")
setnet(q1, 3, "ENC_VCC")
qG, qS, qD = pad_xy(q1, 1), pad_xy(q1, 2), pad_xy(q1, 3)

# Encoder pull-ups below J6 A and B, bottom pads on the switched supply ENC_VCC, which runs below J6 from Q1
# to J6 V+ (pin 4). The wheel's 0V (J6 pin 1) comes from above, from FireBeetle bottom pin 4 (GND_ENC).
ENC_VCC_Y = 47.6
r1 = resistor("R1", pad_xy(jENC, 2)[0], ENC_VCC_Y - R_HALF, ("ENC_A", "ENC_VCC"), "10k", vertical=True)
r2 = resistor("R2", pad_xy(jENC, 3)[0], ENC_VCC_Y - R_HALF, ("ENC_B", "ENC_VCC"), "10k", vertical=True)

# --- Routing (B.Cu) -------------------------------------------------------------------------
# Battery: BAT- along the top edge (outermost), BAT+ just inside it, both to J1 on the right.
BAT_M_Y, BAT_P_Y, BAT_M_X = 2.2, 3.7, 68.5
j1m = pad_xy(jBAT, 2)
wire("BAT-", [pogo_minus, (POGO_M_X, BAT_M_Y), (BAT_M_X, BAT_M_Y), (BAT_M_X, j1m[1]), j1m])
wire("BAT+", [pogo_plus, (POGO_P_X, BAT_P_Y), (BAT_X, BAT_P_Y), pad_xy(jBAT, 1)])

# 3V3 rail above the top row: top pin 2 up, right through R4 to R5.
wire("+3V3", [T(2), (xt(2), RAIL_Y), (xt(10), RAIL_Y), rA[0]])
for rr, k, n in ((rZ, 7, "BTN_Z"), (rA, 10, "BTN_A")):
    wire(n, [rr[1], T(k)])

# Display: top pins 2..5 inward, left under the FireBeetle, down the left edge, then right into J4 from above.
# SDA (innermost lane, rightmost pad) turns first, below the ENC_PWR loop.
for i, (k, n) in enumerate(((2, "+3V3"), (3, "GND"), (4, "SCL"), (5, "SDA"))):
    level = 12.0 + i * 1.5
    jog = 40.6 - i * PITCH
    px, py = pad_xy(jDISP, i + 1)
    wire(n, [T(k), (xt(k), level), (LX[n], level), (LX[n], jog), (px, jog), (px, py)])

# Encoder switch. ENC_PWR loops: bottom pin 6 up under the FireBeetle, left past RST, down to R6.
ENC_PWR_Y = 28.0
wire("ENC_PWR", [B(6), (xb(6), ENC_PWR_Y), (LX["ENC_PWR"], ENC_PWR_Y), (LX["ENC_PWR"], R6_Y), r6[0]])
wire("ENC_PWR", [r6[0], qG])
wire("+3V3_ENC", [B(2), r6[1]])
wire("+3V3_ENC", [r6[1], qS])
xv, yv = pad_xy(jENC, 4)
wire("ENC_VCC", [qD, (qD[0], ENC_VCC_Y), (xv, ENC_VCC_Y), (xv, yv)])
g6 = pad_xy(jENC, 1)
wire("GND_ENC", [B(4), (xb(4), Y_BOT + 1.75), (g6[0], Y_BOT + 1.75 + (xb(4) - g6[0])), g6])
ENC_A_Y, ENC_B_Y = 34.0, 35.4
wire("ENC_A", [B(7), (xb(7), ENC_A_Y), (r1[0][0], ENC_A_Y), r1[0]])     # through J6 pin 2
wire("ENC_B", [B(8), (xb(8), ENC_B_Y), (r2[0][0], ENC_B_Y), r2[0]])     # through J6 pin 3

# Buttons X and Y from the bottom row, down between J6 and J2, then left in the band between the rows into
# the lower row from above. The line from the leftmost source runs highest (no crossings).
BAND = [45.6 + i * PITCH for i in range(4)]
for (k, fp, pin), y in zip(((12, jX, 3), (13, jX, 4), (15, jY, 3), (16, jY, 4)), BAND):
    px, py = pad_xy(fp, pin)
    wire(BOT_NETS[k], [B(k), (xb(k), y), (px, y), (px, py)])

# Right group: top pins fan out under the FireBeetle (rightmost pin highest) to lanes right of the rows.
# Z: the two inner lanes drop into J5 (lower row) with a 45 degree jog to the left.
for k, n, level, turn in ((6, "LED_Z", 18.0, 48.0), (7, "BTN_Z", 16.5, 49.4)):
    px, py = pad_xy(jZ, 3 if n == "LED_Z" else 4)
    lx = RX[n]
    wire(n, [T(k), (xt(k), level), (lx, level), (lx, turn), (px, turn + (lx - px)), (px, py)])
# A: the next two lanes turn right in the band (outer lane higher) into J7 pins 1/2 from above.
for k, n, level, turn, pin in ((10, "BTN_A", 15.0, 47.4, 1), (11, "LED_A", 13.5, 46.0, 2)):
    px, py = pad_xy(jA, pin)
    wire(n, [T(k), (xt(k), level), (RX[n], level), (RX[n], turn), (px, turn), (px, py)])
# On/off button: the outermost lane turns right above everything into J2 pin 1 (upper row, right end).
PWR_SW_IN_Y = 39.0
s1 = pad_xy(jSW, 1)
wire("PWR_SW", [T(12), (xt(12), 12.0), (RX["PWR_SW"], 12.0), (RX["PWR_SW"], PWR_SW_IN_Y),
                (s1[0], PWR_SW_IN_Y), s1])

# GND: J4 GND straight down to the bus along the bottom edge, the bus to J7's last GND pin; J2 GND drops onto
# that pin; every other button GND pin joins the bus from above.
g4 = pad_xy(jDISP, 2)
g7 = pad_xy(jA, 4)
wire("GND", [g4, (g4[0], Y_BUS), (g7[0], Y_BUS), g7])
wire("GND", [pad_xy(jSW, 2), g7])
for fp, k in ((jX, 1), (jX, 2), (jY, 1), (jY, 2), (jZ, 1), (jZ, 2), (jA, 3)):
    gx, gy = pad_xy(fp, k)
    wire("GND", [(gx, gy), (gx, Y_BUS)])

# --- Labels (top silkscreen) ----------------------------------------------------------------
# Large enough to read on the renders and on the milled board: pin names 0.8 mm, connector names 1.2 mm.
PIN_TXT, NAME_TXT = 0.8, 1.2


def label_row(fp, names, y, size=PIN_TXT):
    for i, n in enumerate(names, start=1):
        silk(n, pad_xy(fp, i)[0], y, size)


def label_name(fp, text, y, size=NAME_TXT):
    fp.Reference().SetVisible(False)   # the label carries the reference
    xs = [p.GetPosition().x for p in fp.Pads()]
    x = pcbnew.ToMM((min(xs) + max(xs)) // 2)
    silk(text, x, y, size)


ROW1_PINS, ROW1_NAME = Y_ROW1 - 3.6, Y_ROW1 - 5.3      # above the upper row
ROW2_PINS, ROW2_NAME = Y_ROW2 + 4.6, Y_ROW2 + 6.3      # below the lower row
for fp, pins, name in ((jDISP, ["3V3", "G", "SCL", "SDA"], "J4 Display"),
                       (jENC, ["0V", "A", "B", "V+"], "J6 Handrad"),
                       (jSW, ["SW", "G"], "J2 Ein/Aus")):
    label_row(fp, pins, ROW1_PINS)
    label_name(fp, name, ROW1_NAME)
for fp, pins, name in ((jX, ["G", "G", "SIG", "LED"], "J9 Taster X"),
                       (jY, ["G", "G", "SIG", "LED"], "J8 Taster Y"),
                       (jZ, ["G", "G", "LED", "SIG"], "J5 Taster Z"),
                       (jA, ["SIG", "LED", "G", "G"], "J7 Taster A")):
    label_row(fp, pins, ROW2_PINS)
    label_name(fp, name, ROW2_NAME)
for i, n in enumerate(["B+", "B-"], start=1):
    silk(n, BAT_X + 5.6, pad_xy(jBAT, i)[1], PIN_TXT)
jBAT.Reference().SetVisible(False)
silk("J1 Akku", BAT_X + 6.2, J1_Y - 2.2, NAME_TXT)
silk("Makera Handrad Rev. 2", W / 2, H - 2.4, 1.2)
s = pcbnew.PCB_TEXT(board)
s.SetText("USB")
s.SetPosition(pcbnew.VECTOR2I(mm(2.0), mm(CY)))
s.SetLayer(pcbnew.F_SilkS)
s.SetTextSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
s.SetTextThickness(mm(0.15))
s.SetTextAngleDegrees(90)
board.Add(s)

# Milled boards have no plating: enlarge pads for a sturdy annular ring (>= 0.4 mm) while keeping
# >= 0.4 mm between neighbouring pins. Drill 1.0 mm everywhere except JST-PH (0.8 mm, 2.0 mm pitch).
PAD_RULES = {  # footprint name -> (pad w, pad h, drill)
    "PinSocket_1x18_P2.54mm_Vertical": (2.0, 2.0, 1.0),
    "PinSocket_1x14_P2.54mm_Vertical": (2.0, 2.0, 1.0),
    XH2: (2.0, 2.4, 1.0),
    XH4: (2.0, 2.4, 1.0),
    PH2: (1.6, 2.4, 0.8),
}
for fp in board.GetFootprints():
    rule = PAD_RULES.get(str(fp.GetFPID().GetLibItemName()))
    if rule is None:
        continue
    w, h, d = rule
    for pad in fp.Pads():
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
            pad.SetSize(pcbnew.VECTOR2I(mm(w), mm(h)))
            pad.SetDrillSize(pcbnew.VECTOR2I(mm(d), mm(d)))

# --- Rules ----------------------------------------------------------------------------------
ds = board.GetDesignSettings()
ds.m_TrackMinWidth = mm(0.4)
ds.m_MinClearance = mm(CLEARANCE)
nc = ds.m_NetSettings.GetDefaultNetclass()
nc.SetClearance(mm(CLEARANCE))
nc.SetTrackWidth(mm(W_SIG))

os.makedirs(os.path.dirname(OUT), exist_ok=True)
board.Save(OUT)
print("saved", OUT)

# Assembly STEP with the official DFRobot/SnapMagic model (hardware/vendor/, private repository only):
# Y-up, origin at the FireBeetle PCB underside, male headers included. Rotate Y-up to Z-up and lift it onto
# the female sockets (8.5 + 2.5 mm). export.sh exports fab/handrad.step from this copy and deletes it.
if os.path.exists(OUT_VENDOR):
    os.remove(OUT_VENDOR)
if os.path.exists(VENDOR_FB_STEP):
    fb.Models().clear()
    add_model(fb, "", rot=(-90.0, 0.0, 0.0), offset=(0.0, 0.0, 11.0), path="${KIPRJMOD}/../vendor/DFR0654.step")
    board.Save(OUT_VENDOR)
    print("saved", OUT_VENDOR)
