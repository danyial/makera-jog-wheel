#!/bin/sh
# Regenerates the board and writes the milling data to hardware/handrad_pcb/fab/ (ticket #20).
set -e
cd "$(dirname "$0")"
KICAD=/Applications/KiCad/KiCad.app/Contents
PY=$KICAD/Frameworks/Python.framework/Versions/Current/bin/python3
CLI=$KICAD/MacOS/kicad-cli
BOARD=handrad_pcb/handrad.kicad_pcb
FAB=handrad_pcb/fab
ROW=$(sed -n 's/^ROW = \([0-9.]*\).*/\1/p' generate_board.py)
python3 models/make_models.py "$ROW" >/dev/null
"$PY" generate_board.py
rm -rf "$FAB" && mkdir -p "$FAB"
"$CLI" pcb drc -o "$FAB/drc.rpt" "$BOARD" >/dev/null
# .gbr extension (not Protel .gbl/.gm1): Makera Studio only accepts .gbr.
"$CLI" pcb export gerbers --no-protel-ext --layers B.Cu,Edge.Cuts -o "$FAB/" "$BOARD" >/dev/null
# Makera Studio needs the outline as one closed path; KiCad writes unordered pieces with arcs.
"$PY" outline_gerber.py "$BOARD" "$FAB/handrad-Edge_Cuts.gbr" 2>/dev/null
"$CLI" pcb export drill --format excellon --excellon-units mm -o "$FAB/" "$BOARD" >/dev/null
# Copper side seen from below (mirrored), for the mill:
"$CLI" pcb export svg --layers B.Cu,Edge.Cuts --mirror --mode-single --fit-page-to-board \
  -o "$FAB/handrad-B_Cu-mirrored.svg" "$BOARD" >/dev/null
# 1:1 top view with outline and labels, for a paper fit test before milling:
"$CLI" pcb export pdf --layers B.Cu,Edge.Cuts,F.SilkS --mode-single -o "$FAB/handrad-1to1.pdf" "$BOARD" >/dev/null
# 3D: STEP of the whole assembly and the renders. With hardware/vendor/ (private repository only, ticket #50)
# generate_board.py also writes a board copy with the licensed FireBeetle model; the STEP comes from that
# copy, the renders always from the board with the simplified model.
STEP_BOARD=$BOARD
[ -f handrad_pcb/handrad_vendor.kicad_pcb ] && STEP_BOARD=handrad_pcb/handrad_vendor.kicad_pcb
"$CLI" pcb export step --subst-models --force -o "$FAB/handrad.step" "$STEP_BOARD" >/dev/null 2>&1 || echo "STEP export failed"
rm -f handrad_pcb/handrad_vendor.kicad_pcb handrad_pcb/handrad_vendor.kicad_pro handrad_pcb/handrad_vendor.kicad_prl
"$CLI" pcb render --quality high --width 1600 --height 1200 --perspective --rotate "-55,0,20" --zoom 0.85 --background opaque \
  -o "$FAB/handrad-3d.png" "$BOARD" >/dev/null 2>&1 || echo "render failed"
"$CLI" pcb render --quality high --width 1600 --height 1200 --side bottom --background opaque \
  -o "$FAB/handrad-3d-bottom.png" "$BOARD" >/dev/null 2>&1 || echo "render failed"
# Top view with readable connector labels (ticket #49): 2D silkscreen + copper seen from above (SVG) and an
# orthographic top render of the assembly (PNG).
"$CLI" pcb export svg --layers B.Cu,F.SilkS,Edge.Cuts --mode-single --exclude-drawing-sheet --fit-page-to-board \
  -o "$FAB/handrad-top-labels.svg" "$BOARD" >/dev/null
"$CLI" pcb render --quality high --width 2000 --height 1700 --side top --zoom 0.95 --background opaque \
  -o "$FAB/handrad-top.png" "$BOARD" >/dev/null 2>&1 || echo "render failed"
grep -E "^\*\* Found" "$FAB/drc.rpt" || true
ls "$FAB"
