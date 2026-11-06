#!/usr/bin/env bash
# Runs the batch-2 acceptance probe (AC-2.1 rename applied, AC-2.2 references
# list, AC-2.3 existing actions) in the pinned Qt6/KF6 image. The product must
# already be built (scripts/build-qt.sh): the probe links the product's own
# static libraries and never recompiles src/.
#
#   bash evidence/batch2-editor/probe/run.sh
#
# Evidence lands in evidence/batch2-editor/ (00-probe-build.log,
# 10-probe-offscreen.log, 11-probe-xcb.log, shots/10-*, shots/20-*).
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
  "$IMAGE" bash evidence/batch2-editor/probe/container-run.sh
