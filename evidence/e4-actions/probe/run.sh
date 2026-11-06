#!/usr/bin/env bash
# Run the T15 editor-actions probe in the pinned image (OFFSCREEN mode):
#
#   bash evidence/e4-actions/probe/run.sh
#
# Screenshots land in evidence/e4-actions/ (menu + post-F8 window). The probe
# drives the REAL product shell (ApplicationShell + installDiagnostics), opens
# REAL files in a REAL KTextEditor view and talks to a REAL verible-verilog-ls
# session (the local checkout under .mpd/recon/verible/bin), so every enabled
# action's effect is observed, not simulated.
#
# Offscreen does not deliver a POPUP WIDGET to the platform (QMenu::exec still
# runs, but there is no activePopupWidget); the real right-click popup is
# asserted by run-xcb.sh under Xvfb. Everything else runs here.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
MENU_SHOT=${1:-evidence/e4-actions/10-context-menu.png}
F8_SHOT=${2:-evidence/e4-actions/11-f8-diagnostic.png}
DIAG_MENU_SHOT=${3:-evidence/e4-actions/12-menu-diagnostics-enabled.png}
MENU_REL=${MENU_SHOT#"$ROOT"/}
F8_REL=${F8_SHOT#"$ROOT"/}
DIAG_MENU_REL=${DIAG_MENU_SHOT#"$ROOT"/}

cd "$ROOT"
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime

docker run --rm \
  -v "$ROOT":/vedit-build \
  -w /vedit-build \
  -e QT_QPA_PLATFORM=offscreen \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime \
  -e VEDIT_VERIBLE_LS_DIR=/vedit-build/.mpd/recon/verible/bin \
  -e VEDIT_VERIBLE_FORMAT=/vedit-build/.mpd/recon/verible/bin/verible-verilog-format \
  -e VEDIT_VERIBLE_LINT=/vedit-build/.mpd/recon/verible/bin/verible-verilog-lint \
  -e VEDIT_SLANG=/vedit-build/build/slang/out/slang \
  -e VEDIT_TOOLCHAIN_TRACE=1 \
  vedit-qt6-build:local \
  bash -lc "./target/e4-actions-probe/editor-actions-probe '$MENU_REL' '$F8_REL' '$DIAG_MENU_REL'"
