#!/usr/bin/env bash
# The seven gates of batch 4a, in the order the contract names them:
#   build-qt, ctest, --selfcheck, clippy, fmt, docs, lrelease
#
#   bash evidence/vcs-git-remote/run-gates.sh                 # everything
#   bash evidence/vcs-git-remote/run-gates.sh --no-build-qt   # reuse the build log
#
# Every gate writes its own log under evidence/vcs-git-remote/logs/ and the
# script fails on the first red one. `build-qt` is the artifact gate: it rebuilds
# the Rust service layer (including `cargo test -p vd-ffi -p vd-vcs`), runs
# cbindgen, reconfigures cmake, builds vedit-app, runs ctest (which includes the
# lane selftest) and the xcb/software-rendering window smoke.
#
# A last step scans THIS directory for the credential sentinel the lane uses: the
# scan must report zero matches, which is the "credentials are not in the logs or
# the evidence" claim of AC-4.4.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LOGS="$ROOT/evidence/vcs-git-remote/logs"
mkdir -p "$LOGS"
STAMP=$(date --iso-8601=seconds)
RUN_BUILD=1
for arg in "$@"; do
  case "$arg" in
    --no-build-qt) RUN_BUILD=0 ;;
    *) echo "FATAL: unknown argument $arg" >&2; exit 2 ;;
  esac
done

fail() { echo "GATE FAIL: $*" >&2; exit 1; }

# --- 1/7 build-qt ----------------------------------------------------------
if [ "$RUN_BUILD" = "1" ]; then
  echo "== gate 1/7: build-qt =="
  if ! bash "$ROOT/scripts/build-qt.sh" > "$LOGS/gate-build-qt.log" 2>&1; then
    fail "bash scripts/build-qt.sh exited non-zero (see $LOGS/gate-build-qt.log)"
  fi
fi
[ -s "$LOGS/gate-build-qt.log" ] || fail "no build-qt log; run without --no-build-qt first"
tail -3 "$LOGS/gate-build-qt.log"
grep -q "\[qt6\] PASS" "$LOGS/gate-build-qt.log" || fail "build-qt did not print its PASS line"
echo "gate build-qt: PASS exit=0"

# --- 2/7 ctest (part of the build-qt lane; quoted separately) --------------
echo "== gate 2/7: ctest =="
grep -E "tests passed|tests failed" "$LOGS/gate-build-qt.log" > "$LOGS/gate-ctest.log" || true
grep -qE "100% tests passed, 0 tests failed out of [0-9]+" "$LOGS/gate-build-qt.log" \
  || fail "ctest is not 100%/0 failed (see $LOGS/gate-ctest.log)"
CTEST_LINE=$(grep -E "100% tests passed, 0 tests failed out of [0-9]+" "$LOGS/gate-build-qt.log" | tail -1)
grep -E "vedit-workspace-vcs-selftest" "$LOGS/gate-build-qt.log" >> "$LOGS/gate-ctest.log" || true
echo "gate ctest: PASS ($CTEST_LINE)"

# --- 3/7 product --selfcheck ----------------------------------------------
echo "== gate 3/7: vedit-app --selfcheck =="
docker run --rm -v "$ROOT":/vedit-build -w /tmp -e QT_QPA_PLATFORM=offscreen \
  -e NO_AT_BRIDGE=1 vedit-qt6-build:local \
  bash -lc 'mkdir -p /tmp/xdg && chmod 700 /tmp/xdg; XDG_RUNTIME_DIR=/tmp/xdg \
    /vedit-build/target/qt6-build/src/app/vedit-app --selfcheck' \
  > "$LOGS/gate-selfcheck.log" 2>&1 || fail "vedit-app --selfcheck exited non-zero"
grep -q "APP-SHELL-SELFTEST ok=1 " "$LOGS/gate-selfcheck.log" || fail "selfcheck did not report ok=1"
grep -E "APP-SHELL-SELFTEST|^CHECK .* FAIL" "$LOGS/gate-selfcheck.log" | tail -3
echo "gate selfcheck: PASS"

# --- 4/7 clippy ------------------------------------------------------------
# The host has no GTK3/GtkSourceView development files, so the Rust compile gates
# run in the pinned deploy image - the same fallback scripts/gates.sh takes
# (AGENT.md section 4).
echo "== gate 4/7: clippy (pinned image vedit-build:local) =="
docker run --rm \
  -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 \
  -v "$ROOT:/vedit-build" \
  -w /vedit-build \
  vedit-build:local \
  bash -lc 'cargo clippy --workspace --all-targets -- -D warnings' \
  > "$LOGS/gate-clippy.log" 2>&1 || fail "clippy is red (see $LOGS/gate-clippy.log)"
tail -2 "$LOGS/gate-clippy.log"
echo "gate clippy: PASS"

# --- 5/7 fmt ---------------------------------------------------------------
echo "== gate 5/7: fmt =="
( cd "$ROOT" && CARGO_HOME="$ROOT/target/cargo-home" RUSTUP_TOOLCHAIN=stable \
    cargo fmt --all -- --check ) > "$LOGS/gate-fmt.log" 2>&1 \
  || fail "cargo fmt --all -- --check is red (see $LOGS/gate-fmt.log)"
echo "gate fmt: PASS"

# --- 6/7 docs --------------------------------------------------------------
echo "== gate 6/7: docs =="
bash "$ROOT/build/gates/docs.sh" > "$LOGS/gate-docs.log" 2>&1 || fail "docs gate is red"
tail -1 "$LOGS/gate-docs.log"
echo "gate docs: PASS"

# --- 7/7 lrelease ----------------------------------------------------------
echo "== gate 7/7: lrelease =="
docker run --rm -v "$ROOT":/vedit-build -w /tmp vedit-qt6-build:local \
  bash -lc '/usr/lib/qt6/bin/lrelease /vedit-build/translations/vedit_zh_CN.ts \
              -qm /tmp/vedit_zh_CN.qm' > "$LOGS/gate-lrelease.log" 2>&1 \
  || fail "lrelease exited non-zero (see $LOGS/gate-lrelease.log)"
cat "$LOGS/gate-lrelease.log"
grep -qE "\([0-9]+ finished and 0 unfinished\)" "$LOGS/gate-lrelease.log" \
  || fail "lrelease reports unfinished translations (see $LOGS/gate-lrelease.log)"
echo "gate lrelease: PASS (0 unfinished)"

# --- credential sentinel: the evidence directory must not contain it --------
# The sentinel is DEFINED once in the lane source (a test constant) so that this
# directory can be scanned without this script carrying the literal: a match here
# would be a leak, exactly what the scan is looking for.
echo "== scan: credential sentinel must not appear in evidence/ =="
SENTINEL=$(grep -oE 'e2e-secret-[0-9a-z]+' "$ROOT/src/workspace/vcs/vcs_selftest.cpp" | head -1)
[ -n "$SENTINEL" ] || fail "the credential sentinel is not defined in the lane source"
SENTINEL_LINE=$(grep -n "e2e-secret-" "$ROOT/src/workspace/vcs/vcs_selftest.cpp" | head -1 | cut -d: -f1)
MATCHES=$( { grep -r --binary-files=text -n -- "$SENTINEL" "$ROOT/evidence/vcs-git-remote" || true; } | wc -l )
{
  echo "# credential sentinel scan (AC-4.4)"
  echo "collected_at: $STAMP"
  echo "sentinel_defined_at=src/workspace/vcs/vcs_selftest.cpp:$SENTINEL_LINE"
  echo "sentinel_length=${#SENTINEL}"
  echo "scanned=$ROOT/evidence/vcs-git-remote (recursive, binary files read as text)"
  echo "matches=$MATCHES"
  echo "result=$([ "$MATCHES" -eq 0 ] && echo PASS || echo FAIL)"
} > "$LOGS/gate-sensitive-scan.log"
cat "$LOGS/gate-sensitive-scan.log"
[ "$MATCHES" -eq 0 ] || fail "the credential sentinel appears in the evidence directory"

{
  echo "# batch 4a seven-gate summary"
  echo "collected_at: $STAMP"
  echo "build-qt=0"
  echo "ctest=$CTEST_LINE"
  echo "selfcheck=$(grep -oE 'APP-SHELL-SELFTEST ok=1 checks=[0-9]+ failures=0' "$LOGS/gate-selfcheck.log" | head -1)"
  echo "clippy=0"
  echo "fmt=0"
  echo "docs=0"
  echo "lrelease=$(grep -oE '[0-9]+ finished and 0 unfinished' "$LOGS/gate-lrelease.log" | head -1)"
  echo "credential-sentinel-matches=0"
} > "$LOGS/gate-summary.log"
cat "$LOGS/gate-summary.log"
echo "[vcs-git-remote] PASS: seven gates green"
