#!/usr/bin/env bash
# Rebuild + re-run the file tree lane (task T21) INSIDE the pinned Qt6/KF6 image:
# an independent configure, the -Werror build, the ctest suite, and the
# screenshot driver on a real filesystem under target/ws-filetree-tmp/.
#
# It is the lane's reproducibility script: a verifier runs this one command and
# gets the same logs and PNGs under evidence/ws-filetree/.
#
#   bash src/workspace/filetree/tools/run-in-container.sh
#
# The image is T10's (scripts/build-qt.sh, docs/BUILD.md §12.2). This script
# never builds or pulls an image and never writes another lane's evidence.
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
echo "[filetree] workspace: $ROOT"
run_in_qt6_container bash src/workspace/filetree/tools/in-container.sh
