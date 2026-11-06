#!/usr/bin/env bash
# 7c: 1500x1000 xcb screenshots of the four workspace panels with the persisted
# project root (Files / Search / VCS / Module Tree), plus the View menu and the
# restored-panels state. Runs the product's own shell through the batch-1 probe
# inside the pinned Qt6/KF6 image.
#
#   bash evidence/xcb-visual-2/run-panels.sh
#
# Evidence: evidence/xcb-visual-2/shots/panels/*.png, logs/10..12-*.log
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
IMAGE="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
[ -f "$ROOT/target/qt6-build/build.ninja" ] || {
  echo "FATAL: target/qt6-build/build.ninja is missing; run bash scripts/build-qt.sh first" >&2
  exit 1
}
docker run --rm \
  -v "$ROOT":/vedit-build \
  -w /vedit-build \
  -e HOME=/tmp \
  -e LANG=C.UTF-8 \
  "$IMAGE" bash evidence/xcb-visual-2/probe/container-run.sh
