#!/usr/bin/env bash
# Runs INSIDE the pinned Qt6 build image (invoked by tools/bench.sh).
#
# Configures the palette lane standalone (build tree under target/, git-ignored),
# builds the benchmark, creates a REAL file tree with the requested number of
# files (once, reused on later runs), scans it through WorkspaceFileIndex, then
# measures the filter path and the palette keystroke path and writes
# evidence/ws-palette/bench.json.
set -euo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
FILES="${1:-10000}"
ITERATIONS="${2:-200}"
BUILD_DIR="$WORK/target/ws-palette-bench-build"
FIXTURE="$WORK/target/ws-palette-bench-fixture"
EVIDENCE="$WORK/evidence/ws-palette"

mkdir -p "$EVIDENCE"

echo "[bench] container: $(head -1 /etc/os-release), cmake $(cmake --version | head -1 | awk '{print $3}'), g++ $(g++ -dumpversion)"
echo "[bench] configure palette lane standalone (target/, git-ignored)"
cmake -S "$WORK/src/workspace/palette" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -3
cmake --build "$BUILD_DIR" --parallel "$(nproc)" \
  --target vedit-workspace-palette-bench 2>&1 | tail -3

echo "[bench] fixture: $FIXTURE"
echo "[bench] run: files=$FILES iterations=$ITERATIONS limit=100 budget=200ms"
QT_QPA_PLATFORM=offscreen \
  "$BUILD_DIR/vedit-workspace-palette-bench" \
    --files "$FILES" \
    --iterations "$ITERATIONS" \
    --limit 100 \
    --fixture "$FIXTURE" \
    --json "$EVIDENCE/bench.json" \
    --assert-p95-ms 200
