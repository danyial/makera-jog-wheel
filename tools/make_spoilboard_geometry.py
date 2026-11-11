#!/usr/bin/env python3
"""Spoilboard-Draufsicht für die Weboberfläche aus dem Makera-STEP (Ticket #36).

Aufruf (die STEP-Datei ist heruntergeladen und gilt als unvertrauenswürdig, deshalb isoliert):

    python3 -I tools/make_spoilboard_geometry.py hardware/vendor/spoilboard/Makera-Z1_MDF-v2.1.STEP \
        > include/spoilboard_geometry.h

Liest die Datei nur als Text (eigener Mini-Parser, kein CAD-Paket, nichts aus dem Vendor-Ordner wird
ausgeführt). Die Platte besteht nur aus Ebenen und Zylindern:
  - Umriss = alle Geraden auf der Oberseite (größtes z), zu einem Polygon verkettet (Bögen im
    Umriss bricht das Skript mit Meldung ab),
  - Löcher = Vollkreise auf der Oberseite; liegt am selben Mittelpunkt ein zweiter, kleinerer
    Kreis tiefer, ist es eine Senkbohrung (Senkung + Durchgangsloch).
Ausgabe in Platten-Koordinaten in 0,1 mm, Ursprung = vordere linke Ecke der Platte (kleinstes x
und kleinstes y im STEP-Rahmen), x nach rechts, y nach hinten, wie die Maschine.
"""
import math
import re
import sys

ENT = re.compile(r"^#(\d+)\s*=\s*([A-Z0-9_]+)\s*\((.*)\)\s*;\s*$", re.S)
REF = re.compile(r"#(\d+)")
NUM = re.compile(r"[-+]?\d+\.?\d*(?:[eE][-+]?\d+)?")
EPS = 1e-3


def parse(path):
    with open(path, "rb") as fh:
        text = fh.read().decode("latin-1")
    data = text.split("DATA;", 1)[1].split("ENDSEC;", 1)[0]
    ents = {}
    for stmt in data.split(";"):
        m = ENT.match(stmt.strip() + ";")
        if m:
            ents[int(m.group(1))] = (m.group(2), m.group(3))
    return ents


def refs(args):
    return [int(r) for r in REF.findall(args)]


def point(ents, i):
    kind, args = ents[i]
    if kind == "VERTEX_POINT":
        return point(ents, refs(args)[0])
    assert kind == "CARTESIAN_POINT", kind
    inner = args.split("(", 1)[1]
    return tuple(float(v) for v in NUM.findall(inner)[:3])


def direction(ents, i):
    kind, args = ents[i]
    assert kind == "DIRECTION", kind
    inner = args.split("(", 1)[1]
    return tuple(float(v) for v in NUM.findall(inner)[:3])


def circle(ents, i):
    """Center, normal and radius of a CIRCLE."""
    kind, args = ents[i]
    assert kind == "CIRCLE", kind
    place = refs(args)[0]
    radius = float(NUM.findall(args.split(",")[-1])[0])
    p_args = ents[place][1]
    loc, axis = refs(p_args)[:2]
    return point(ents, loc), direction(ents, axis), radius


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    ents = parse(sys.argv[1])
    edges = []  # (kind, start, end, circle-or-None)
    for i, (kind, args) in ents.items():
        if kind != "EDGE_CURVE":
            continue
        r = refs(args)
        a, b, geom = point(ents, r[0]), point(ents, r[1]), r[2]
        gk = ents[geom][0]
        if gk == "LINE":
            edges.append(("L", a, b, None))
        elif gk == "CIRCLE":
            edges.append(("C", a, b, circle(ents, geom)))
        else:
            sys.exit("unerwartete Kurve " + gk)

    pts = [e[1] for e in edges] + [e[2] for e in edges]
    x0 = min(p[0] for p in pts)
    y0 = min(p[1] for p in pts)
    x1 = max(p[0] for p in pts)
    y1 = max(p[1] for p in pts)
    z_top = max(p[2] for p in pts)
    z_bot = min(p[2] for p in pts)

    def on_top(p):
        return abs(p[2] - z_top) < EPS

    # Outline: top-face edges that are not full circles (a full circle starts and ends at one vertex).
    outline = []
    holes = {}
    for kind, a, b, c in edges:
        full = kind == "C" and math.dist(a, b) < EPS
        if full:
            (cx, cy, cz), n, rad = c
            if abs(abs(n[2]) - 1) > EPS:
                continue  # only vertical cylinders
            key = (round(cx, 2), round(cy, 2))
            holes.setdefault(key, set()).add((round(cz, 3), round(rad, 3)))
        elif on_top(a) and on_top(b):
            outline.append((kind, a, b, c))

    def q(v):  # board-local 0.1 mm
        return int(round(v * 10))

    out_holes = []
    for (cx, cy), rings in sorted(holes.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        top = [r for z, r in rings if abs(z - z_top) < EPS]
        if not top:
            continue  # blind from below: not visible from above
        r_top = max(top)
        inner = [r for z, r in rings if r < r_top - EPS]
        r_in = max(inner) if inner else 0
        out_holes.append((q(cx - x0), q(cy - y0), q(r_top), q(r_in)))

    w, h = q(x1 - x0), q(y1 - y0)
    if any(k != "L" for k, *_ in outline):
        sys.exit("Umriss enthaelt Boegen, Ausgabe dafuer erweitern")
    # Chain the outline segments into one closed polygon.
    segs = [((q(a[0] - x0), q(a[1] - y0)), (q(b[0] - x0), q(b[1] - y0))) for _, a, b, _ in outline]
    poly = [segs[0][0], segs[0][1]]
    segs = segs[1:]
    while segs:
        tail = poly[-1]
        for k, (a, b) in enumerate(segs):
            if a == tail or b == tail:
                poly.append(b if a == tail else a)
                del segs[k]
                break
        else:
            sys.exit("Umriss nicht geschlossen")
    if poly[-1] != poly[0]:
        sys.exit("Umriss nicht geschlossen")
    poly = poly[:-1]

    print("#pragma once")
    print("// Generated by tools/make_spoilboard_geometry.py from Makera-Z1_MDF-v2.1.STEP (ticket #36).")
    print("// Derived dimensions from Makera's public CAD file (see README, Trademarks). Do not edit by hand.")
    print("// Board-local coordinates in 0.1 mm, origin = front left corner of the board, x right, y back.")
    print("// Board %.2f x %.2f x %.2f mm, %d holes." % (w / 10, h / 10, z_top - z_bot, len(out_holes)))
    kinds = sorted({(hh[2], hh[3]) for hh in out_holes}, reverse=True)
    print("// JS object literal: w/h = size, o = outline polygon [x,y], k = hole kinds [r,r2]")
    print("// (r = radius at the top face; r2 > 0 = counterbore, r2 = through hole below it),")
    print("// p = holes as flat list x,y,kind index.")
    flat = ",".join("%d,%d,%d" % (hh[0], hh[1], kinds.index((hh[2], hh[3]))) for hh in out_holes)
    outl = ",".join("[%d,%d]" % pt for pt in poly)
    kk = ",".join("[%d,%d]" % kd for kd in kinds)
    print('#define SPOILBOARD_JS "{w:%d,h:%d,o:[%s],k:[%s],p:[%s]}"' % (w, h, outl, kk, flat))
    sys.stderr.write("Platte %.2f x %.2f mm, %d Loecher, %d davon gesenkt\n"
                     % (w / 10, h / 10, len(out_holes), sum(1 for hh in out_holes if hh[3])))


if __name__ == "__main__":
    main()
