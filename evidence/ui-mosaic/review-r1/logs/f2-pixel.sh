#!/usr/bin/env bash
# A8 pixel evidence: same product theme (yunwu), same multi-syntax Verilog file,
# two syntax themes (Breeze Light / Breeze Dark). One run per theme.
# Usage: f2-pixel.sh <label> <syntax-theme>
set -euo pipefail
LOGS="/home/haroldzhao/MyProj/DshProj/VEdit/evidence/ui-mosaic/review-r1/logs"
NAME=review-r1-iso
BIN=${BIN:-/vedit-build/evidence/ui-mosaic/review-r1/logs/r2-binary}
FIX=/vedit-build/evidence/ui-mosaic/review-r1/r2/fixtures/multi-syntax.sv
LABEL=$1
THEME=${2:-}
SEED_THEME=${SEED_THEME:-}

# A fresh state with the SAME product theme (yunwu) so only the syntax theme differs.
docker exec "$NAME" bash -lc 'pkill -f "[r]2-binary" || true; find /state -mindepth 1 -delete; printf "uiTheme = \"yunwu\"\n" > /state/settings.toml'
if [ -n "$SEED_THEME" ]; then
  docker exec "$NAME" bash -lc "printf 'theme = \"$SEED_THEME\"\n' >> /state/settings.toml"
fi
args="--state-dir /state --hold 120000"
if [ -n "$THEME" ]; then
  args="$args --theme \"$THEME\""
fi
docker exec -d "$NAME" bash -lc \
  "DISPLAY=:98 QT_QPA_PLATFORM=xcb VEDIT_UI_PROBE=1 $BIN $args $FIX >/tmp/app-$LABEL.log 2>&1"
sleep 12
ln -sfn "$LOGS/r2/x11-iso" /tmp/.X11-unix
REVIEW_DISPLAY=:98 python3 "$LOGS/x11r2.py" shot "$LOGS/r2/$LABEL.png"
{
  docker exec "$NAME" bash -lc "grep -a 'ui-probe] theme=' /tmp/app-$LABEL.log | tail -1" || true
  docker exec "$NAME" bash -lc "grep -a 'unknown theme\|theme: \|theme fallback' /tmp/app-$LABEL.log | head -3" || true
} > "$LOGS/r2/$LABEL.probe.txt"
echo "--- $LABEL ---"; cat "$LOGS/r2/$LABEL.probe.txt"
