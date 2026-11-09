#!/usr/bin/env bash
# T4 independent check, part 2: the archive-only and invalid-settings cases.
# Part 1 (t4-precedence.sh) showed that a run REWRITES session.json on exit, so
# the archive must be re-edited immediately before each run; this script does
# that explicitly.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
LOGS="$ROOT/evidence/ui-mosaic/review/logs"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
S="$LOGS/precedence2/state"
rm -rf "$LOGS/precedence2"
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
      > "$LOGS/precedence2/$label.log" 2>&1 || true
  printf '%-30s ' "$label"
  grep -o 'theme=[a-z]*' "$LOGS/precedence2/$label.log" | head -1 || echo "NO PROBE"
  printf '%-30s archive-after-exit=%s\n' "" "$(python3 -c "import json;print(json.load(open('$S/session.json')).get('uiTheme'))" 2>/dev/null || echo '<none>')"
}

echo "--- seed a valid archive from a real run (settings yaoshi) ---"
printf 'uiTheme = "yaoshi"\n' > "$S/settings.toml"
run seed

echo "--- archive-only: archive edited to shenye, NO settings.toml ---"
python3 -c "import json;p='$S/session.json';d=json.load(open(p));d['uiTheme']='shenye';json.dump(d,open(p,'w'),indent=2)"
rm -f "$S/settings.toml"
run archive-shenye

echo "--- invalid settings id + archive shenye: what wins? ---"
python3 -c "import json;p='$S/session.json';d=json.load(open(p));d['uiTheme']='shenye';json.dump(d,open(p,'w'),indent=2)"
printf 'uiTheme = "no-such-theme"\n' > "$S/settings.toml"
run invalid-settings
