#!/usr/bin/env bash
# Run the T20 shell-integration probe in the pinned image:
#
#   bash evidence/t20-shell/probe/run-t20.sh
#
# The probe drives the REAL product shell (ApplicationShell + installPanels +
# the KTextEditor provider) on evidence/t20-shell/fixture and writes the
# screenshots into evidence/t20-shell/shots/.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"

docker run --rm \
  -v "$ROOT":/vedit-build -w /vedit-build \
  -e QT_QPA_PLATFORM=offscreen \
  -e VEDIT_SLANG=/vedit-build/build/slang/out/slang \
  vedit-qt6-build:local \
  bash -lc 'mkdir -p target/t20-probe/xdg && chmod 700 target/t20-probe/xdg;
            export XDG_RUNTIME_DIR=/vedit-build/target/t20-probe/xdg;
            ./target/t20-probe/t20-shell-probe evidence/t20-shell/fixture \
              /vedit-build/evidence/t20-shell/shots'
