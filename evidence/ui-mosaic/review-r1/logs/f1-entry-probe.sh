#!/usr/bin/env bash
# F1 entry-point probe for the UI-Mosaic R1 review.
#
# Drives the REAL UI with XTest (host libXtst -> Xvfb in the pinned image):
#   view    View > Theme submenu, then the 4th theme (Sky Blue / 晴空蓝)
#   toolbar the toolbar theme button, same submenu
#   prefs   Ctrl+, -> "Interface theme" combo -> 4th item -> OK
# and records, for each step: settings.toml, the session archive, screenshots
# and the restart read-back probe. No coordinate is hand-typed: the app window
# origin and every Qt popup geometry are queried from the X tree at run time.
#
# This is the same script used as the PRE-FIX positive control and, after the
# repair, as the independent check. Usage:
#   APP_BIN=/vedit-build/.../vedit-app bash f1-entry-probe.sh view [label]
set -euo pipefail
ROOT="/home/haroldzhao/MyProj/DshProj/VEdit"
LOGS="$ROOT/evidence/ui-mosaic/review-r1/logs"
ENTRY="${1:?usage: f1-entry-probe.sh view|toolbar|prefs [label]}"
LABEL="${2:-$ENTRY}"
APP_BIN="${APP_BIN:-/vedit-build/evidence/ui-mosaic/review-r1/logs/prefix-vedit-app}"
NAME=review-r1-x
XDIR="$LOGS/x11"
OUT="$LOGS/f1-$LABEL"
mkdir -p "$OUT"

x11() { python3 "$LOGS/x11.py" chain "$1"; }

# geometry.py windows -> "<x> <y> <w> <h>" of the LAST mapped override-redirect
# popup (Qt menus and combo popups are override-redirect top-levels).
last_popup() {
  python3 "$LOGS/geometry.py" windows | awk '/override=1/{last=$0} END{print last}' \
    | sed -E 's/.*geom=\(([0-9-]+),([0-9-]+) ([0-9]+)x([0-9]+)\).*/\1 \2 \3 \4/'
}
app_geom() {
  python3 "$LOGS/geometry.py" app | sed -E 's/.*geom=\(x=([0-9-]+),y=([0-9-]+),w=([0-9]+),h=([0-9]+)\).*/\1 \2 \3 \4/'
}
# Non-override top-level that is not the main window (the Preferences dialog).
dialog_geom() {
  python3 "$LOGS/geometry.py" windows | awk '/override=0/ && !/1280x820/' | tail -1 \
    | sed -E 's/.*geom=\(([0-9-]+),([0-9-]+) ([0-9]+)x([0-9]+)\).*/\1 \2 \3 \4/'
}
state() { docker exec "$NAME" bash -lc "$1"; }
settings() { state 'cat /state/settings.toml 2>/dev/null' || true; }
archive_theme() {
  state 'python3 -c "import json;print(json.load(open(\"/state/session.json\")).get(\"uiTheme\"))" 2>/dev/null' || true
}
click_item() { # <popup geom> <index 0-based> <count> [xoffset]
  local geom=$1 idx=$2 count=$3 xoff=${4:-40}
  set -- $geom; local px=$1 py=$2 pw=$3 ph=$4
  local cy=$(python3 -c "print(int($py + ($idx + 0.5) * $ph / $count))")
  x11 "click $((px + xoff)) $cy"
}
stop_app() {
  x11 "focus; move 600 400; sleep 200; key q Control_L; sleep 3500" || true
  state 'pkill -f "[r]eview-r1/logs/prefix-vedit-app" || true; pkill -f "[r]eview-r1/logs/r1-binary" || true; pkill -f "[t]arget/qt6-build/src/app/vedit-app" || true' || true
  sleep 1
}
start_app() {
  local hold=$1
  docker exec -d "$NAME" bash -lc \
    "DISPLAY=:99 QT_QPA_PLATFORM=xcb VEDIT_UI_PROBE=1 $APP_BIN --state-dir /state --hold $hold >/tmp/app.log 2>&1"
  # Poll for the window instead of a fixed sleep: under load the first paint can
  # take longer than 13 s and a fixed wait made a whole entry probe unusable.
  local i
  for i in $(seq 1 40); do
    sleep 1
    python3 "$LOGS/geometry.py" app >/dev/null 2>&1 && break
  done
  sleep 3
}

{
  echo "=== F1 probe entry=$ENTRY label=$LABEL bin=$APP_BIN at $(date -Is) ==="
  docker exec "$NAME" bash -lc 'pkill -f "[v]edit-app" || true; rm -f /state/session.json; mkdir -p /state; printf "uiTheme = \"bohe\"\n" > /state/settings.toml'
  sleep 1
  start_app 900000
  x11 "focus; sleep 300"
  python3 "$LOGS/geometry.py" app
  read -r WX WY WW WH <<<"$(app_geom)"
  echo "app origin=($WX,$WY) size=${WW}x${WH}"

  x11 "shot $OUT/01-start.png"
  echo "settings@start:"; settings
  echo "archive@start: $(archive_theme)"

  if [ "$ENTRY" = view ]; then
    x11 "click $((WX + 127)) $((WY + 11)); sleep 900"
    read -r PX PY PW PH <<<"$(last_popup)"
    echo "view menu popup=($PX,$PY ${PW}x${PH})"
    x11 "shot $OUT/02-viewmenu.png"
    # The Theme item is the LAST entry of the View menu.
    x11 "click $((PX + 60)) $((PY + PH - 14)); sleep 1000"
    read -r SX SY SW SH <<<"$(last_popup)"
    echo "theme submenu=($SX,$SY ${SW}x${SH})"
    x11 "shot $OUT/03-themesubmenu.png"
    click_item "$SX $SY $SW $SH" 3 6 40
  elif [ "$ENTRY" = toolbar ]; then
    x11 "click $((WX + 1256)) $((WY + 47)); sleep 900"
    read -r PX PY PW PH <<<"$(last_popup)"
    echo "toolbar theme popup=($PX,$PY ${PW}x${PH})"
    x11 "shot $OUT/02-toolbarmenu.png"
    click_item "$PX $PY $PW $PH" 3 6 40
  elif [ "$ENTRY" = prefs ]; then
    x11 "focus; move 600 400; sleep 200; key comma Control_L; sleep 2500"
    read -r DX DY DW DH <<<"$(dialog_geom)"
    echo "preferences dialog=($DX,$DY ${DW}x${DH})"
    x11 "shot $OUT/02-prefs.png"
    # "Interface theme" combo: first form row, dialog-relative (450, 63).
    x11 "click $((DX + 450)) $((DY + 63)); sleep 1000"
    read -r CX CY CW CH <<<"$(last_popup)"
    echo "ui theme combo popup=($CX,$CY ${CW}x${CH})"
    x11 "shot $OUT/03-uicombo.png"
    click_item "$CX $CY $CW $CH" 3 6 60
    sleep 0.8
    x11 "shot $OUT/04-picked.png"
    # OK is the first bottom-right button, dialog-relative.
    x11 "click $((DX + 659)) $((DY + 924)); sleep 2000"
    x11 "shot $OUT/05-ok.png"
  else
    echo "unknown entry $ENTRY" >&2; exit 2
  fi

  sleep 2
  x11 "shot $OUT/06-after-switch.png"
  echo "settings@after-switch:"; settings
  echo "archive@after-switch: $(archive_theme)"

  # F1 sub-case: opening Preferences after a menu/toolbar switch must NOT replay
  # a stale settings value. Pre-fix this reverts the chrome; post-fix it must not.
  if [ "$ENTRY" != prefs ]; then
    x11 "focus; move 600 400; sleep 200; key comma Control_L; sleep 2500"
    x11 "shot $OUT/07-prefs-after-switch.png"
    python3 - "$OUT/06-after-switch.png" "$OUT/07-prefs-after-switch.png" <<'PY'
import sys
from PIL import Image
a, b = (Image.open(p).convert("RGB") for p in sys.argv[1:3])
# right edge of the main window, outside the Preferences dialog (x=1270)
print("color-after-switch=%s color-with-prefs-open=%s" % (a.getpixel((1270, 500)), b.getpixel((1270, 500))))
PY
    read -r DX DY DW DH <<<"$(dialog_geom)"
    # Cancel is the second bottom-right button, dialog-relative (743, 924).
    x11 "click $((DX + 743)) $((DY + 924)); sleep 800"
  fi

  stop_app
  echo "settings@after-quit:"; settings
  echo "archive@after-quit: $(archive_theme)"
  x11 "shot $OUT/08-after-quit.png"

  # Restart read-back: the user-visible meaning of "restart keeps the theme".
  start_app 4000
  echo "restart-probe:"; state 'grep -o "theme=[a-z]*" /tmp/app.log | head -1; grep -o "status-theme=[^ ]*" /tmp/app.log | head -1'
  sleep 6
} 2>&1 | tee "$OUT/probe.log"
echo "F1-PROBE-DONE $ENTRY $LABEL"
