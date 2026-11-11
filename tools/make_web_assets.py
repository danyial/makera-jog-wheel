#!/usr/bin/env python3
"""Static assets of the web status page (ticket #37, SPEC 9.1): writes src/web_assets.h.

The 3D view needs three.js. It goes gzip-compressed into the firmware image (flash, .rodata), so an
OTA update (firmware only, SPEC 9.5) updates it too; no filesystem partition.

  python3 -I tools/make_web_assets.py                      # after editing web/view3d.js
  python3 -I tools/make_web_assets.py --three <package>    # (re)import three.js from an unpacked npm package

--three takes the directory "package" of the npm tarball three-0.160.0.tgz (registry.npmjs.org, sha512
integrity DLU8lc0z…uDhng==), unpacked into its own empty directory. The files are only read as bytes and
checked against the SHA-256 values below; nothing from there is executed. Result: web/vendor/*.gz
(committed, deterministic gzip -9 without timestamp) and web/vendor/three-LICENSE.

Always: check web/vendor/* against VENDOR_SHA256 (the CI job web-assets runs this), gzip web/view3d.js, then write src/web_assets.h (all assets as byte arrays, only webui.cpp includes it)
and src/web_asset_paths.h (path of the 3D module for the page). The 3D module is
served under a path with its content hash (cached as immutable, a new build gets a new path).
"""
import argparse
import gzip
import hashlib
import pathlib
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
VENDOR = WEB / "vendor"
OUT = ROOT / "src" / "web_assets.h"
OUT_PATHS = ROOT / "src" / "web_asset_paths.h"

THREE_VERSION = "0.160.0"
THREE_FILES = {  # file in the npm package -> SHA-256
    "build/three.module.min.js": "3e690ac7d180b0aadf0891bea39eec643e29e2d3e75c99b18689518665f69ba6",
    "examples/jsm/controls/OrbitControls.js": "5a44a9e86a2a0fb11933eed69bc2cd33c76a496854c1aed6ed776efa87d7b064",
    "LICENSE": "852e0e8699169bf9f6fdc6bda3e682d078dcbc738b5d33e74df594721bff271d",
}
THREE_GZ = f"three-{THREE_VERSION}.js.gz"
OC_GZ = f"oc-{THREE_VERSION}.js.gz"
# Committed web/vendor files -> SHA-256 of their content (decompressed for .gz). Checked on every run,
# so CI notices a changed vendor file. three = the npm file unchanged; oc = OrbitControls with the
# licence line and the rewritten import (see import_three).
VENDOR_SHA256 = {
    THREE_GZ: THREE_FILES["build/three.module.min.js"],
    OC_GZ: "994254f44feaf2ac9fdaf577ca5b43ca44e7258978fe7de268671d3b308f8bd3",
    "three-LICENSE": THREE_FILES["LICENSE"],
}
# web/view3d.js imports OrbitControls from this placeholder; the generator puts in the hashed path.
OC_PLACEHOLDER = b"'./oc.js'"


def gz(data: bytes) -> bytes:
    """gzip -9 with a fixed header (mtime 0, XFL 2, OS 255), so the bytes do not depend on the Python
    version (gzip.compress wrote OS 3 or 255 depending on version). The deflate stream comes from zlib;
    classic zlib gives the same stream on macOS and in the CI image (zlib-ng would not)."""
    c = zlib.compressobj(9, zlib.DEFLATED, -15, 8)
    body = c.compress(data) + c.flush()
    return (b"\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff" + body +
            struct.pack("<II", zlib.crc32(data), len(data) & 0xFFFFFFFF))


def import_three(pkg: pathlib.Path) -> None:
    raw = {}
    for name, want in THREE_FILES.items():
        data = (pkg / name).read_bytes()
        got = hashlib.sha256(data).hexdigest()
        if got != want:
            sys.exit(f"{name}: SHA-256 {got}, erwartet {want}")
        raw[name] = data
    oc = raw["examples/jsm/controls/OrbitControls.js"]
    if oc.count(b"from 'three';") != 1:
        sys.exit("OrbitControls.js: Import von 'three' nicht gefunden")
    # The browser has no import map here: point the import to the served three.js path (same module).
    oc = (f"// three.js r{THREE_VERSION.split('.')[1]} OrbitControls, MIT License, "
          "Copyright 2010-2023 three.js authors\n").encode() + \
        oc.replace(b"from 'three';", f"from './three-{THREE_VERSION}.js';".encode())
    VENDOR.mkdir(parents=True, exist_ok=True)
    (VENDOR / THREE_GZ).write_bytes(gz(raw["build/three.module.min.js"]))
    (VENDOR / OC_GZ).write_bytes(gz(oc))
    (VENDOR / "three-LICENSE").write_bytes(raw["LICENSE"])
    for name in VENDOR_SHA256:  # the rewritten OrbitControls must match the pinned hash too
        vendor(name)


def vendor(name: str) -> bytes:
    """Content of a committed vendor file (decompressed for .gz), checked against VENDOR_SHA256."""
    data = (VENDOR / name).read_bytes()
    content = gzip.decompress(data) if name.endswith(".gz") else data
    got = hashlib.sha256(content).hexdigest()
    if got != VENDOR_SHA256[name]:
        sys.exit(f"web/vendor/{name}: SHA-256 {got}, erwartet {VENDOR_SHA256[name]}")
    return content


def h8(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:8]


def c_array(name: str, data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 32):
        lines.append(",".join(str(b) for b in data[i:i + 32]))
    return f"static const uint8_t {name}[{len(data)}] = {{\n" + ",\n".join(lines) + "};\n"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--three", type=pathlib.Path, help="unpacked npm package three@" + THREE_VERSION)
    args = ap.parse_args()
    if args.three:
        import_three(args.three)
    for name in VENDOR_SHA256:
        vendor(name)
    # Paths: three.js by version (its bytes are the npm file, pinned by SHA-256 above; another version
    # gets another path). OrbitControls and the 3D view by content hash: their bytes are ours (rewritten
    # import, licence line), so any change gets a new path and the immutable cache never serves old code.
    three = (VENDOR / THREE_GZ).read_bytes()
    oc = (VENDOR / OC_GZ).read_bytes()
    oc_path = f"/a/oc-{h8(vendor(OC_GZ))}.js"
    v3d_src = (WEB / "view3d.js").read_bytes()
    if v3d_src.count(OC_PLACEHOLDER) != 1:
        sys.exit("web/view3d.js: Import " + OC_PLACEHOLDER.decode() + " nicht genau einmal gefunden")
    v3d_src = v3d_src.replace(OC_PLACEHOLDER, f"'.{oc_path[2:]}'".encode())
    v3d = gz(v3d_src)
    v3d_path = f"/a/v3d-{h8(v3d_src)}.js"
    assets = [  # (path, symbol, data)
        (f"/a/three-{THREE_VERSION}.js", "kGzThree", three),
        (oc_path, "kGzOrbit", oc),
        (v3d_path, "kGzView3d", v3d),
    ]
    total = sum(len(a[2]) for a in assets)
    out = [
        "#pragma once\n",
        "// Generated by tools/make_web_assets.py (ticket #37). Do not edit by hand.\n",
        f"// gzip -9 assets of the status page, sent from flash with Content-Encoding: gzip, {total} bytes.\n",
        f"// three.js {THREE_VERSION} (three.module.min.js, OrbitControls): MIT License, Copyright 2010-2023\n",
        "// three.js authors, full text in web/vendor/three-LICENSE. 3D view: web/view3d.js.\n",
        "#include <cstddef>\n#include <cstdint>\n\n",
        "struct WebAsset {\n  const char* path;\n  const uint8_t* gz;\n  size_t len;\n};\n\n",
    ]
    for _, sym, data in assets:
        out.append(c_array(sym, data))
    out.append("\nstatic const WebAsset kWebAssets[] = {\n")
    for path, sym, _ in assets:
        out.append(f'    {{"{path}", {sym}, sizeof({sym})}},\n')
    out.append("};\n")
    OUT.write_text("".join(out))
    OUT_PATHS.write_text(
        "#pragma once\n// Generated by tools/make_web_assets.py (ticket #37). Do not edit by hand.\n"
        "// Path of the 3D module (content hash), used by the status page (web_pages.h).\n"
        f'#define WEB_V3D_PATH "{v3d_path}"\n')
    for path, _, data in assets:
        print(f"{path}: {len(data)} Byte gzip")
    print(f"{OUT.relative_to(ROOT)}: {total} Byte Assets")


if __name__ == "__main__":
    main()
