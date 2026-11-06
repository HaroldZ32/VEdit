#!/usr/bin/env bash
# Build + run one KTextEditor probe inside the pinned Qt6/KF6 image.
#   bash evidence/ux-batch-2/rm5b-shortcuts/probe/run-probe.sh kt_undo_probe.cpp
# The build output stays in the container's /tmp so the workspace never gets a
# root-owned binary. Offscreen by default; pass --xcb to run under Xvfb+xcb.
set -euo pipefail

src="${1:?usage: run-probe.sh <probe.cpp> [--xcb]}"
mode="${2:-offscreen}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
img="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"

docker run --rm -i -e HOME=/tmp -v "$root:/vedit-build" "$img" bash -s -- "$src" "$mode" <<'INNER'
set -euo pipefail
src="/vedit-build/evidence/ux-batch-2/rm5b-shortcuts/probe/$1"
mode="$2"
bin="/tmp/$(basename "$src" .cpp)"
flags_inc=(-I/usr/include/x86_64-linux-gnu/qt6 -I/usr/include/x86_64-linux-gnu/qt6/QtCore
           -I/usr/include/x86_64-linux-gnu/qt6/QtGui -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets
           -I/usr/include/x86_64-linux-gnu/qt6/QtTest -I/usr/include/KF6/KTextEditor -I/usr/include/KF6/KSyntaxHighlighting -I/usr/include/KF6/KConfigWidgets -I/usr/include/KF6/KWidgetsAddons
           -I/usr/include/KF6/KCoreAddons -I/usr/include/KF6/KConfig
           -I/usr/include/KF6/KConfigCore -I/usr/include/KF6/KXmlGui
           -I/usr/include/KF6/KParts -I/usr/include/KF6/KIOCore -I/usr/include/KF6/KIO)
flags_lib=(-lQt6Widgets -lQt6Test -lQt6Gui -lQt6Core -lKF6TextEditor -lKF6CoreAddons
           -lKF6ConfigCore -lKF6XmlGui -lKF6Parts -lKF6KIOCore -lKF6GuiAddons
           -lKF6SyntaxHighlighting -ltree-sitter)
g++ -std=c++20 -fPIC -O1 -g -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB -DQT_NO_DEBUG "${flags_inc[@]}" "$src" -o "$bin" "${flags_lib[@]}"
export XDG_RUNTIME_DIR=/tmp/xdg-probe
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
if [ "$mode" = "--xcb" ]; then
  xvfb-run -a -s "-screen 0 1280x900x24" env QT_QPA_PLATFORM=xcb "$bin"
else
  env QT_QPA_PLATFORM=offscreen "$bin"
fi
INNER
