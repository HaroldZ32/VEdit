#!/usr/bin/env bash
# Run the T27 status-line i18n probe in the pinned image (offscreen):
#
#   bash evidence/i18n-l2/probe/run-status-i18n.sh
#
# The pinned image ships only DejaVu; the probe's bundled NotoSansCJK is installed
# first so the screenshots render Chinese instead of tofu boxes (the TEXT
# assertions do not depend on it — same convention as run-i18n.sh).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"

docker run --rm \
  -v "$ROOT":/vedit-build -w /vedit-build \
  -e QT_QPA_PLATFORM=offscreen \
  vedit-qt6-build:local \
  bash -lc '
    set -e
    mkdir -p target/i18n-status-probe/xdg evidence/i18n-l2/shots-t27
    chmod 700 target/i18n-status-probe/xdg
    export XDG_RUNTIME_DIR=/vedit-build/target/i18n-status-probe/xdg
    if [ -f packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc ]; then
      mkdir -p /usr/share/fonts/truetype/vedit
      cp packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc /usr/share/fonts/truetype/vedit/
      fc-cache -f >/dev/null 2>&1 || true
    fi
    ./target/i18n-status-probe/status-i18n-probe \
      /vedit-build/target/qt6-build/src/app/translations \
      /vedit-build/evidence/i18n-l2/shots-t27
'
