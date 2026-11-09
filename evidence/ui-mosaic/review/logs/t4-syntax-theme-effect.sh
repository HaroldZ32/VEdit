#!/usr/bin/env bash
# T4 independent check (v3): syntax-theme visual effect, clean sockets.
# Same product theme (yunwu) in both shots; only the KSyntaxHighlighting theme
# differs. Fresh X display :97 and a fresh socket dir so a stale :99 socket from
# an earlier capture cannot poison the run.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/review/logs/screens"
XDIR="$ROOT/.mpd/x11-t4"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
mkdir -p "$OUT"
rm -rf "$XDIR"; mkdir -p "$XDIR"; chmod 1777 "$XDIR"

cleanup() { docker rm -f vedit-t4-shot >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup
docker run -d --name vedit-t4-shot \
  -v "$XDIR:/tmp/.X11-unix:rw" \
  -v "$ROOT":/vedit-build -w /vedit-build "$IMG" \
  bash -lc 'Xvfb :97 -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 3; sleep 100000' >/dev/null
sleep 4
rm -rf /tmp/.X11-unix
ln -sfn "$XDIR" /tmp/.X11-unix
ls -la /tmp/.X11-unix/

shoot() { # shoot <label> <extra app args...>
  local label=$1; shift
  docker exec vedit-t4-shot bash -lc \
    "mkdir -p /root/.config/VEdit/vedit-app && printf 'uiTheme = \"yunwu\"\n' > /root/.config/VEdit/vedit-app/settings.toml && rm -f /root/.config/VEdit/vedit-app/session.json"
  docker exec vedit-t4-shot bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 2
  docker exec -d vedit-t4-shot bash -lc \
    "DISPLAY=:97 QT_QPA_PLATFORM=xcb $APP --hold 60000 $(printf '%q ' "$@") >/tmp/app-$label.log 2>&1"
  sleep 12
  docker exec vedit-t4-shot bash -lc "cp /tmp/app-$label.log /vedit-build/evidence/ui-mosaic/review/logs/screens/app-$label.log; tail -2 /tmp/app-$label.log"
  python3 - "$OUT" "$label" <<'PY'
import sys
from PIL import Image, ImageGrab, ImageStat
out, label = sys.argv[1], sys.argv[2]
grab = ImageGrab.grab(xdisplay=":97")
grab.save(f"{out}/{label}.png")
print(f"captured {label} mean_luma={ImageStat.Stat(grab.convert('L')).mean[0]:.1f}")
PY
}

echo "--- yunwu + default syntax pairing (Breeze Light) ---"
shoot yunwu-plain --no-session-restore
echo "--- yunwu + explicit --theme 'Breeze Dark' ---"
shoot yunwu-syntaxdark --no-session-restore --theme "Breeze Dark"
docker exec vedit-t4-shot bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true

python3 - "$OUT" <<'PY'
import sys
from PIL import Image, ImageChops
out=sys.argv[1]
a=Image.open(f"{out}/yunwu-plain.png").convert("RGB")
b=Image.open(f"{out}/yunwu-syntaxdark.png").convert("RGB")
d=ImageChops.difference(a,b)
print("diff bbox:", d.getbbox())
px=d.load(); w,h=d.size; count=0; rows={}
for y in range(h):
    row=0
    for x in range(w):
        r,g,bl=px[x,y]
        if max(r,g,bl)>8:
            row+=1
    count+=row
    if row: rows[y]=row
print("changed pixels (>8):", count)
if rows:
    ys=sorted(rows); bands=[]; start=prev=ys[0]
    for y in ys[1:]:
        if y>prev+1: bands.append((start,prev)); start=y
        prev=y
    bands.append((start,prev))
    print("changed row bands:", bands)
    print("VERDICT:", "ONLY THE STATUS TEXT CHANGED" if count < 5000 else "VISUAL CHANGE BEYOND THE STATUS TEXT")
else:
    print("VERDICT: NO CHANGE AT ALL")
PY
