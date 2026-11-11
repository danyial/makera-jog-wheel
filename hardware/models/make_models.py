#!/usr/bin/env python3
"""Writes simplified STEP models (coloured boxes) for parts without a KiCad 3D model (ticket #20).

  firebeetle2_esp32ue.step  FireBeetle 2 ESP32-UE as it sits on the carrier (incl. male header bodies)
  pogo_p50_j1.step          P50-J1 pogo pin, compressed to the working height

Model coordinates follow KiCad: origin = footprint origin, X right, Y towards the top of the board
view (USB end), Z up, mm. Dimensions are taken from photos and data sheets; they are meant for
fit checks and enclosure design, not for manufacturing the FireBeetle.

Run with any Python 3:  python3 hardware/models/make_models.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# Stack heights above the carrier top (z = 0).
SOCKET_H = 8.5        # 2.54 mm female header
MALE_BODY_H = 2.5     # plastic body of the FireBeetle's male headers
FB_Z0 = SOCKET_H + MALE_BODY_H   # FireBeetle PCB underside, = gap for the pogo pins (11.0)
FB_T = 1.6            # FireBeetle PCB thickness


class Step:
    def __init__(self, name):
        self.name = name
        self.lines = []
        self.n = 0
        self.solids = []   # (solid id, rgb)

    def add(self, text):
        self.n += 1
        self.lines.append(f"#{self.n}={text};")
        return self.n

    def pt(self, x, y, z):
        return self.add(f"CARTESIAN_POINT('',({x:.4f},{y:.4f},{z:.4f}))")

    def dir(self, x, y, z):
        return self.add(f"DIRECTION('',({x:.4f},{y:.4f},{z:.4f}))")

    def box(self, x0, y0, z0, x1, y1, z1, rgb):
        xs, ys, zs = (min(x0, x1), max(x0, x1)), (min(y0, y1), max(y0, y1)), (min(z0, z1), max(z0, z1))
        coords = {(i, j, k): (xs[i], ys[j], zs[k]) for i in (0, 1) for j in (0, 1) for k in (0, 1)}
        vtx = {key: self.add(f"VERTEX_POINT('',#{self.pt(*c)})") for key, c in coords.items()}
        edges = {}

        def edge(a, b):
            key = tuple(sorted((a, b)))
            if key not in edges:
                pa, pb = coords[key[0]], coords[key[1]]
                d = [pb[m] - pa[m] for m in range(3)]
                length = sum(v * v for v in d) ** 0.5
                vec = self.add(f"VECTOR('',#{self.dir(*[v / length for v in d])},{length:.4f})")
                line = self.add(f"LINE('',#{self.pt(*pa)},#{vec})")
                edges[key] = self.add(f"EDGE_CURVE('',#{vtx[key[0]]},#{vtx[key[1]]},#{line},.T.)")
            return edges[key], (a, b) == tuple(sorted((a, b)))

        faces_def = [  # counter-clockwise seen from outside
            ((0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0)),
            ((1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)),
            ((0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)),
            ((0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)),
            ((0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0)),
            ((0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)),
        ]
        faces = []
        for f in faces_def:
            oes = []
            for a, b in zip(f, f[1:] + f[:1]):
                eid, fwd = edge(a, b)
                oes.append(self.add(f"ORIENTED_EDGE('',*,*,#{eid},{'.T.' if fwd else '.F.'})"))
            loop = self.add(f"EDGE_LOOP('',({','.join('#%d' % o for o in oes)}))")
            bound = self.add(f"FACE_OUTER_BOUND('',#{loop},.T.)")
            p0, p1, p3 = coords[f[0]], coords[f[1]], coords[f[3]]
            e1 = [p1[m] - p0[m] for m in range(3)]
            e2 = [p3[m] - p0[m] for m in range(3)]
            nrm = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
            # (f0->f1) x (f0->f3) is the outward normal for the CCW order above
            ln = sum(v * v for v in nrm) ** 0.5
            le = sum(v * v for v in e1) ** 0.5
            ax = self.add(f"AXIS2_PLACEMENT_3D('',#{self.pt(*p0)},#{self.dir(*[v / ln for v in nrm])},"
                          f"#{self.dir(*[v / le for v in e1])})")
            plane = self.add(f"PLANE('',#{ax})")
            faces.append(self.add(f"ADVANCED_FACE('',(#{bound}),#{plane},.T.)"))
        shell = self.add(f"CLOSED_SHELL('',({','.join('#%d' % f for f in faces)}))")
        solid = self.add(f"MANIFOLD_SOLID_BREP('',#{shell})")
        self.solids.append((solid, rgb))

    def write(self, path):
        body_start = self.n
        app = self.add("APPLICATION_CONTEXT('automotive design')")
        self.add(f"APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#{app})")
        pctx = self.add(f"PRODUCT_CONTEXT('',#{app},'mechanical')")
        prod = self.add(f"PRODUCT('{self.name}','{self.name}','',(#{pctx}))")
        pdf = self.add(f"PRODUCT_DEFINITION_FORMATION('','',#{prod})")
        pdc = self.add(f"PRODUCT_DEFINITION_CONTEXT('part definition',#{app},'design')")
        pd = self.add(f"PRODUCT_DEFINITION('design','',#{pdf},#{pdc})")
        pds = self.add(f"PRODUCT_DEFINITION_SHAPE('','',#{pd})")
        self.add(f"PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#{prod}))")
        lu = self.add("(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.))")
        au = self.add("(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.))")
        su = self.add("(NAMED_UNIT(*)SI_UNIT($,.STERADIAN.)SOLID_ANGLE_UNIT())")
        unc = self.add(f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#{lu},'distance_accuracy_value',"
                       f"'confusion accuracy')")
        ctx = self.add(f"(GEOMETRIC_REPRESENTATION_CONTEXT(3)GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#{unc}))"
                       f"GLOBAL_UNIT_ASSIGNED_CONTEXT((#{lu},#{au},#{su}))REPRESENTATION_CONTEXT('Context','3D'))")
        origin = self.add(f"AXIS2_PLACEMENT_3D('',#{self.pt(0, 0, 0)},#{self.dir(0, 0, 1)},#{self.dir(1, 0, 0)})")
        items = ",".join(["#%d" % origin] + ["#%d" % s for s, _ in self.solids])
        rep = self.add(f"ADVANCED_BREP_SHAPE_REPRESENTATION('',({items}),#{ctx})")
        self.add(f"SHAPE_DEFINITION_REPRESENTATION(#{pds},#{rep})")
        styled = []
        for solid, (r, g, b) in self.solids:
            col = self.add(f"COLOUR_RGB('',{r:.3f},{g:.3f},{b:.3f})")
            fasc = self.add(f"FILL_AREA_STYLE_COLOUR('',#{col})")
            fas = self.add(f"FILL_AREA_STYLE('',(#{fasc}))")
            ssfa = self.add(f"SURFACE_STYLE_FILL_AREA(#{fas})")
            sss = self.add(f"SURFACE_SIDE_STYLE('',(#{ssfa}))")
            ssu = self.add(f"SURFACE_STYLE_USAGE(.BOTH.,#{sss})")
            psa = self.add(f"PRESENTATION_STYLE_ASSIGNMENT((#{ssu}))")
            styled.append(self.add(f"STYLED_ITEM('color',(#{psa}),#{solid})"))
        self.add(f"MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('',({','.join('#%d' % s for s in styled)}),"
                 f"#{ctx})")
        del body_start
        with open(path, "w") as f:
            f.write("ISO-10303-21;\nHEADER;\n")
            f.write(f"FILE_DESCRIPTION(('{self.name}'),'2;1');\n")
            f.write(f"FILE_NAME('{os.path.basename(path)}','2026-11-09T00:00:00',(''),(''),'','','');\n")
            f.write("FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n")
            f.write("\n".join(self.lines))
            f.write("\nENDSEC;\nEND-ISO-10303-21;\n")


BLACK, DARK, SILVER, WHITE, GOLD, STEEL = ((0.05, 0.05, 0.05), (0.12, 0.12, 0.14), (0.78, 0.78, 0.80),
                                           (0.95, 0.95, 0.92), (0.85, 0.68, 0.25), (0.70, 0.70, 0.72))


def firebeetle(path, row, pitch):
    """FireBeetle 2 ESP32-UE, origin = board centre at carrier level. 25.4 x 60 mm."""
    s = Step("FireBeetle 2 ESP32-UE (simplified)")
    z0, z1 = FB_Z0, FB_Z0 + FB_T
    s.box(-12.7, -30.0, z0, 12.7, 30.0, z1, DARK)                       # PCB
    # Male header bodies under the board. Left row pin 1 is 11 mm below the USB edge (y = +19.0).
    y_pin1 = 30.0 - 11.32   # DFR0654 footprint: pin 1 at y = -18.68
    s.box(-row / 2 - 1.27, y_pin1 + 1.27, z0 - MALE_BODY_H, -row / 2 + 1.27, y_pin1 - 17 * pitch - 1.27, z0, BLACK)
    s.box(row / 2 - 1.27, y_pin1 - 4 * pitch + 1.27, z0 - MALE_BODY_H, row / 2 + 1.27, y_pin1 - 17 * pitch - 1.27,
          z0, BLACK)
    # Top side (positions from a photo of the board, USB-C at the top).
    s.box(-7.9, -24.4, z1, 9.1, -4.8, z1 + 3.2, SILVER)                  # ESP32-WROOM-32UE shield
    s.box(-4.6, 24.4, z1, 4.6, 33.7, z1 + 3.2, SILVER)                   # USB-C (overhangs)
    s.box(4.9, 12.2, z1, 13.6, 21.6, z1 + 4.5, WHITE)                    # PH2.0 battery socket
    s.box(-12.4, 21.1, z1, -9.3, 25.8, z1 + 1.6, BLACK)                  # RST button
    s.box(8.9, 21.1, z1, 12.6, 25.8, z1 + 1.6, BLACK)                    # button GPIO27
    s.box(-6.5, -25.6, z1 + 3.2, -5.3, -24.4, z1 + 4.2, GOLD)            # u.FL connector
    s.write(path)


def pogo(path):
    """P50-J1, soldered from the top, compressed to the working height (head on the FireBeetle pad)."""
    s = Step("Pogo P50-J1 (compressed)")
    b, p = 0.68 / 2, 0.48 / 2
    s.box(-b, -b, -1.6 - 2.0, b, b, FB_Z0 - 2.0, GOLD)     # barrel, 2 mm below the carrier underside
    s.box(-p, -p, FB_Z0 - 2.0, p, p, FB_Z0, STEEL)         # plunger up to the FireBeetle underside
    s.write(path)


if __name__ == "__main__":
    import sys
    row = float(sys.argv[1]) if len(sys.argv) > 1 else 22.5
    firebeetle(os.path.join(HERE, "firebeetle2_esp32ue.step"), row, 2.54)
    pogo(os.path.join(HERE, "pogo_p50_j1.step"))
    print("written to", HERE)
