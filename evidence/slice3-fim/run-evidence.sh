#!/usr/bin/env bash
# Reproduce the Slice-3 FIM evidence (task T27). Run from the repository root.
#
#   bash evidence/slice3-fim/run-evidence.sh
#
# Everything runs headless inside the pinned images; no real model endpoint and
# no network access is required (the "configured" run uses a loopback fake
# endpoint started by the selftest itself).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EVID="${VEDIT_FIM_EVIDENCE_DIR:-$ROOT/evidence/slice3-fim/repro}"
QT6_IMAGE="vedit-qt6-build:local"
RUST_IMAGE="vedit-build:local"
mkdir -p "$EVID"

# C9 header: every log this runner writes starts with the collection facts, so a
# re-run never recreates the "no header" defect (T105).
c9_header() {
  local file=$1
  {
    echo '# C9 evidence header'
    echo "collected_at: $(date --iso-8601=seconds)"
    echo "collected_at_utc: $(date -u --iso-8601=seconds)"
    echo "git_head: $(git -C "$ROOT" rev-parse HEAD)"
    echo 'git_status_porcelain:'
    git -C "$ROOT" status --porcelain | sed 's/^/ /'
    echo '---'
  } > "$file"
}

echo "== [1/6] Rust: cargo test (FIM) =="
c9_header "$EVID/01-ffi-rust-tests.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e CARGO_HOME=/tmp/cargo-home -e LANG=C.UTF-8 "$RUST_IMAGE" \
  bash -lc 'cargo test -p vd-ffi fim:: -- --nocapture' | tee -a "$EVID/01-ffi-rust-tests.log"

echo "== [2/6] Rust: cargo test (vd-fim default-off) =="
c9_header "$EVID/02-vdfim-default-off-tests.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e CARGO_HOME=/tmp/cargo-home -e LANG=C.UTF-8 "$RUST_IMAGE" \
  bash -lc 'cargo test -p vd-fim --test default_off' | tee -a "$EVID/02-vdfim-default-off-tests.log"

echo "== [3/6] build the editor-side lane + staticlib =="
c9_header "$EVID/03-staticlib-build.log"
c9_header "$EVID/03-lane-build.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e CARGO_HOME=/tmp/cargo-home -e LANG=C.UTF-8 "$RUST_IMAGE" \
  bash -lc 'cargo build -p vd-ffi --release' >> "$EVID/03-staticlib-build.log" 2>&1
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 "$QT6_IMAGE" \
  bash -lc 'cmake -S /vedit-build/src/editor/fim -B /vedit-build/target/fim-lane -G Ninja \
      -DVEDIT_FFI_STATICLIB=/vedit-build/target/release/libvd_ffi.a \
      -DVEDIT_FFI_INCLUDE_DIR=/vedit-build/crates/vd-ffi/include >/dev/null && \
    cmake --build /vedit-build/target/fim-lane --parallel' >> "$EVID/03-lane-build.log" 2>&1
grep -E 'VEdit FIM' "$EVID/03-lane-build.log" || true

echo "== [4/6] zero-network proof: LD_PRELOAD syscall counter, default off =="
c9_header "$EVID/04-default-off-sockets.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 "$QT6_IMAGE" \
  bash -lc 'gcc -shared -fPIC -O2 -o /tmp/socket_probe.so \
      /vedit-build/evidence/slice3-fim/socket_probe.c -ldl
    set +e
    VEDIT_SOCKET_PROBE_OUT=/tmp/default-off.txt \
    LD_PRELOAD=/tmp/socket_probe.so QT_QPA_PLATFORM=offscreen \
      /vedit-build/target/fim-lane/vedit-fim-selftest --default-off-only
    echo "SELFTEST-EXIT=$?"
    echo "--- syscall counters (no listener in this mode) ---"
    cat /tmp/default-off.txt' | tee -a "$EVID/04-default-off-sockets.log"

echo "== [4b/6] zero-network proof: PRODUCT binary --selfcheck-ffi, default off =="
c9_header "$EVID/04b-product-default-off-sockets.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 "$QT6_IMAGE" \
  bash -lc 'gcc -shared -fPIC -O2 -o /tmp/socket_probe.so \
      /vedit-build/evidence/slice3-fim/socket_probe.c -ldl
    set +e
    VEDIT_SOCKET_PROBE_OUT=/tmp/product.txt \
    LD_PRELOAD=/tmp/socket_probe.so QT_QPA_PLATFORM=offscreen \
      /vedit-build/target/qt6-build/src/app/vedit-app --selfcheck-ffi \
        /vedit-build/crates/vd-ffi/tests/fixtures/project > /tmp/product.log 2>&1
    echo "PRODUCT-SELFCHECK-FFI-EXIT=$?"
    grep -E "CHECK ffi.fim|FFI-ABI-SELFTEST" /tmp/product.log
    echo "--- syscall counters (product process) ---"
    cat /tmp/product.txt' | tee -a "$EVID/04b-product-default-off-sockets.log"

echo "== [5/6] configured: request goes through Rust to the local fake endpoint =="
c9_header "$EVID/05-configured-chain.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 "$QT6_IMAGE" \
  bash -lc 'gcc -shared -fPIC -O2 -o /tmp/socket_probe.so \
      /vedit-build/evidence/slice3-fim/socket_probe.c -ldl
    set +e
    VEDIT_SOCKET_PROBE_OUT=/tmp/configured.txt \
    LD_PRELOAD=/tmp/socket_probe.so QT_QPA_PLATFORM=offscreen \
      /vedit-build/target/fim-lane/vedit-fim-selftest > /tmp/configured.log 2>&1
    echo "SELFTEST-EXIT=$?"
    grep -E "FIM-RUST-REQUEST|FIM-DEFAULT-OFF|FIM-HALF-CONFIGURED|FIM-SELFTEST" /tmp/configured.log
    echo "--- syscall counters (listener + client) ---"
    cat /tmp/configured.txt' | tee -a "$EVID/05-configured-chain.log"

echo "== [6/6] gate: lane ctest + repo ctest + typecheck =="
c9_header "$EVID/06-lane-ctest.log"
c9_header "$EVID/06-root-ctest.log"
c9_header "$EVID/06-typecheck.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 "$QT6_IMAGE" \
  bash -lc 'QT_QPA_PLATFORM=offscreen ctest --test-dir /vedit-build/target/fim-lane --output-on-failure' \
  | tee -a "$EVID/06-lane-ctest.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$QT6_IMAGE" \
  bash -lc 'QT_QPA_PLATFORM=offscreen ctest --test-dir /vedit-build/target/qt6-build --output-on-failure' \
  | tail -4 | tee -a "$EVID/06-root-ctest.log"
set +e
bash "$ROOT/build/gates/typecheck.sh" >> "$EVID/06-typecheck.log" 2>&1
TYPECHECK_EXIT=$?
set -e
echo "TYPECHECK-EXIT=$TYPECHECK_EXIT" | tee -a "$EVID/06-typecheck.log"
