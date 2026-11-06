#!/usr/bin/env bash
# Build + run the RM-7 render probe inside the pinned image.
#   run-render.sh <output-subdir> [scratch theme_manager.cpp]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
out="${1:?usage: run-render.sh <output-subdir> [scratch theme_manager.cpp]}"
src="${2:-}"
img="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
docker run --rm -i -e HOME=/tmp -v "$root:/vedit-build" "$img" bash -s -- "$out" "$src" <<'INNER'
set -euo pipefail
out="$1"
src="$2"
root=/vedit-build
probe="$root/evidence/ux-batch-2/rm7-disabled-look/probe/render_pixels.cpp"
manager="$root/src/app/theme_manager.cpp"
if [ -n "$src" ]; then manager="$src"; fi
bin=/tmp/rm7-render-pixels
flags_inc=(-I"$root/src/app" -I/usr/include/x86_64-linux-gnu/qt6
           -I/usr/include/x86_64-linux-gnu/qt6/QtCore -I/usr/include/x86_64-linux-gnu/qt6/QtGui
           -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets
           -I/usr/include/KF6/KSyntaxHighlighting)
flags_lib=(-lQt6Widgets -lQt6Gui -lQt6Core -lKF6SyntaxHighlighting)
g++ -std=c++20 -fPIC -O1 -g -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB -DQT_NO_DEBUG \
    "${flags_inc[@]}" "$probe" "$manager" -o "$bin" "${flags_lib[@]}"
mkdir -p "$root/evidence/ux-batch-2/rm7-disabled-look/$out"
export XDG_RUNTIME_DIR=/tmp/xdg-probe; mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
env QT_QPA_PLATFORM=offscreen "$bin" "$root/evidence/ux-batch-2/rm7-disabled-look/$out"
INNER
