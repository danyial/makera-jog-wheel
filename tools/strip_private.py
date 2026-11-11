#!/usr/bin/env python3
"""Removes private blocks for the public export (ticket #49, user decision for #46).

Private text (board revision 1, which stays in the private repository) sits between a begin and an end marker
line, each alone on its line, in the comment syntax of the file:
  C/C++:     // PRIVATE-BEGIN <tag>      // PRIVATE-END <tag>
  ini:       ; PRIVATE-BEGIN <tag>       ; PRIVATE-END <tag>
  YAML/py:   # PRIVATE-BEGIN <tag>       # PRIVATE-END <tag>
  Markdown:  <!-- PRIVATE-BEGIN <tag> --> <!-- PRIVATE-END <tag> -->
Blocks do not nest; begin and end carry the same tag. Both marker lines are removed with the block.

Private files (ticket #50) are never copied: PRIVATE_FILES lists paths relative to the repository root, an
entry ending in "/" covers the whole directory. They are third-party files (Makera logo and CAD, DFRobot/
SnapMagic model) that the private repository holds but must not be redistributed (DECISIONS.md 2026-11-11),
and the private forge tooling: CI, agent workflow and the export script (ticket #46).

Usage (stdlib only):
  python3 -I tools/strip_private.py --check [SRC]   # markers balanced in all tracked files? exit 1 if not
  python3 -I tools/strip_private.py SRC DEST        # copy tracked files of SRC to DEST without private blocks
                                                    # and without PRIVATE_FILES
  python3 -I tools/strip_private.py --list-private  # print PRIVATE_FILES, one per line (used by CI)
Tracked files come from `git ls-files` in SRC (without git: every file except .git, .pio, .pio-cache).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

MARKER = re.compile(r"^\s*(?://|;|#|<!--)\s*PRIVATE-(BEGIN|END)\s+(\S+?)\s*(?:-->)?\s*$")
SKIP_DIRS = {".git", ".pio", ".pio-cache"}
PRIVATE_FILES = [
    "src/logo_bitmap.h",                          # Makera logo, screensaver (export: src/logo_fallback.h)
    "hardware/enclosure/logo_polygons.json",      # Makera logo, enclosure engraving (export: neutral "MPG")
    "hardware/enclosure/enclosure_logo.json",     # logo builds (make_enclosure.py --logo), git-ignored anyway
    "hardware/enclosure/handrad_enclosure_logo.sc",
    "hardware/enclosure/enclosure_logo.step",
    "hardware/enclosure/front_shell_logo.stl",
    "hardware/vendor/",                           # DFRobot/SnapMagic DFR0654, Makera STEP, part models
    "hardware/handrad_pcb/fab/handrad.step",      # assembly STEP, embeds DFR0654
    "hardware/enclosure/handrad_assembly.sc",     # assembly, embeds the vendor models and handrad.step
    # Private forge and tooling (ticket #46): CI, agent permissions and workflow of the private repository,
    # and the export script itself (its scrub patterns name the private host).
    ".gitlab-ci.yml",
    ".claude/settings.json",
    ".claude/skills/git-workflow/",
    "tools/export_public.py",
]


def is_private(rel):
    rel = rel.replace(os.sep, "/")
    return any(rel == p or (p.endswith("/") and rel.startswith(p)) for p in PRIVATE_FILES)


def tracked_files(src):
    try:
        out = subprocess.run(["git", "-C", src, "ls-files", "-z"], check=True, capture_output=True).stdout
        return [p for p in out.decode().split("\0") if p]
    except (OSError, subprocess.CalledProcessError):
        files = []
        for root, dirs, names in os.walk(src):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            files += [os.path.relpath(os.path.join(root, n), src) for n in names]
        return sorted(files)


def read_text(path):
    try:
        with open(path, encoding="utf-8", newline="") as f:
            return f.read()
    except (UnicodeDecodeError, OSError):
        return None


def strip(text, name):
    """Returns (text without private blocks, list of errors)."""
    out, errors = [], []
    open_tag, open_line = None, 0
    for no, line in enumerate(text.splitlines(keepends=True), start=1):
        m = MARKER.match(line)
        if m:
            kind, tag = m.group(1), m.group(2)
            if kind == "BEGIN":
                if open_tag is not None:
                    errors.append(f"{name}:{no}: PRIVATE-BEGIN {tag} inside the block from line {open_line}")
                open_tag, open_line = tag, no
            else:
                if open_tag is None:
                    errors.append(f"{name}:{no}: PRIVATE-END {tag} without PRIVATE-BEGIN")
                elif tag != open_tag:
                    errors.append(f"{name}:{no}: PRIVATE-END {tag} closes PRIVATE-BEGIN {open_tag} (line {open_line})")
                open_tag = None
            continue
        if open_tag is None:
            out.append(line)
    if open_tag is not None:
        errors.append(f"{name}:{open_line}: PRIVATE-BEGIN {open_tag} never closed")
    return "".join(out), errors


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="only verify that the markers are balanced")
    ap.add_argument("--list-private", action="store_true", help="print PRIVATE_FILES and exit")
    ap.add_argument("src", nargs="?", default=".")
    ap.add_argument("dest", nargs="?")
    a = ap.parse_args()
    if a.list_private:
        print("\n".join(PRIVATE_FILES))
        return 0
    if not a.check and not a.dest:
        ap.error("DEST missing (or use --check)")
    src = os.path.abspath(a.src)
    if a.dest and os.path.abspath(a.dest).startswith(src + os.sep) and not a.check:
        ap.error("DEST must not lie inside SRC")
    errors, blocks, private = [], 0, 0
    for rel in tracked_files(src):
        path = os.path.join(src, rel)
        if not os.path.isfile(path):
            continue
        if is_private(rel):
            private += 1
            continue
        text = read_text(path)
        if text is not None:
            stripped, errs = strip(text, rel)
            errors += errs
            if stripped != text:
                blocks += 1
        if a.check:
            continue
        dst = os.path.join(a.dest, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if text is None or stripped == text:
            shutil.copy2(path, dst)
        else:
            with open(dst, "w", encoding="utf-8", newline="") as f:
                f.write(stripped)
            shutil.copymode(path, dst)
    for e in errors:
        print(e, file=sys.stderr)
    if errors:
        return 1
    print(f"{'checked' if a.check else 'copied'}: {blocks} file(s) with private blocks, {private} private file(s) left out")
    return 0


if __name__ == "__main__":
    sys.exit(main())
