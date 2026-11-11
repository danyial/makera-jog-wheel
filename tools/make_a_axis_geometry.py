#!/usr/bin/env python3
"""Rotary axis module (A axis) for the 3D view, from Makera's machine model (ticket #48, SPEC 9.1).

Source: https://github.com/MakeraInc/CarveraProfiles/tree/main/Machine_Design_Files,
file Makera-Z1_Simplified_4Axis.step (public, no licence: the STEP is committed in the private repository
under hardware/vendor/a_axis/ and left out of the public export, ticket #50; only the derived dimensions
below are exported). Call (the STEP is downloaded and untrusted, hence isolated):

    python3 -I tools/make_a_axis_geometry.py hardware/vendor/a_axis/Makera-Z1_Simplified_4Axis.step \
        > include/a_axis_geometry.h

Reads the file as text only (own mini parser, no CAD package, nothing from the vendor folder is
executed). The model is an assembly: the transformations of all occurrences are applied, so every
part is in one frame. In that frame the spoilboard ("Z1-MDF-v2.1"), the anchor plate ("Anchor1") and
the module ("4th Axis") all hang below "Y Axis", i.e. they move together; the pose of the machine
axes in the model does not matter.

Registration: output is relative to the front left corner of the spoilboard (smallest x and y of the
MDF part), like include/spoilboard_geometry.h, and to the board top. The page puts the board corner at
the measured MPos (ticket #40), so the module lands in MPos without a second calibration. The script
checks the chain: the origin of the "Anchor1" part (= anchor 1 of the machine) must sit 15/15 mm
inside the board corner, as measured on the machine (ticket #40).

Simplification (the 3D view needs a few primitives, not the CAD): rail, headstock and tailstock as
boxes (T profile: upright and foot), motor housing as boxes, chuck as cylinders, four jaws as boxes,
tailstock quill as cylinder with a cone. The stock is not in the model, the view adds it.
"""
import hashlib
import math
import re
import sys

REF = re.compile(r"#(\d+)")
NUM = re.compile(r"[-+]?\d+\.?\d*(?:[eE][-+]?\d+)?")
NAME = re.compile(r"'([^']*)'")
EPS = 0.01


def die(msg):
    sys.exit("make_a_axis_geometry: " + msg)


def parse(path):
    """Entities by number, plus the git blob id of the file (as GitHub lists it) and the STEP time stamp."""
    with open(path, "rb") as fh:
        raw = fh.read()
    blob = hashlib.sha1(b"blob %d\0" % len(raw) + raw).hexdigest()
    text = raw.decode("latin-1")
    stamp = re.search(r"FILE_NAME\s*\(.*?'[^']*'\s*,.*?'([^']*)'", text, re.S)
    data = text.split("DATA;", 1)[1].rsplit("ENDSEC;", 1)[0]
    ents = {}
    for stmt in re.split(r";\s*\n", data):  # statements span lines; complex entities start with "("
        m = re.match(r"#(\d+)\s*=\s*(.*)$", stmt.strip(), re.S)
        if m:
            ents[int(m.group(1))] = re.sub(r"\s+", " ", m.group(2))
    return ents, blob, stamp.group(1) if stamp else "?"


class Step:
    def __init__(self, ents):
        self.e = ents

    def kind(self, i):
        s = self.e[i]
        return "COMPLEX" if s.startswith("(") else s.split("(", 1)[0].strip()

    def refs(self, i):
        return [int(r) for r in REF.findall(self.e[i])]

    def vec(self, i):  # CARTESIAN_POINT / DIRECTION: name first, then the coordinate list
        inner = self.e[i].split("(", 2)[2]
        return [float(v) for v in NUM.findall(inner)[:3]]

    def placement(self, i):  # AXIS2_PLACEMENT_3D -> 4x4 matrix
        r = self.refs(i)
        o = self.vec(r[0])
        z = unit(self.vec(r[1])) if len(r) > 1 else [0.0, 0.0, 1.0]
        x = self.vec(r[2]) if len(r) > 2 else [1.0, 0.0, 0.0]
        x = unit([a - dot(x, z) * b for a, b in zip(x, z)])
        y = cross(z, x)
        return [[x[0], y[0], z[0], o[0]], [x[1], y[1], z[1], o[1]], [x[2], y[2], z[2], o[2]], [0, 0, 0, 1]]


def dot(a, b):
    return sum(p * q for p, q in zip(a, b))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def unit(a):
    n = math.sqrt(dot(a, a))
    return [v / n for v in a]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def inv(m):  # rigid transform
    r = [[m[j][i] for j in range(3)] for i in range(3)]
    t = [-sum(r[i][k] * m[k][3] for k in range(3)) for i in range(3)]
    return [r[0] + [t[0]], r[1] + [t[1]], r[2] + [t[2]], [0, 0, 0, 1]]


def ap(m, p):
    return [sum(m[i][k] * p[k] for k in range(3)) + m[i][3] for i in range(3)]


def apd(m, d):
    return [sum(m[i][k] * d[k] for k in range(3)) for i in range(3)]


IDENT = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]


def assembly(s):
    """Product name -> (shape representation, global transform), plus representation -> breps."""
    pd_name, pd_rep, geom, kids = {}, {}, {}, {}
    for i in s.e:
        k = s.kind(i)
        if k == "PRODUCT_DEFINITION":
            pd_name[i] = NAME.findall(s.e[i])[1]
        elif k == "SHAPE_DEFINITION_REPRESENTATION":
            pds, rep = s.refs(i)[:2]
            tgt = s.refs(pds)[0]
            if s.kind(tgt) == "PRODUCT_DEFINITION":
                pd_rep[tgt] = rep
        elif k == "SHAPE_REPRESENTATION_RELATIONSHIP":
            a, b = s.refs(i)[:2]
            geom.setdefault(a, []).append(b)
        elif k == "CONTEXT_DEPENDENT_SHAPE_REPRESENTATION":
            # (REPRESENTATION_RELATIONSHIP(child, parent) ..._WITH_TRANSFORMATION(idt)): idt maps the
            # placement item 1 (in the child) onto item 2 (in the parent).
            rr = s.refs(i)[0]
            child, parent, idt = s.refs(rr)[:3]
            it1, it2 = s.refs(idt)[:2]
            kids.setdefault(parent, []).append((child, mul(s.placement(it2), inv(s.placement(it1)))))
    rep_name = {rep: pd_name[pd] for pd, rep in pd_rep.items()}
    children = {c for lst in kids.values() for c, _ in lst}
    parts = {}

    def walk(rep, t, parent):
        name = rep_name.get(rep, "?")
        if name in parts:
            die("Teil %r kommt mehrfach vor" % name)
        parts[name] = (rep, t, parent)
        for c, tc in kids.get(rep, []):
            walk(c, mul(t, tc), name)

    for rep in rep_name:
        if rep not in children:
            walk(rep, IDENT, None)
    return parts, geom


class Body:
    """Vertices and circles of one solid in the global frame."""

    def __init__(self, s, brep, t):
        self.name = NAME.findall(s.e[brep])[0]
        self.v, self.c = [], []  # points; (centre, normal, radius)
        seen, stack = set(), [brep]
        while stack:
            j = stack.pop()
            if j in seen:
                continue
            seen.add(j)
            k = s.kind(j)
            if k == "VERTEX_POINT":
                self.v.append(ap(t, s.vec(s.refs(j)[0])))
            elif k == "CIRCLE":
                pl = s.refs(j)[0]
                rad = float(NUM.findall(s.e[j].rsplit(",", 1)[1])[0])
                r = s.refs(pl)
                self.c.append((ap(t, s.vec(r[0])), apd(t, s.vec(r[1])), rad))
            elif k not in ("CARTESIAN_POINT", "DIRECTION", "VECTOR", "AXIS2_PLACEMENT_3D"):
                stack.extend(s.refs(j))

    def box(self):
        return bbox(self.v)


def bbox(pts):
    return [min(p[a] for p in pts) for a in range(3)], [max(p[a] for p in pts) for a in range(3)]


def bodies(s, parts, geom, name):
    if name not in parts:
        die("Teil %r fehlt im Modell" % name)
    rep, t, _ = parts[name]
    out = []
    for g in geom.get(rep, []):
        for it in s.refs(g):
            if s.kind(it) in ("MANIFOLD_SOLID_BREP", "BREP_WITH_VOIDS"):
                out.append(Body(s, it, t))
    if not out:
        die("Teil %r ohne Koerper" % name)
    return out


def one(s, parts, geom, name):
    b = bodies(s, parts, geom, name)
    if len(b) != 1:
        die("Teil %r: %d Koerper, erwartet 1" % (name, len(b)))
    return b[0]


def tee(pts):
    """T profile standing on the rail (headstock, tailstock): upright = width of the upper half,
    foot = full width up to the highest vertex outside the upright."""
    lo, hi = bbox(pts)
    zmid = (lo[2] + hi[2]) / 2
    up = [p for p in pts if p[2] > zmid]
    ylo, yhi = min(p[1] for p in up), max(p[1] for p in up)
    foot_top = max(p[2] for p in pts if p[1] < ylo - EPS or p[1] > yhi + EPS)
    return [(lo[0], hi[0], ylo, yhi, lo[2], hi[2]), (lo[0], hi[0], lo[1], hi[1], lo[2], foot_top)]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    ents, blob, stamp = parse(sys.argv[1])
    s = Step(ents)
    parts, geom = assembly(s)

    # Spoilboard: corner and top in the model frame.
    board = one(s, parts, geom, "Z1-MDF-v2.1")
    (bx0, by0, bz0), (bx1, by1, bz1) = board.box()
    if abs(bx1 - bx0 - 206) > EPS or abs(by1 - by0 - 206) > EPS or abs(bz1 - bz0 - 6) > EPS:
        die("Platte nicht 206 x 206 x 6 mm")
    anchor = ap(parts["Anchor1"][1], [0, 0, 0])
    adx, ady = anchor[0] - bx0, anchor[1] - by0
    if abs(adx - 15) > 0.05 or abs(ady - 15) > 0.05:
        die("Anker 1 liegt %.2f/%.2f mm in der Plattenecke, gemessen 15/15 (Ticket #40)" % (adx, ady))
    # Board, anchor plate and module must ride on the same carrier (they move together).
    if len({parts[n][2] for n in ("Z1-MDF-v2.1", "Anchor1", "4th Axis")}) != 1:
        die("Platte, Anker und Modul haengen nicht an derselben Baugruppe")
    if parts["Spindle Chuck"][2] != "4th Axis" or parts["Tailstock"][2] != "4th Axis":
        die("Futter oder Reitstock nicht im Modul")

    # Rotation axis: the chuck circles share one centre line parallel to X.
    chuck = bodies(s, parts, geom, "Spindle Chuck")
    ax_c = [c for b in chuck for c in b.c if abs(abs(c[1][0]) - 1) < 1e-6]
    # The centre line is the one most circles share (the jaw profiles have arcs off the axis).
    count = {}
    for c in ax_c:
        key = (round(c[0][1], 2), round(c[0][2], 2))
        count[key] = count.get(key, 0) + 1
    (ay, az), hits = max(count.items(), key=lambda kv: kv[1])
    if hits < 10:
        die("Futterachse nicht eindeutig")
    ring = [c for c in ax_c if math.hypot(c[0][1] - ay, c[0][2] - az) < EPS]
    ay = sum(c[0][1] for c in ring) / len(ring)
    az = sum(c[0][2] for c in ring) / len(ring)
    tail = one(s, parts, geom, "Tailstock")
    tail_ax = [c for c in tail.c if abs(abs(c[1][0]) - 1) < 1e-6 and math.hypot(c[0][1] - ay, c[0][2] - az) < EPS]
    if len(tail_ax) < 4:
        die("Reitstock nicht auf der Futterachse")

    def rel(lo, hi):  # model box -> board-local x, axis-relative y and z
        return (lo[0] - bx0, hi[0] - bx0, lo[1] - ay, hi[1] - ay, lo[2] - az, hi[2] - az)

    boxes = []
    rail = one(s, parts, geom, "MZ1--V20003_1")
    lo, hi = rail.box()
    if abs(lo[2] - bz1) > EPS:
        die("Schiene liegt nicht auf der Platte")
    boxes.append(rel(lo, hi))
    head = one(s, parts, geom, "MZ1--V20005_1")
    for b in tee(head.v):
        boxes.append(rel(b[0::2], b[1::2]))
    # Motor housing: back plate of the bracket (MZ1--V30002_1, up to its first step in x) together
    # with the cover behind it (MZ1--V30001_1), and the block of the bracket in front of the axis.
    brk = one(s, parts, geom, "MZ1--V30002_1")
    cover = one(s, parts, geom, "MZ1--V30001_1")
    bx_min = brk.box()[0][0]
    xs = min(p[0] for p in brk.v if p[0] > bx_min + 0.5)
    back = cover.v + [p for p in brk.v if p[0] <= xs + EPS]
    lo, hi = bbox(back)
    boxes.append(rel(lo, hi))
    blk = [p for p in brk.v if p[0] > xs + EPS]
    lo, hi = bbox(blk)
    boxes.append(rel([xs, lo[1], lo[2]], hi))
    # Tailstock: body (T profile) behind the quill; quill as cylinder, live centre as cone.
    tlo = tail.box()[0]
    xb = min(p[0] for p in tail.v if abs(p[1] - ay) > 10)
    for b in tee([p for p in tail.v if p[0] >= xb - EPS]):
        boxes.append(rel(b[0::2], b[1::2]))
    quill = [c for c in tail_ax if c[0][0] < xb - EPS]
    xq = min(c[0][0] for c in quill)
    rq = max(c[2] for c in quill)
    rt = min(c[2] for c in quill if abs(c[0][0] - xq) < EPS)

    # Chuck: bodies with circles on the axis are cylinders (radius = largest), the others are jaws.
    cyl, jaws = [], []
    for b in chuck:
        lo, hi = b.box()
        cy, cz = (lo[1] + hi[1]) / 2 - ay, (lo[2] + hi[2]) / 2 - az
        on = [c[2] for c in b.c if abs(abs(c[1][0]) - 1) < 1e-6 and math.hypot(c[0][1] - ay, c[0][2] - az) < EPS]
        if on:
            cyl.append((lo[0] - bx0, hi[0] - bx0, max(on)))
        else:
            u = unit([0, cy, cz])
            rad = [dot([0, p[1] - ay, p[2] - az], u) for p in b.v]
            tan = [(p[1] - ay) * -u[2] + (p[2] - az) * u[1] for p in b.v]
            jaws.append((lo[0] - bx0, hi[0] - bx0, min(rad), max(rad), max(tan) - min(tan),
                         math.degrees(math.atan2(u[2], u[1])) % 360))
    if len(jaws) < 2:
        die("Backen nicht gefunden")
    jaws.sort(key=lambda j: j[5])
    n = len(jaws)
    for k, j in enumerate(jaws):
        if abs((j[5] - jaws[0][5]) - k * 360 / n) > 1:
            die("Backen nicht gleichmaessig verteilt")
    j0 = [sum(j[i] for j in jaws) / n for i in range(5)]
    jx0 = min(j[0] for j in jaws)

    def q(v):  # 0.1 mm
        return int(round(v * 10))

    def fmt(seq):
        return ",".join(str(q(v)) for v in seq)

    a_y, a_z = ay - by0, az - bz1
    out = {
        "y": q(a_y), "z": q(a_z),
        "b": "[" + ",".join("[%s]" % fmt(b) for b in boxes) + "]",
        "s": "[" + ",".join("[%s]" % fmt(c) for c in cyl) + "]",
        "j": "[%s,%d,%d]" % (fmt([jx0, max(j[1] for j in jaws)] + j0[2:5]), n, int(round(jaws[0][5]))),
        "q": "[%s]" % fmt([xq - bx0, xb - bx0, rq, tlo[0] - bx0, rt]),
    }
    js = "{" + ",".join("%s:%s" % (k, v) for k, v in out.items()) + "}"
    print("#pragma once")
    print("// Generated by tools/make_a_axis_geometry.py (ticket #48). Do not edit by hand.")
    print("// Source: Makera-Z1_Simplified_4Axis.step, https://github.com/MakeraInc/CarveraProfiles/tree/main/")
    print("// Machine_Design_Files (Makera, public, no licence, not included; downloaded 2026-11-11), git")
    print("// blob %s, STEP time stamp %s." % (blob, stamp))
    print("// Derived dimensions from Makera's public CAD file, simplified (see README, Trademarks).")
    print("// Registration: same origin as spoilboard_geometry.h (front left board corner), anchor 1 of the model")
    print("// sits %.2f/%.2f mm inside it (measured 15/15, ticket #40). Units 0.1 mm." % (adx, ady))
    print("// Rotation axis parallel to X, %.2f mm behind the board corner, %.2f mm above the board top." % (a_y, a_z))
    print("// JS object literal: y/z = axis (board-local y, height above the board top); x values board-local,")
    print("// y/z values of the parts relative to the axis.")
    print("// b = fixed boxes [x0,x1,y0,y1,z0,z1]: rail, headstock upright and foot, motor housing back and")
    print("//     front block, tailstock upright and foot;")
    print("// s = chuck cylinders [x0,x1,r] (turn with A); j = jaws [x0,x1,r_in,r_out,width,count,angle_deg];")
    print("// q = tailstock quill and live centre [x0,x1,r,tip_x,r_tip_base].")
    print('#define A_AXIS_JS "%s"' % js)
    sys.stderr.write("Achse: y %.2f mm ab Plattenecke, %.2f mm ueber Plattenoberseite; Futterbacken %.2f, "
                     "Spitze %.2f mm ab Plattenecke (x); %d Quader, %d Futterzylinder, %d Backen\n"
                     % (a_y, a_z, max(j[1] for j in jaws) - bx0, tlo[0] - bx0, len(boxes), len(cyl), n))


if __name__ == "__main__":
    main()
