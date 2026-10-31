#!/usr/bin/env bash
# Host entry point for the 10,000-file Ctrl+P palette benchmark (acceptance A2b).
#
# Runs INSIDE the pinned Qt6 build image (scripts/docker/Dockerfile.build stage
# qt6-build) through scripts/lib-qt.sh, exactly like `scripts/build-qt.sh`, so
# the numbers are produced by the same toolchain as the product build.
#
#   bash src/workspace/palette/tools/bench.sh              # 10000 files, 200 samples/query
#   bash src/workspace/palette/tools/bench.sh 10000 500   # more samples
#
# Output: evidence/ws-palette/31-bench.log (stdout + JSON path),
#         evidence/ws-palette/bench.json (machine-readable).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
# shellcheck source=../../../../scripts/lib-qt.sh
source "$ROOT/scripts/lib-qt.sh"
load_qt_versions

FILES="${1:-10000}"
ITERATIONS="${2:-200}"
mkdir -p "$ROOT/evidence/ws-palette"

echo "[bench] host: $(uname -srm), image: ${QT_BUILD_IMAGE_TAG}"
run_in_qt6_container bash src/workspace/palette/tools/bench-in-container.sh "$FILES" "$ITERATIONS" \
  | tee "$ROOT/evidence/ws-palette/31-bench.log"
echo "[bench] done: evidence/ws-palette/31-bench.log, evidence/ws-palette/bench.json"
