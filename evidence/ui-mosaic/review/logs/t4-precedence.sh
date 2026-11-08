#!/usr/bin/env bash
# T4 independent check: startup precedence and the persistence path that A1
# relies on ("switch the theme, restart, it is still there").
#
# Contract §2.1 precedence: settings.toml `uiTheme` > session archive `uiTheme`
# > default yunwu. The question this script answers is what happens when the
# two disagree — which is exactly the state after a user picks a theme in
# Preferences (writes settings.toml) and later switches it from the View menu
# (writes only the session archive; see the code finding in VERDICT.md).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
LOGS="$ROOT/evidence/ui-mosaic/review/logs"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
S="$LOGS/precedence/state"
rm -rf "$LOGS/precedence"
mkdir -p "$S"

run() { # run <label>
  local label=$1
  docker run --rm \
    -v "$ROOT":/vedit-build -v "$S":/state -w /vedit-build \
    -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg-runtime-t4 \
    -e VEDIT_UI_PROBE=1 \
    "$IMG" bash -lc \
      "mkdir -p /tmp/xdg-runtime-t4 && chmod 700 /tmp/xdg-runtime-t4 && \
       ./target/qt6-build/src/app/vedit-app --state-dir /state --hold 3500" \
      > "$LOGS/precedence/$label.log" 2>&1 || true
  printf '%-28s ' "$label"
  grep -o 'theme=[a-z]*' "$LOGS/precedence/$label.log" | head -1 || echo "NO PROBE"
}

echo "--- 1. settings uiTheme=bohe, no archive: expect bohe, archive written ---"
printf 'uiTheme = "bohe"\n' > "$S/settings.toml"
run seed-bohe
ls -la "$S" | sed -n '2,6p'
python3 - "$S/session.json" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p))
print("archive.uiTheme =", d.get("uiTheme"))
d["uiTheme"]="shenye"; json.dump(d,open(p,"w"),indent=2)
print("rewrote archive.uiTheme =", json.load(open(p)).get("uiTheme"))
PY

echo "--- 2. settings bohe vs archive shenye: contract says settings wins ---"
run settings-beats-archive

echo "--- 3. no settings: archive shenye must be used ---"
rm -f "$S/settings.toml"
run archive-only

echo "--- 4. invalid settings id blocks the archive fallback (observed) ---"
printf 'uiTheme = "no-such-theme"\n' > "$S/settings.toml"
run invalid-settings
