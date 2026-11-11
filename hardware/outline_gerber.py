#!/usr/bin/env python3
"""Writes the board outline as ONE closed Gerber path of straight segments (ticket #27).

KiCad plots Edge.Cuts as separate, unordered pieces (lines and G02 arcs, each with its own D02
move). Makera Studio does not recognise that as a board outline ("No valid PCB outline"). This
script chains the Edge.Cuts shapes into a single contour, approximates arcs with short chords
(<= 0.01 mm deviation) and overwrites the outline Gerber. Coordinates match KiCad's Gerber export
(same origin, Y negated), so it lines up with the copper layer.

Run with KiCad's Python:  python3 outline_gerber.py <board.kicad_pcb> <out.gbr>
"""
import math
import sys

import pcbnew

TOL = 0.01      # max chord deviation from the arc, mm
EPS = 1e-3      # endpoint matching tolerance, mm


def to_mm(v):
    return (pcbnew.ToMM(v.x), pcbnew.ToMM(v.y))


def arc_points(shape):
    cx, cy = to_mm(shape.GetCenter())
    sx, sy = to_mm(shape.GetStart())
    r = math.hypot(sx - cx, sy - cy)
    a0 = math.atan2(sy - cy, sx - cx)
    sweep = math.radians(shape.GetArcAngle().AsDegrees())
    n = max(2, math.ceil(abs(sweep) / (2 * math.acos(1 - TOL / r))))
    return [(cx + r * math.cos(a0 + sweep * i / n), cy + r * math.sin(a0 + sweep * i / n))
            for i in range(n + 1)]


def pieces(board):
    out = []
    for d in board.GetDrawings():
        if d.GetLayer() != pcbnew.Edge_Cuts:
            continue
        if d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
            out.append([to_mm(d.GetStart()), to_mm(d.GetEnd())])
        elif d.GetShape() == pcbnew.SHAPE_T_ARC:
            out.append(arc_points(d))
        else:
            raise SystemExit(f"unsupported Edge.Cuts shape {d.GetShapeStr()}")
    return out


def close(p, q):
    return abs(p[0] - q[0]) < EPS and abs(p[1] - q[1]) < EPS


def chain(parts):
    path = list(parts.pop(0))
    while parts:
        for i, part in enumerate(parts):
            if close(part[0], path[-1]):
                path += part[1:]
            elif close(part[-1], path[-1]):
                path += part[::-1][1:]
            else:
                continue
            parts.pop(i)
            break
        else:
            raise SystemExit("Edge.Cuts is not one closed contour")
    if not close(path[0], path[-1]):
        raise SystemExit("Edge.Cuts contour is not closed")
    path[-1] = path[0]
    return path


def coord(v):
    return str(round(v * 1e6))   # format 4.6, mm


def main(board_file, out_file):
    path = chain(pieces(pcbnew.LoadBoard(board_file)))
    lines = ["%TF.GenerationSoftware,makera-handrad,outline_gerber.py*%",
             "%TF.FileFunction,Profile,NP*%",
             "%FSLAX46Y46*%", "%MOMM*%", "%LPD*%", "G01*",
             "%TA.AperFunction,Profile*%", "%ADD10C,0.100000*%", "%TD*%", "D10*"]
    for i, (x, y) in enumerate(path):
        lines.append(f"X{coord(x)}Y{coord(-y)}D0{2 if i == 0 else 1}*")
    lines.append("M02*")
    with open(out_file, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"outline: {len(path) - 1} segments, closed")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
