#!/usr/bin/env bash
# Build + run the RM-7 palette probe inside the pinned Qt6/KF6 image.
#   bash evidence/ux-batch-2/rm7-disabled-look/probe/run-probe.sh [extra-source]
# extra-source: optional replacement theme_manager.cpp (the RED run's pre-fix copy);
# default is the tree's own src/app/theme_manager.cpp.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
src="${1:-}"
img="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
docker run --rm -i -e HOME=/tmp -v "$root:/vedit-build" "$img" bash -s -- "$src" <<'INNER'
set -euo pipefail
src="$1"
root=/vedit-build
probe="$root/evidence/ux-batch-2/rm7-disabled-look/probe/palette_probe.cpp"
manager="$root/src/app/theme_manager.cpp"
if [ -n "$src" ]; then manager="$src"; fi
bin=/tmp/rm7-palette-probe
flags_inc=(-I"$root/src/app" -I/usr/include/x86_64-linux-gnu/qt6
           -I/usr/include/x86_64-linux-gnu/qt6/QtCore -I/usr/include/x86_64-linux-gnu/qt6/QtGui
           -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets
           -I/usr/include/KF6/KSyntaxHighlighting)
flags_lib=(-lQt6Widgets -lQt6Gui -lQt6Core -lKF6SyntaxHighlighting)
g++ -std=c++20 -fPIC -O1 -g -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB -DQT_NO_DEBUG \
    "${flags_inc[@]}" "$probe" "$manager" -o "$bin" "${flags_lib[@]}"
export XDG_RUNTIME_DIR=/tmp/xdg-probe; mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
env QT_QPA_PLATFORM=offscreen "$bin"
INNER
