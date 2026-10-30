#!/usr/bin/env bash
# Gate `dist`: an AppImage must exist and be a plausible, runnable artifact.
# T23 produces it; until then this gate is honestly RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

shopt -s nullglob
candidates=("$ROOT"/build/artifacts/*.AppImage "$ROOT"/*.AppImage)
if [ "${#candidates[@]}" -eq 0 ]; then
  no_gate "[dist] 本项目暂无分发产物（T23 负责）：未找到 *.AppImage"
fi

fail=0
for appimage in "${candidates[@]}"; do
  echo "[dist] checking $appimage"
  if [ ! -x "$appimage" ]; then
    echo "[dist] FAIL: not executable"
    fail=1
    continue
  fi
  size=$(stat -c '%s' "$appimage")
  if [ "$size" -lt 20000000 ]; then
    echo "[dist] FAIL: only $size bytes - an AppImage bundling GTK/slang/verible must be larger"
    fail=1
  fi
  if ! file "$appimage" | grep -q 'ELF'; then
    echo "[dist] FAIL: not an ELF image"
    fail=1
  fi
  if "$appimage" --appimage-version >/dev/null 2>&1; then
    echo "[dist] OK: AppImage runtime answers --appimage-version"
  else
    echo "[dist] FAIL: AppImage runtime does not answer --appimage-version"
    fail=1
  fi
done
[ "$fail" -eq 0 ] || exit 1
echo "[dist] PASS: ${candidates[*]}"
