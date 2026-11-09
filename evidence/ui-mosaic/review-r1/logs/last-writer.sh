#!/usr/bin/env bash
# F1 "last writer wins" check for the R1 review.
#
# One fresh run, TWO real user switches through DIFFERENT entries:
#   1) View > Theme > Sky Blue      (qingkong)
#   2) toolbar theme button > Obsidian (yaoshi)
# then: settings.toml must name the SECOND switch, the archive must agree after
# the quit, and a restart must read the SECOND switch back. This is the case the
# old code failed (each switch invisible in settings; a stale settings value won
# at the next start).
set -euo pipefail
ROOT="/home/haroldzhao/MyProj/DshProj/VEdit"
LOGS="$ROOT/evidence/ui-mosaic/review-r1/logs"
OUT="$LOGS/last-writer"
APP="/vedit-build/target/qt6-build/src/app/vedit-app"
NAME=review-r1-x
XDIR="$LOGS/x11"
mkdir -p "$OUT"

x11() { python3 "$LOGS/x11.py" chain "$1"; }
windows_raw() { python3 "$LOGS/geometry.py" windows; }
last_popup() {
  windows_raw | awk '/override=1/{last=$0} END{print last}' \
    | sed -E 's/.*geom=\(([0-9-]+),([0-9-]+) ([0-9]+)x([0-9]+)\).*/\1 \2 \3 \4/'
}
app_geom() {
  python3 "$LOGS/geometry.py" app \
    | sed -E 's/.*geom=\(x=([0-9-]+),y=([0-9-]+),w=([0-9]+),h=([0-9]+)\).*/\1 \2 \3 \4/'
}
settings_theme() { docker exec "$NAME" bash -lc 'grep -E "^uiTheme" /state/settings.toml || true'; }
archive_theme() {
  docker exec "$NAME" bash -lc 'python3 -c "import json;print(json.load(open(\"/state/session.json\")).get(\"uiTheme\"))" 2>/dev/null' || true
}
click_item() { # <geom> <idx> <count> <xoff>
  local geom=$1 idx=$2 count=$3 xoff=${4:-40}
  set -- $geom; local px=$1 py=$2 pw=$3 ph=$4
  local cy=$(python3 -c "print(int($py + ($idx + 0.5) * $ph / $count))")
  x11 "click $((px + xoff)) $cy"
}

{
  echo "=== F1 last-writer probe $(date -Is) ==="
  docker exec "$NAME" bash -lc 'pkill -f "[v]edit-app" || true; rm -f /state/session.json; mkdir -p /state; printf "uiTheme = \"bohe\"\n" > /state/settings.toml'
  sleep 1
  docker exec -d "$NAME" bash -lc \
    "DISPLAY=:99 QT_QPA_PLATFORM=xcb VEDIT_UI_PROBE=1 LANG=en_US.UTF-8 $APP --state-dir /state --hold 900000 >/tmp/app.log 2>&1"
  for i in $(seq 1 40); do sleep 1; python3 "$LOGS/geometry.py" app >/dev/null 2>&1 && break; done
  sleep 3
  x11 "focus; sleep 300"
  read -r WX WY WW WH <<<"$(app_geom)"
  echo "origin=($WX,$WY)"
  echo "settings@start: $(settings_theme)"

  # switch 1: View > Theme > item 4 (index 3 = Sky Blue / qingkong)
  x11 "click $((WX + 127)) $((WY + 11)); sleep 900"
  read -r PX PY PW PH <<<"$(last_popup)"
  x11 "click $((PX + 60)) $((PY + PH - 14)); sleep 1000"
  read -r SX SY SW SH <<<"$(last_popup)"
  click_item "$SX $SY $SW $SH" 3 6 40
  sleep 1.5
  echo "settings@after-switch-1: $(settings_theme)"
  x11 "shot $OUT/01-after-switch-1.png"

  # switch 2: toolbar theme button > item 5 (index 4 = Obsidian / yaoshi)
  x11 "click $((WX + 1256)) $((WY + 47)); sleep 900"
  read -r TX TY TW TH <<<"$(last_popup)"
  echo "toolbar popup=($TX,$TY ${TW}x${TH})"
  click_item "$TX $TY $TW $TH" 4 6 40
  sleep 1.5
  echo "settings@after-switch-2: $(settings_theme)"
  x11 "shot $OUT/02-after-switch-2.png"

  x11 "focus; move 600 400; sleep 200; key q Control_L; sleep 4000"
  echo "settings@after-quit: $(settings_theme)"
  echo "archive@after-quit: $(archive_theme)"

  docker exec -d "$NAME" bash -lc \
    "DISPLAY=:99 QT_QPA_PLATFORM=xcb VEDIT_UI_PROBE=1 LANG=en_US.UTF-8 $APP --state-dir /state --hold 4000 >/tmp/restart-lw.log 2>&1"
  sleep 8
  echo "restart-probe:"
  docker exec "$NAME" bash -lc 'grep -oE "theme=[a-z]+|status-theme=[^|]*" /tmp/restart-lw.log | head -2'
  sleep 4
} 2>&1 | tee "$OUT/last-writer.log"
echo "LAST-WRITER-DONE"
