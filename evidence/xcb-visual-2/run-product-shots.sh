#!/usr/bin/env bash
# 7c: screenshots of the SHIPPED product binary (`vedit-app --screenshots`) under
# a real Xvfb xcb server: horizontal split, vertical split, all panels and docks,
# and the empty no-project state. This runs the finished product (not a probe),
# at the size the product itself chooses (1440x900); the panel/VCS shots are the
# 1500x1000 ones.
#
#   bash evidence/xcb-visual-2/run-product-shots.sh
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$ROOT/evidence/xcb-visual-2/shots/product" "$ROOT/evidence/xcb-visual-2/logs"

docker run --rm \
  -v "$ROOT":/vedit-build \
  -w /tmp \
  -e NO_AT_BRIDGE=1 \
  -e LIBGL_ALWAYS_SOFTWARE=1 \
  -e QT_XCB_GL_INTEGRATION=none \
  vedit-qt6-build:local \
  bash -lc '
    set -euo pipefail
    mkdir -p /tmp/xdg-run && chmod 700 /tmp/xdg-run
    export XDG_RUNTIME_DIR=/tmp/xdg-run
    Xvfb :85 -screen 0 1500x1000x24 >/tmp/xvfb-7c-product.log 2>&1 &
    xvfb=$!
    trap "kill $xvfb 2>/dev/null || true" EXIT
    sleep 2
    BIN=/vedit-build/target/qt6-build/src/app/vedit-app
    [ -x "$BIN" ] || { echo "FATAL: $BIN is missing; run bash scripts/build-qt.sh first" >&2; exit 1; }
    {
      echo "# 7c product screenshots (Xvfb :85, screen 1500x1000x24, QT_QPA_PLATFORM=xcb)"
      echo "collected_at: $(date --iso-8601=seconds)"
      echo "product_sha256=$(sha256sum $BIN | cut -d\" \" -f1)"
      echo "xvfb=$(pgrep -a Xvfb | head -1)"
    } | tee /vedit-build/evidence/xcb-visual-2/logs/30-product-screenshots.log
    DISPLAY=:85 QT_QPA_PLATFORM=xcb QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1 \
      "$BIN" --screenshots /vedit-build/evidence/xcb-visual-2/shots/product \
      2>&1 | tee -a /vedit-build/evidence/xcb-visual-2/logs/30-product-screenshots.log
    grep -q "APP-SHELL-SCREENSHOTS ok=1" \
      /vedit-build/evidence/xcb-visual-2/logs/30-product-screenshots.log || {
        echo "FAIL: the product screenshot capture did not report ok=1" >&2
        exit 1
      }
    ls -la /vedit-build/evidence/xcb-visual-2/shots/product
    echo "product-screenshots=PASS"
  '

echo "[xcb-visual-2] PASS: product screenshots in evidence/xcb-visual-2/shots/product/"
