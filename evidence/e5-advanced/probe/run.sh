#!/usr/bin/env bash
# Run the T16 advanced-features probe in the pinned image:
#
#   bash evidence/e5-advanced/probe/run.sh [screenshot]
#
# It drives the REAL product shell, the REAL editor provider and the diagnostics
# lane. For AC-3.5 it switches the LSP backend to `slang` pointed at
# evidence/e5-advanced/fixtures/fake_lsp_server.py (a REAL LSP process used only
# as a protocol fixture - see that file's header for why no shipped backend can
# advertise hover in this image). The server's own request log is written to
# evidence/e5-advanced/logs/05-fake-lsp-requests.log.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
SHOT=${1:-evidence/e5-advanced/20-advanced-menu.png}
HOVER_SHOT=${2:-evidence/e5-advanced/21-hover-rendered.png}
SHOT_REL=${SHOT#"$ROOT"/}
HOVER_REL=${HOVER_SHOT#"$ROOT"/}
mkdir -p "$ROOT/evidence/e5-advanced/logs"
chmod +x "$ROOT/evidence/e5-advanced/fixtures/fake_lsp_server.py"

cd "$ROOT"
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime

docker run --rm \
  -v "$ROOT":/vedit-build \
  -w /vedit-build \
  -e QT_QPA_PLATFORM=offscreen \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime \
  -e VEDIT_VERIBLE_LS_DIR=/vedit-build/.mpd/recon/verible/bin \
  -e VEDIT_VERIBLE_FORMAT=/vedit-build/.mpd/recon/verible/bin/verible-verilog-format \
  -e VEDIT_VERIBLE_LINT=/vedit-build/.mpd/recon/verible/bin/verible-verilog-lint \
  -e VEDIT_SLANG=/vedit-build/build/slang/out/slang \
  -e VEDIT_FAKE_LSP=/vedit-build/evidence/e5-advanced/fixtures/fake_lsp_server.py \
  -e FAKE_LSP_LOG=/vedit-build/evidence/e5-advanced/logs/05-fake-lsp-requests.log \
  -e VEDIT_TOOLCHAIN_TRACE=1 \
  vedit-qt6-build:local \
  bash -lc "./target/e5-advanced-probe/advanced-probe '$SHOT_REL' '$HOVER_REL'"
