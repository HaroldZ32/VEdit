#!/usr/bin/env bash
# Run the T15 editor-actions probe under a REAL X server (Xvfb + xcb) in the
# pinned image, so the RIGHT-CLICK POPUP is a real QMenu window:
#
#   bash evidence/e4-actions/probe/run-xcb.sh
#
# The probe is passed --require-popup: a missing popup widget is a FAIL here.
# Screenshots are rendered by the xcb platform (real widgets, real menu).
#
# Xvfb is started directly, not through xvfb-run: the pinned image has no
# xdpyinfo, and xvfb-run waits for it forever (measured: a 10-minute hang with
# only Xvfb alive). This is the same recipe evidence/e1-tabs/probe/run.sh uses.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
MENU_SHOT=${1:-evidence/e4-actions/10-context-menu-xcb.png}
F8_SHOT=${2:-evidence/e4-actions/11-f8-diagnostic-xcb.png}
DIAG_MENU_SHOT=${3:-evidence/e4-actions/12-menu-diagnostics-enabled-xcb.png}
MENU_REL=${MENU_SHOT#"$ROOT"/}
F8_REL=${F8_SHOT#"$ROOT"/}
DIAG_MENU_REL=${DIAG_MENU_SHOT#"$ROOT"/}

cd "$ROOT"
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime

docker run --rm \
  -v "$ROOT":/vedit-build \
  -w /vedit-build \
  -e QT_QPA_PLATFORM=xcb \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime \
  -e QT_XCB_GL_INTEGRATION=none \
  -e LIBGL_ALWAYS_SOFTWARE=1 \
  -e VEDIT_VERIBLE_LS_DIR=/vedit-build/.mpd/recon/verible/bin \
  -e VEDIT_VERIBLE_FORMAT=/vedit-build/.mpd/recon/verible/bin/verible-verilog-format \
  -e VEDIT_VERIBLE_LINT=/vedit-build/.mpd/recon/verible/bin/verible-verilog-lint \
  -e VEDIT_SLANG=/vedit-build/build/slang/out/slang \
  -e VEDIT_TOOLCHAIN_TRACE=1 \
  vedit-qt6-build:local \
  bash -lc "Xvfb :78 -screen 0 1400x1000x24 >/tmp/xvfb-e4-actions.log 2>&1 & xvfb=\$!; sleep 2; \
    DISPLAY=:78 QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software \
    timeout 300 stdbuf -oL -eL ./target/e4-actions-probe/editor-actions-probe '$MENU_REL' '$F8_REL' '$DIAG_MENU_REL' --require-popup; \
    rc=\$?; kill \$xvfb 2>/dev/null || true; exit \$rc"
