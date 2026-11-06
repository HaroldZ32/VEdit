#!/usr/bin/env bash
# Builds the RM-9 pass-cost probe against the PRODUCT lint seam inside the
# pinned Qt6 image (the host has no Qt development files).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT"
source scripts/lib-qt.sh
load_qt_versions
run_in_qt6_container bash -lc 'cd /vedit-build && g++ -std=gnu++20 -fPIC -O2 \
  -I src/workspace/diagnostics -I src/common -I crates/vd-ffi/include \
  $(pkg-config --cflags Qt6Core Qt6Gui Qt6Widgets) \
  evidence/ux-batch-2/rm9-dots/probe/coverage_cost_probe.cpp \
  target/qt6-build/src/app/workspace-lane-diagnostics/libvedit_workspace_diagnostics.a \
  target/release/libvd_ffi.a \
  -o evidence/ux-batch-2/rm9-dots/probe/coverage_cost_probe \
  $(pkg-config --libs Qt6Core Qt6Gui Qt6Widgets) -lpthread -ldl -lm'
echo "built evidence/ux-batch-2/rm9-dots/probe/coverage_cost_probe"
