#!/usr/bin/env bash
# UI-Mosaic F2 pixel evidence: SAME product theme (yunwu) in both runs, ONLY the
# KSyntaxHighlighting theme differs (default pairing "Breeze Light" vs an explicit
# `--theme "Breeze Dark"`). The same multi-syntax Verilog fixture is opened in
# both runs, and the two REAL window captures are compared pixel by pixel.
#
# Isolation (attributable evidence, no shared reviewer resources):
#   container  f2-pixels          (this lane only)
#   display    :97                (this lane only)
#   X sockets  $ROOT/.mpd/x11-97  (this lane only)
#   app state  evidence/ui-mosaic/fix-f2/state/  (--state-dir; this lane only)
#   output     evidence/ui-mosaic/fix-f2/**      (never any review*/ directory)
# Everything (container, Xvfb, socket symlink, capture) happens inside THIS one
# bash invocation: the host /tmp is per-call, so the symlink cannot outlive it.
#
# The capture chain itself (Xvfb + xcb real binary, host PIL grab of the X
# display) is the same class of chain the independent review used; this script is
# self-contained and shares no script, container, state dir or display with it.
# The comparison must land in the EDITOR text area, not only in the status bar.
#
# Run on the HOST from the repository root:  bash evidence/ui-mosaic/fix-f2/capture-pixels.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/fix-f2"
SHOTS="$OUT/screens"
STATE="$OUT/state"
XDIR="$ROOT/.mpd/x11-97"
CONTAINER="f2-pixels"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
FIXTURE="/vedit-build/evidence/ui-mosaic/fix-f2/fixture/syntax-mosaic.v"
STATE_IN_CONTAINER="/vedit-build/evidence/ui-mosaic/fix-f2/state"
DISPLAY_NUM=":97"
mkdir -p "$SHOTS" "$STATE"
rm -rf "$XDIR"
mkdir -p "$XDIR"
chmod 1777 "$XDIR"
# Both runs get the SAME freshly written state: product theme yunwu, no session.
printf 'uiTheme = "yunwu"\n' > "$STATE/settings.toml"
rm -f "$STATE/session.json"

cleanup() { docker rm -f "$CONTAINER" >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup
docker run -d --name "$CONTAINER" \
  -v "$XDIR:/tmp/.X11-unix:rw" \
  -v "$ROOT":/vedit-build -w /vedit-build "$IMG" \
  bash -lc "Xvfb $DISPLAY_NUM -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 3; sleep 100000" >/dev/null
sleep 4
# PIL's X connection uses the standard socket path; point it at this run's socket
# directory (the host has no X server, so the path is free).
rm -rf /tmp/.X11-unix
ln -sfn "$XDIR" /tmp/.X11-unix
ls -la /tmp/.X11-unix/ | sed -n '1,6p'

shoot() { # shoot <label> <extra app args...>
  local label=$1; shift
  docker exec "$CONTAINER" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 2
  docker exec -d "$CONTAINER" bash -lc \
    "DISPLAY=$DISPLAY_NUM QT_QPA_PLATFORM=xcb $APP --state-dir '$STATE_IN_CONTAINER' --hold 60000 $(printf '%q ' "$@") $FIXTURE >/tmp/app-$label.log 2>&1"
  sleep 14
  docker exec "$CONTAINER" bash -lc "cp /tmp/app-$label.log /vedit-build/evidence/ui-mosaic/fix-f2/screens/app-$label.log; tail -3 /tmp/app-$label.log"
  python3 - "$SHOTS" "$label" "$DISPLAY_NUM" <<'PY'
import sys
from PIL import Image, ImageGrab, ImageStat
out, label, display = sys.argv[1], sys.argv[2], sys.argv[3]
grab = ImageGrab.grab(xdisplay=display)
grab.save(f"{out}/{label}.png")
print(f"captured {label} size={grab.size} mean_luma={ImageStat.Stat(grab.convert('L')).mean[0]:.1f}")
PY
}

echo "--- yunwu + paired default syntax theme (Breeze Light) ---"
shoot yunwu-breeze-light --no-session-restore
echo "--- yunwu + explicit syntax theme (Breeze Dark), same file ---"
shoot yunwu-breeze-dark --no-session-restore --theme "Breeze Dark"
docker exec "$CONTAINER" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true

python3 - "$SHOTS" <<'PY'
import sys
from PIL import Image, ImageChops, ImageStat
out = sys.argv[1]
light = Image.open(f"{out}/yunwu-breeze-light.png").convert("RGB")
dark = Image.open(f"{out}/yunwu-breeze-dark.png").convert("RGB")
d = ImageChops.difference(light, dark)
print("light mean_luma=%.1f dark mean_luma=%.1f"
      % (ImageStat.Stat(light.convert("L")).mean[0], ImageStat.Stat(dark.convert("L")).mean[0]))
print("diff bbox:", d.getbbox())
px = d.load(); w, h = d.size
count8 = 0; count_any = 0; rows = {}
for y in range(h):
    row = 0
    for x in range(w):
        r, g, b = px[x, y]
        if max(r, g, b) > 8:
            row += 1
    if row:
        rows[y] = row
    count8 += row
print("changed pixels (>8):", count8)
if rows:
    ys = sorted(rows); bands = []; start = prev = ys[0]
    for y in ys[1:]:
        if y > prev + 1:
            bands.append((start, prev)); start = y
        prev = y
    bands.append((start, prev))
    print("changed row bands:", bands)
    # The status bar is the bottom strip of the 1280x820 product window; the
    # editor text area is everything above it. A fix that only repaints the
    # status text (the reviewed defect) leaves this at zero.
    window_bottom = max(y for y in ys)          # lowest changed row
    status_top = 780                            # below this = status bar strip
    editor_pixels = sum(n for y, n in rows.items() if y < status_top)
    band_pixels = sum(n for y, n in rows.items() if y >= status_top)
    print("changed pixels in editor/panel area (y<780):", editor_pixels)
    print("changed pixels in status strip (y>=780):", band_pixels)
    print("changed rows: top=%d bottom=%d" % (ys[0], ys[-1]))
    # a cropped pair for human review: the editor region only
    box = (240, 80, 1060, 770)
    light.crop(box).save(f"{out}/editor-light.png")
    dark.crop(box).save(f"{out}/editor-dark.png")
    ok = editor_pixels > 10000 and ys[0] < 200 and (ys[-1] - ys[0]) > 300
    print("F2-PIXEL:", "PASS - the editor text area changed" if ok else "FAIL - change did not reach the editor text area")
    if not ok:
        sys.exit(1)
else:
    print("F2-PIXEL: FAIL - no change at all")
    sys.exit(1)
PY
