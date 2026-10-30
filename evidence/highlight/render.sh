#!/bin/sh
# Evidence step for T11: run the AC-05 probe under Xvfb and convert its PPM
# raster to PNG. Invoked from the repository root inside the pinned CentOS 7
# build image (see README.md in this directory for the exact docker command).
#
#   sh evidence/highlight/render.sh /opt/gsv4/share/gtksourceview-4
#
# Argument 1 is the GtkSourceView data directory (the one holding
# language-specs/ and styles/).
set -eu
DATADIR=${1:-/opt/gsv4/share/gtksourceview-4}
HERE=$(cd "$(dirname "$0")" && pwd)

xvfb-run -a cargo run -q -p vd-syntax --bin highlight-probe -- \
  --datadir "$DATADIR" --screenshot "$HERE/probe-ac05.ppm" \
  > "$HERE/01-probe.log" 2>&1

python3 - "$HERE/probe-ac05.ppm" "$HERE/probe-ac05.png" <<'PY'
import struct
import sys
import zlib

source, target = sys.argv[1], sys.argv[2]
with open(source, "rb") as handle:
    data = handle.read()
if not data.startswith(b"P6\n"):
    raise SystemExit(f"{source}: not a P6 PPM")
offset = data.index(b"\n") + 1
dimensions = data[offset:].split(b"\n", 2)
width, height = (int(value) for value in dimensions[0].split())
pixels = dimensions[2]

def chunk(kind, payload):
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

stride = width * 3
raw = b"".join(b"\x00" + pixels[row * stride:(row + 1) * stride] for row in range(height))
png = (
    b"\x89PNG\r\n\x1a\n"
    + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    + chunk(b"IDAT", zlib.compress(raw, 6))
    + chunk(b"IEND", b"")
)
with open(target, "wb") as handle:
    handle.write(png)
print(f"PNG {target} {width}x{height} {len(png)} bytes")
PY

# The PPM is an intermediate: the PNG is the reviewable artifact, and keeping a
# 1.9 MB raw raster in git would only bloat the tree.
rm -f "$HERE/probe-ac05.ppm"
