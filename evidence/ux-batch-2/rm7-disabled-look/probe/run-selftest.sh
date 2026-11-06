#!/usr/bin/env bash
# Build + run the RM-7 disabled-look selftest source inside the pinned Qt6/KF6
# image, without configuring the whole tree. Used for the red/green runs:
#   run-selftest.sh                       # the tree's own theme_manager.cpp
#   run-selftest.sh <scratch theme_manager.cpp>   # the pre-fix copy (RED run)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
src="${1:-}"
img="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
docker run --rm -i -e HOME=/tmp -v "$root:/vedit-build" "$img" bash -s -- "$src" <<'INNER'
set -euo pipefail
src="$1"
root=/vedit-build
test_src="$root/src/app/disabled_look_selftest.cpp"
manager="$root/src/app/theme_manager.cpp"
if [ -n "$src" ]; then manager="$src"; fi
bin=/tmp/rm7-disabled-look-selftest
flags_inc=(-I"$root/src/app" -I/usr/include/x86_64-linux-gnu/qt6
           -I/usr/include/x86_64-linux-gnu/qt6/QtCore -I/usr/include/x86_64-linux-gnu/qt6/QtGui
           -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets
           -I/usr/include/KF6/KSyntaxHighlighting)
flags_lib=(-lQt6Widgets -lQt6Gui -lQt6Core -lKF6SyntaxHighlighting)
g++ -std=c++20 -fPIC -O1 -g -Wall -Wextra -Wpedantic -Werror \
    -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB -DQT_NO_DEBUG \
    "${flags_inc[@]}" "$test_src" "$manager" -o "$bin" "${flags_lib[@]}"
export XDG_RUNTIME_DIR=/tmp/xdg-probe; mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
env QT_QPA_PLATFORM=offscreen "$bin"
INNER
