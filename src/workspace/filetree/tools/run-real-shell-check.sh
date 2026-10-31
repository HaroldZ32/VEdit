#!/usr/bin/env bash
# Host entry point for the "the panel is really inside the shell" evidence:
# runs tools/real-shell-check.sh in the pinned image.
#
#   bash scripts/build-qt.sh                                       # the product build
#   bash src/workspace/filetree/tools/run-in-container.sh          # lane build + tests + screenshots
#   bash src/workspace/filetree/tools/run-real-shell-check.sh      # shell seam proof
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
# shellcheck source=../../../../scripts/lib-qt.sh
source "$ROOT/scripts/lib-qt.sh"
load_qt_versions

if ! docker image inspect "$QT_BUILD_IMAGE_TAG" >/dev/null 2>&1; then
  echo "FATAL: the pinned Qt6 image '$QT_BUILD_IMAGE_TAG' is not present locally." >&2
  echo "       Build it once with T10's documented command: bash scripts/build-qt.sh" >&2
  exit 2
fi

echo "[filetree] image: $QT_BUILD_IMAGE_TAG"
run_in_qt6_container bash src/workspace/filetree/tools/real-shell-check.sh
