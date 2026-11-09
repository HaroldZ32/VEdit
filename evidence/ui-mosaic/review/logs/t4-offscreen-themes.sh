#!/usr/bin/env bash
# T4 independent check: start the DELIVERED binary once per product theme,
# offscreen, with settings.toml `uiTheme` as the theme selector (the contract
# forbids a new CLI option, so this is the real startup path), and record the
# runtime probe line (theme applied, status-bar text, QSS state, toolbar size,
# opaque icon pixels).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
LOGS="$ROOT/evidence/ui-mosaic/review/logs"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
THEMES=(yunwu xinghuang bohe qingkong yaoshi shenye)

rm -rf "$LOGS/offscreen"
mkdir -p "$LOGS/offscreen"

for theme in "${THEMES[@]}"; do
  state="$LOGS/offscreen/state-$theme"
  rm -rf "$state"
  mkdir -p "$state"
  printf 'uiTheme = "%s"\n' "$theme" > "$state/settings.toml"
  docker run --rm \
    -v "$ROOT":/vedit-build -v "$state":/state -w /vedit-build \
    -e QT_QPA_PLATFORM=offscreen \
    -e XDG_RUNTIME_DIR=/tmp/xdg-runtime-t4 \
    -e VEDIT_UI_PROBE=1 \
    "$IMG" bash -lc \
      "mkdir -p /tmp/xdg-runtime-t4 && chmod 700 /tmp/xdg-runtime-t4 && \
       ./target/qt6-build/src/app/vedit-app --state-dir /state --hold 4000 --no-session-restore" \
      > "$LOGS/offscreen/app-$theme.log" 2>&1 || echo "EXIT=$? theme=$theme" >> "$LOGS/offscreen/app-$theme.log"
  echo "== $theme =="
  grep -E '\[ui-probe\] theme=|^EXIT=' "$LOGS/offscreen/app-$theme.log" || echo "  NO PROBE LINE"
done

echo "--- icon opaque-pixel lines (one theme) ---"
grep 'opaquePixels' "$LOGS/offscreen/app-yaoshi.log" | head -20 || true
