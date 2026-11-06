#!/usr/bin/env bash
# Run the T37 module-tree recheck probe in the pinned image:
#
#   bash evidence/m-tree/probe/run-t37.sh [screenshot-path]
#
# Default screenshot: evidence/m-tree/t37-module-tree.png (repo-relative).
# The probe drives the REAL product shell (ApplicationShell + installModuleTree)
# and asserts the three T46 path segments + their exact double-click jumps.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
SHOT=${1:-evidence/m-tree/t37-module-tree.png}
SHOT_REL=${SHOT#"$ROOT"/}

cd "$ROOT"
docker run --rm \
  -v "$ROOT":/vedit-build -w /vedit-build \
  -e QT_QPA_PLATFORM=offscreen \
  -e VEDIT_SLANG=/vedit-build/build/slang/out/slang \
  vedit-qt6-build:local \
  bash -lc "./target/t37-probe/t37-module-tree-probe evidence/m-tree/fixture-t37 '$SHOT_REL'"
