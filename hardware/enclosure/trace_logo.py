#!/usr/bin/env python3
"""Zeichnet das Makera-"M" aus einer Bildvorlage als Polygone nach (Ticket #30).

Aufruf:  python3 -I trace_logo.py <logo.png> [logo_polygons.json]

Die Vorlage (PNG des Nutzers) liegt in keinem Repository. Das Logo ist eine Marke von Makera, nur
zur privaten Nutzung: `logo_polygons.json` liegt nur im privaten Repository und wird vom oeffentlichen
Export weggelassen (Ticket #50). `make_enclosure.py --logo` graviert daraus Makera-"M" + "PG"
(enclosure_logo.json, lokal), ohne --logo das neutrale "MPG" (Ticket #45, DECISIONS.md 2026-11-11).

Ablauf: Schwarz und Blau getrennt als Graustufenfeld, beide Felder so weit "erodiert",
dass zwischen allen Teilflaechen bei der Zielgroesse mindestens MIN_GAP_MM frei bleibt
(die weissen Fugen der Vorlage sind dafuer zu schmal), Konturen bei 0.5 (contourpy aus
matplotlib), vereinfacht (Douglas-Peucker). Ausgabe normiert: Hoehe des Logos = 1,
Ursprung unten links, y nach oben.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image
import contourpy

TARGET_CAP_MM = 14.0   # Zielhoehe auf der Front (nur fuer Fugenbreite und Vereinfachung)
MIN_GAP_MM = 0.6
SIMPLIFY_MM = 0.03


def fields(path):
    im = np.asarray(Image.open(path).convert("RGB")).astype(float) / 255.0
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    blue = np.clip((b - r) * 1.6, 0, 1)                    # kraeftiges Blau
    black = np.clip((1 - np.maximum(np.maximum(r, g), b)) * 1.6, 0, 1) * (1 - blue)
    return black, blue


def erode(f, rad):
    """Grauwert-Erosion mit einer Kreisscheibe (Minimum ueber Verschiebungen)."""
    if rad <= 0:
        return f
    out = f.copy()
    p = int(math.ceil(rad))
    pad = np.pad(f, p, mode="constant")
    h, w = f.shape
    for dy in range(-p, p + 1):
        for dx in range(-p, p + 1):
            if dx * dx + dy * dy <= rad * rad:
                out = np.minimum(out, pad[p + dy:p + dy + h, p + dx:p + dx + w])
    return out


def contours(f):
    gen = contourpy.contour_generator(z=f, line_type="Separate")
    return [np.asarray(c) for c in gen.lines(0.5) if len(c) > 8]


def area(p):
    x, y = p[:, 0], p[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))


def dp(pts, eps):
    """Douglas-Peucker fuer einen offenen Linienzug."""
    if len(pts) < 3:
        return pts
    a, b = pts[0], pts[-1]
    ab = b - a
    n = np.hypot(*ab)
    rel = pts - a
    d = (np.abs(ab[0] * rel[:, 1] - ab[1] * rel[:, 0]) / n) if n > 0 else np.hypot(*rel.T)
    i = int(np.argmax(d))
    if d[i] > eps:
        return np.vstack([dp(pts[:i + 1], eps)[:-1], dp(pts[i:], eps)])
    return np.vstack([a, b])


def simplify_closed(p, eps):
    p = p[:-1] if np.allclose(p[0], p[-1]) else p
    i = int(np.argmax(np.hypot(*(p - p[0]).T)))       # zweiter Ankerpunkt: der entfernteste
    s1 = dp(p[:i + 1], eps)
    s2 = dp(np.vstack([p[i:], p[:1]]), eps)
    return np.vstack([s1[:-1], s2[:-1]])


def min_gap(polys):
    best = float("inf")
    for i in range(len(polys)):
        for j in range(i + 1, len(polys)):
            a, b = polys[i], polys[j]
            d = np.hypot(a[:, None, 0] - b[None, :, 0], a[:, None, 1] - b[None, :, 1]).min()
            best = min(best, d)
    return best


def trace(path):
    black, blue = fields(path)
    ys, xs = np.nonzero((black > 0.5) | (blue > 0.5))
    height_px = ys.max() - ys.min() + 1
    mm_per_px = TARGET_CAP_MM / height_px
    # Fugen messen, dann so weit erodieren, dass jede Fuge >= MIN_GAP_MM wird
    raw = [(c, k) for k, f in (("black", black), ("blue", blue)) for c in contours(f)]
    gap_px = min_gap([c for c, _ in raw])
    need_px = MIN_GAP_MM / mm_per_px
    rad = max(0.0, (need_px - gap_px) / 2 + 0.5)
    shapes = []
    for kind, f in (("black", black), ("blue", blue)):
        for c in contours(erode(f, rad)):
            if abs(area(c)) * mm_per_px ** 2 < 0.5:      # Krümel unter 0.5 mm²
                continue
            shapes.append((kind, simplify_closed(c, SIMPLIFY_MM / mm_per_px)))
    gap_after = min_gap([p for _, p in shapes]) * mm_per_px
    # normieren: Hoehe 1, y nach oben, Ursprung unten links
    allp = np.vstack([p for _, p in shapes])
    x0, y1 = allp[:, 0].min(), allp[:, 1].max()
    y0 = allp[:, 1].min()
    hgt = y1 - y0
    out = []
    for kind, p in shapes:
        q = np.column_stack([(p[:, 0] - x0) / hgt, (y1 - p[:, 1]) / hgt])
        if area(q) < 0:
            q = q[::-1]
        out.append({"kind": kind, "points": [[round(float(x), 5), round(float(y), 5)] for x, y in q]})
    width = float((allp[:, 0].max() - x0) / hgt)
    info = {"source": "Makera-Logo, Vorlage des Nutzers (nicht im Repository)",
            "note": "Marke von Makera, nur private Nutzung, git-ignoriert, nicht weitergeben",
            "height": 1.0, "width": round(width, 5),
            "traced_at_cap_mm": TARGET_CAP_MM,
            "gap_source_mm": round(gap_px * mm_per_px, 3), "erosion_mm": round(rad * mm_per_px, 3),
            "min_gap_mm": round(gap_after, 3), "shapes": out}
    return info


if __name__ == "__main__":
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                               "logo_polygons.json")
    info = trace(src)
    with open(dst, "w") as f:
        json.dump(info, f, indent=None, separators=(",", ":"))
        f.write("\n")
    print(f"{len(info['shapes'])} Flaechen, Breite {info['width']:.3f} x Hoehe 1, "
          f"Fuge Vorlage {info['gap_source_mm']} mm -> {info['min_gap_mm']} mm "
          f"(Erosion {info['erosion_mm']} mm), Punkte {sum(len(s['points']) for s in info['shapes'])}")
