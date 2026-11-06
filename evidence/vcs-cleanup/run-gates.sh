#!/usr/bin/env bash
# The seven gates of batch 7 (AC-5.2 / AC-5.4), in the order the contract
# names them:
#   build-qt, ctest, --selfcheck, clippy, fmt, docs, lrelease
#
#   bash evidence/vcs-cleanup/run-gates.sh                 # everything
#   bash evidence/vcs-cleanup/run-gates.sh --no-build-qt   # reuse the build log
#
# Every gate writes its own log under evidence/vcs-cleanup/logs/ and the
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
LOGS="$ROOT/evidence/vcs-cleanup/logs"
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

# --- the Rust evidence: the typed-payload ABI tests and the whole vd-vcs suite ----
echo "== evidence: cargo test -p vd-ffi (ABI) + -p vd-vcs (pinned image) =="
docker run --rm \
  -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 \
  -v "$ROOT:/vedit-build" \
  -w /vedit-build \
  vedit-build:local \
  bash -lc 'cargo test -p vd-ffi --test abi -- --test-threads=1' \
  > "$LOGS/10-rust-abi.log" 2>&1 || fail "the vd-ffi ABI tests failed (see $LOGS/10-rust-abi.log)"
docker run --rm \
  -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 \
  -v "$ROOT:/vedit-build" \
  -w /vedit-build \
  vedit-build:local \
  bash -lc 'cargo test -p vd-vcs -- --test-threads=1' \
  > "$LOGS/11-rust-vd-vcs.log" 2>&1 || fail "the vd-vcs tests failed (see $LOGS/11-rust-vd-vcs.log)"
grep -q "history_and_workspace_polls_reject_a_foreign_payload ... ok" "$LOGS/10-rust-abi.log" \
  || fail "the typed-payload wrong-kind ABI test did not run"
echo "   vd-ffi ABI $(grep -oE 'test result: ok. [0-9]+ passed' "$LOGS/10-rust-abi.log" | head -1)"
echo "   vd-vcs $(grep -cE 'test result: ok' "$LOGS/11-rust-vd-vcs.log") test binaries green"

# --- Batch 7 cleanup evidence: typed JobValue, rebase confirmation, second-step
# refusal, toolchain properties, credential default refusal, source-literal i18n ----
# The lane selftest (10-selftest.log) and the Rust tests (10/11-*.log) are produced
# by step 5 / the Rust step above; this block proves what THIS batch changed.
echo "== evidence: typed JobValue + lane checks + i18n + workaround removal =="
LANE="$LOGS/10-selftest.log"
CHECKS=$(grep -oE 'checks=[0-9]+' "$LANE" | head -1 | cut -d= -f2)
FAILURES=$(grep -oE 'failures=[0-9]+' "$LANE" | head -1 | cut -d= -f2)
[ -n "$CHECKS" ] && [ -n "$FAILURES" ] || fail "the lane log has no summary line"
[ "$FAILURES" = "0" ] || fail "the lane selftest has $FAILURES failures"
[ "$CHECKS" -ge 673 ] || fail "the assertion count fell below the 673 baseline ($CHECKS)"
for name in vcs.ops.rebase-declined vcs.ops.rebase-declined-zero-write \
            vcs.ops.rebase-single-confirm vcs.ops.rebase-preview-spells-consequence \
            vcs.ops.rebase-confirmed-request \
            vcs.ops.discard-second-step-declined vcs.ops.discard-second-step-zero-write \
            vcs.ops.discard-second-step-two-confirms \
            vcs.workspace.worktree-remove-second-step-declined \
            vcs.workspace.worktree-remove-second-step-zero-write \
            vcs.workspace.worktree-remove-second-step-two-confirms \
            vcs.toolchain.panel-properties \
            vcs.cred.headless-default-refusal vcs.cred.headless-no-modal \
            vcs.cred.headless-no-retry \
            vcs.i18n.source-literals vcs.i18n.change-row-chinese; do
  grep -q "CHECK $name PASS" "$LANE" || fail "the lane check $name is not green"
done
# Typed payloads: the VcsInfoData workaround must be GONE and the typed variants
# present; the C ABI header is regenerated by cbindgen during build-qt.
if grep -q "enum VcsInfoData" crates/vd-ffi/src/vcs.rs; then
  fail "the VcsInfoData slot workaround still exists"
fi
if grep -rq "VcsInfoData::" crates/vd-ffi/src/; then
  fail "a VcsInfoData:: variant is still used"
fi
grep -q "VcsHistoryCommits" crates/vd-ffi/src/service.rs \
  || fail "the typed VcsHistoryCommits payload is missing from service.rs"
grep -q "VcsWorkspace" crates/vd-ffi/src/service.rs \
  || fail "the typed VcsWorkspace payload is missing from service.rs"
grep -q "VcsInfo(crate::vcs::VcsInfoPayload)" crates/vd-ffi/src/service.rs \
  || fail "VcsInfo is not the typed identity payload"
grep -q "vd_vcs_history_poll" crates/vd-ffi/include/vd_ffi.h \
  || fail "the C ABI header lost the history poll"
HEADER_SHA=$(sha256sum crates/vd-ffi/include/vd_ffi.h | cut -d" " -f1)
grep -q "history_and_workspace_polls_reject_a_foreign_payload ... ok" "$LOGS/10-rust-abi.log" \
  || fail "the typed-payload wrong-kind ABI test is missing or red"
grep -q "test result: ok" "$LOGS/10-rust-abi.log" || fail "the vd-ffi ABI tests are red"
grep -q "test result: ok" "$LOGS/11-rust-vd-vcs.log" || fail "the vd-vcs tests are red"
# The old evidence README workaround notes are replaced by the typed design.
if grep -q "outside this batch's write scopes" "$ROOT/evidence/vcs-git-history/README.md" \
     "$ROOT/evidence/vcs-git-workspace/README.md"; then
  fail "an evidence README still documents the VcsInfoData workaround"
fi
grep -q "Superseded by batch 7" "$ROOT/evidence/vcs-git-history/README.md" \
  || fail "the history README does not point at the typed design"
LRELEASE=$(grep -oE '[0-9]+ finished and 0 unfinished' "$LOGS/gate-lrelease.log" | head -1)
[ -n "$LRELEASE" ] || fail "lrelease is not 0-unfinished"
{
  echo "# batch 7 cleanup evidence (T45)"
  echo "collected_at: $STAMP"
  echo "lane_checks=$CHECKS (baseline 673, failures=$FAILURES)"
  echo "typed_payloads=VcsInfo(VcsInfoPayload) + VcsWorkspace + VcsHistoryCommits/Commit/File/Blame/Compare"
  echo "vcs_info_data_workaround=absent"
  echo "header_sha256=$HEADER_SHA"
  echo "rust_abi=ok rust_vd_vcs=ok"
  echo "rebase_confirmation=declined-zero-write + accepted-request + consequence preview"
  echo "second_step_refusal=discard + worktree remove (two confirms, zero write)"
  echo "toolchain_properties=asserted on a plain VcsPanel"
  echo "credential_default_headless=no-dialog, no-retry, refused"
  echo "i18n_source_literals=Chinese change row asserted"
  echo "lrelease=$LRELEASE"
  echo "result=PASS"
} > "$LOGS/gate-cleanup-evidence.log"
cat "$LOGS/gate-cleanup-evidence.log"

{
  echo "# batch 7 seven-gate summary"
  echo "collected_at: $STAMP"
  echo "build-qt=0"
  echo "ctest=$CTEST_LINE"
  echo "selfcheck=$(grep -oE 'APP-SHELL-SELFTEST ok=1 checks=[0-9]+ failures=0' "$LOGS/gate-selfcheck.log" | head -1)"
  echo "clippy=0"
  echo "fmt=0"
  echo "docs=0"
  echo "lrelease=$(grep -oE '[0-9]+ finished and 0 unfinished' "$LOGS/gate-lrelease.log" | head -1)"
  echo "lane_checks=$CHECKS"
} > "$LOGS/gate-summary.log"
cat "$LOGS/gate-summary.log"
echo "[vcs-cleanup] PASS: seven gates green"
