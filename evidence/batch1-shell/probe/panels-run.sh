#!/usr/bin/env bash
# Runs the batch-1b acceptance probe (AC-1.3 panels recoverable through View,
# AC-1.4 Save As) in the pinned Qt6/KF6 image. The product must already be built
# (scripts/build-qt.sh): the probe links the product's own static libraries and
# never recompiles src/.
#
#   bash evidence/batch1-shell/probe/panels-run.sh
#
# Evidence lands in evidence/batch1-shell/ (20-probe-panels-build.log,
# 21-probe-panels-offscreen.log, 22-probe-panels-xcb.log, shots/30-*, shots/31-*).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
IMAGE="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"

if [ ! -f "$ROOT/target/qt6-build/build.ninja" ]; then
  echo "FATAL: target/qt6-build/build.ninja is missing; run scripts/build-qt.sh first" >&2
  exit 1
fi

docker run --rm \
  -v "$ROOT":/vedit-build \
  -w /vedit-build \
  -e HOME=/tmp \
  -e LANG=C.UTF-8 \
  "$IMAGE" bash evidence/batch1-shell/probe/panels-container-run.sh
