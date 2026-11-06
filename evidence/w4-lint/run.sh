#!/usr/bin/env bash
# Reproduce the T8 (lint orchestration) evidence: build the probe through the
# pinned Qt6 image, run it, and run the lane's ctest entries.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EV="$ROOT/evidence/w4-lint"
mkdir -p "$EV/logs"

docker run --rm -v "$ROOT":/vedit-build vedit-qt6-build:local bash -lc '
  set -euo pipefail
  cd /vedit-build
  cmake --build target/qt6-build --target vedit-lint-orchestration-probe
  mkdir -p /tmp/xdg && chmod 700 /tmp/xdg
  export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg
  target/qt6-build/src/app/workspace-lane-diagnostics/vedit-lint-orchestration-probe
' | tee "$EV/logs/10-probe.log"

docker run --rm -v "$ROOT":/vedit-build vedit-qt6-build:local bash -lc '
  set -euo pipefail
  cd /vedit-build
  mkdir -p /tmp/xdg && chmod 700 /tmp/xdg
  ctest --test-dir target/qt6-build \
    -R "vedit-workspace-diagnostics-selftest|vedit-lint-orchestration-probe" \
    --output-on-failure
' | tee "$EV/logs/11-ctest.log"
