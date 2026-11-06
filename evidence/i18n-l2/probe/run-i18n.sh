#!/usr/bin/env bash
# Run the T38 i18n L2 acceptance probe in the pinned image:
#
#   bash evidence/i18n-l2/probe/run-i18n.sh
#
# The pinned image ships only DejaVu, so Chinese renders as tofu boxes in a
# screenshot even when every Qt string is correct. Like the other UI probes
# (evidence/i2-i18n/probe/run.sh, evidence/e1-tabs/probe/run.sh), install the
# product's bundled NotoSansCJK into the container's font path first; the TEXT
# assertions do not depend on it, the screenshot does.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"

docker run --rm \
  -v "$ROOT":/vedit-build -w /vedit-build \
  -e QT_QPA_PLATFORM=offscreen \
  vedit-qt6-build:local \
  bash -lc '
    set -e
    mkdir -p target/i18n-probe/xdg && chmod 700 target/i18n-probe/xdg
    export XDG_RUNTIME_DIR=/vedit-build/target/i18n-probe/xdg
    if [ -f packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc ]; then
      mkdir -p /usr/share/fonts/truetype/vedit
      cp packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc /usr/share/fonts/truetype/vedit/
      fc-cache -f >/dev/null 2>&1 || true
    fi
    ./target/i18n-probe/i18n-probe /vedit-build/target/qt6-build/src/app/translations \
      /vedit-build/evidence/i18n-l2/shots'
