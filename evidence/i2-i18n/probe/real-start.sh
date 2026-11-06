#!/usr/bin/env bash
# T18 — REAL shipped-binary startup evidence.
#
# Starts `target/qt6-build/src/app/vedit-app` itself (not the lane probe) twice
# under Xvfb + xcb:
#   * settings.toml has `"ui.language" = "zh_CN"`  -> Chinese menus/status bar
#   * no language key (Auto; the image locale is C) -> English source strings
# and grabs the X root window into evidence/i2-i18n/shots/36-real-*-start.png.
#
# The pinned build image has no xwd/import/scrot, so the grab is a 30-line Qt
# helper (probe/grab_root.cpp) from the same image. The bundled NotoSansCJK is
# installed first, exactly like probe/run.sh does (the build image only has
# DejaVu; without it Chinese renders as boxes — that is a font, not an i18n,
# property).
#
# Runs on the host (one docker run); called by probe/run.sh.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
IMAGE="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
OUT="$ROOT/evidence/i2-i18n"
mkdir -p "$OUT/shots" "$OUT/logs"

docker run --rm -v "$ROOT":/vedit-build -w /vedit-build "$IMAGE" bash -lc '
set -uo pipefail
export XDG_RUNTIME_DIR=/tmp/xdg-real; mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
if [ -f packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc ]; then
  mkdir -p /usr/share/fonts/truetype/vedit
  cp packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc /usr/share/fonts/truetype/vedit/
  fc-cache -f >/dev/null 2>&1 || true
fi
c++ -std=c++20 -fPIC $(pkg-config --cflags Qt6Gui) \
    evidence/i2-i18n/probe/grab_root.cpp -o target/i18n-probe/grab-root \
    $(pkg-config --libs Qt6Gui) || { echo "FATAL: grab-root build failed"; exit 1; }
Xvfb :82 -screen 0 1400x900x24 >/tmp/xvfb-real.log 2>&1 &
xvfb=$!
trap "kill $xvfb 2>/dev/null || true" EXIT
sleep 2
ok=1
run_case() {
  name=$1; lang=$2
  state=/tmp/i18n-real-$name; rm -rf "$state"; mkdir -p "$state"
  if [ -n "$lang" ]; then printf "\"ui.language\" = \"%s\"\n" "$lang" > "$state/settings.toml"; fi
  DISPLAY=:82 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
    target/qt6-build/src/app/vedit-app --state-dir "$state" --hold 6000 >/tmp/app-$name.log 2>&1 &
  app=$!
  sleep 4
  DISPLAY=:82 target/i18n-probe/grab-root "evidence/i2-i18n/shots/36-real-$name-start.png"
  shot=$?
  wait $app; apprc=$?
  echo "CASE $name language=${lang:-auto} shot_rc=$shot app_rc=$apprc"
  [ "$shot" -eq 0 ] && [ "$apprc" -eq 0 ] || ok=0
  grep -E "vedit-app:|error|ASSERT" /tmp/app-$name.log | head -3 || true
}
run_case zh zh_CN
run_case en ""
for f in 36-real-zh-start.png 36-real-en-start.png; do
  [ -s "evidence/i2-i18n/shots/$f" ] || { echo "FATAL: missing $f"; ok=0; }
done
[ "$ok" -eq 1 ] || exit 1
echo "REAL-START-OK"
' 2>&1 | tee "$OUT/logs/34-real-binary-i18n.log"
