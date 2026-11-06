#!/usr/bin/env bash
# AC-0.4 driver for the batch-3a VCS write foundation evidence.
#
# Every VCS E2E runs in a fresh temporary fixture, and the probe asserts at
# start-up that its working directory is NOT the VEdit working tree. This driver
# records the run context, then executes the two probes inside their pinned
# images:
#
#   10-rust-e2e.log      vd-vcs write/read gate tests + the real-git round trip
#                        (cargo test runs with cwd inside a mktemp -d fixture)
#   20-selftest.log      the C++ lane selftest: write allow-list checks through
#                        the C ABI and the fake-git event-loop probe
#   30-header.log        cbindgen output vs the consumed vd_ffi.h (byte equal)
#
# Usage: bash evidence/vcs-write/run-all.sh
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EVID="$ROOT/evidence/vcs-write"
mkdir -p "$EVID/logs"

{
  echo "# batch 3a VCS write foundation - run context"
  echo "collected_at: $(date --iso-8601=seconds)"
  echo "workspace: $ROOT"
  echo "git_head: $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo '<no git>')"
  echo "host_git: $(git --version)"
  echo "porcelain (declared write scopes):"
  git -C "$ROOT" status --porcelain -- \
    crates/vd-vcs crates/vd-ffi src/workspace/vcs translations | sed 's/^/  /'
  echo "header_sha256: $(sha256sum "$ROOT/crates/vd-ffi/include/vd_ffi.h" | cut -d' ' -f1)"
} | tee "$EVID/logs/00-context.txt"

echo "== rust probe (vedit-build:local, cwd inside a mktemp fixture) =="
docker run --rm \
  -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 \
  -e NO_AT_BRIDGE=1 \
  -v "$ROOT":/vedit-build \
  -w /tmp \
  vedit-build:local bash -lc 'bash /vedit-build/evidence/vcs-write/run-rust-e2e.sh' \
  2>&1 | tee "$EVID/logs/10-rust-e2e.log"

echo "== C++ lane selftest (vedit-qt6-build:local, cwd inside a mktemp fixture) =="
docker run --rm \
  -e HOME=/tmp \
  -e QT_QPA_PLATFORM=offscreen \
  -e NO_AT_BRIDGE=1 \
  -v "$ROOT":/vedit-build \
  -w /tmp \
  vedit-qt6-build:local bash -lc 'bash /vedit-build/evidence/vcs-write/run-selftest.sh' \
  2>&1 | tee "$EVID/logs/20-selftest.log"

echo "== cbindgen byte-identity (vedit-build:local) =="
docker run --rm \
  -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 \
  -v "$ROOT":/vedit-build \
  -w /tmp \
  vedit-build:local bash -lc 'bash /vedit-build/evidence/vcs-write/check-header.sh' \
  2>&1 | tee "$EVID/logs/30-header.log"

echo "[vcs-write] PASS: rust probe + C++ selftest + header identity; logs in $EVID/logs"
