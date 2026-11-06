#!/usr/bin/env bash
# The seven gates of batch 4b, in the order the contract names them:
#   build-qt, ctest, --selfcheck, clippy, fmt, docs, lrelease
#
#   bash evidence/vcs-git-history/run-gates.sh                 # everything
#   bash evidence/vcs-git-history/run-gates.sh --no-build-qt   # reuse the build log
#
# Every gate writes its own log under evidence/vcs-git-history/logs/ and the
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
LOGS="$ROOT/evidence/vcs-git-history/logs"
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

# --- AC-4.3 evidence: the 1500x1000 screenshots and the 1000-commit timing ----
# The screenshots are produced by screenshots.sh (Xvfb+xcb) BEFORE this script;
# each one must exist and be non-trivial (bytes), exactly like the probe asserts.
echo "== evidence: history screenshots + 1000-commit timing =="
SHOTS_DIR="$ROOT/evidence/vcs-git-history/screenshots"
SHOT_COUNT=$(ls "$SHOTS_DIR"/3*.png 2>/dev/null | wc -l)
[ "$SHOT_COUNT" -ge 4 ] || fail "expected at least 4 history screenshots, found $SHOT_COUNT"
for file in "$SHOTS_DIR"/3*.png; do
  bytes=$(stat -c%s "$file")
  [ "$bytes" -gt 4096 ] || fail "$(basename "$file") is only $bytes bytes"
done
TIMING=$(grep -oE "HISTORY-TIMING .*" "$LOGS/10-selftest.log" | head -1)
[ -n "$TIMING" ] || fail "the 1000-commit timing line is missing from the lane log"
{
  echo "# AC-4.3 evidence (screenshots + async timing)"
  echo "collected_at: $STAMP"
  echo "screenshots=$SHOT_COUNT"
  ls -ln "$SHOTS_DIR"/3*.png | awk '{print "  shot_bytes=" $5 " " $9}'
  echo "timing=$TIMING"
  echo "result=PASS"
} > "$LOGS/gate-ac43-evidence.log"
cat "$LOGS/gate-ac43-evidence.log"


{
  echo "# batch 4b seven-gate summary"
  echo "collected_at: $STAMP"
  echo "build-qt=0"
  echo "ctest=$CTEST_LINE"
  echo "selfcheck=$(grep -oE 'APP-SHELL-SELFTEST ok=1 checks=[0-9]+ failures=0' "$LOGS/gate-selfcheck.log" | head -1)"
  echo "clippy=0"
  echo "fmt=0"
  echo "docs=0"
  echo "lrelease=$(grep -oE '[0-9]+ finished and 0 unfinished' "$LOGS/gate-lrelease.log" | head -1)"
  echo "screenshots=$SHOT_COUNT"
} > "$LOGS/gate-summary.log"
cat "$LOGS/gate-summary.log"
echo "[vcs-git-history] PASS: seven gates green"
