#!/usr/bin/env bash
# Batch 6b (AC-6.2/6.3/6.4/6.5/6.6/6.7) — one-command evidence run.
#
#   bash evidence/svn-e2e/run-all.sh             # full (rebuilds the Qt6 product)
#   bash evidence/svn-e2e/run-all.sh --no-build  # reuse the existing build
#
# Steps (each writes its own log under evidence/svn-e2e/logs/):
#   1. bundled toolchain + capability probe (versions, loader/readelf/ldd, svn help)
#   2. the full SVN working-copy cycle in the pinned CentOS 7.9 build image
#   3. the Rust SVN E2E through the product path (bundled svn + build-only svnadmin)
#   4. the Rust worktree E2E through the product path on the bundled git 2.53.0
#      (AC-6.7; log lands in evidence/vcs-git-workspace/logs/ + a copy here)
#   5. the lane selftest: capability table + panel parity + real SVN panel E2E
#   6. the 1500x1000 xcb screenshots (SVN panel, stub + real working copy)
#   7. the seven gates
#   8. a credential scan over this batch's evidence (must be 0 matches)
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LOGS="$ROOT/evidence/svn-e2e/logs"
APP="$ROOT/packaging/runtime/out/AppDir"
SVN="$APP/bin/svn"
GIT="$APP/bin/git"
ADMIN="$ROOT/build/vcs-tools/svnadmin"
WORKTREE_LOG="$ROOT/evidence/vcs-git-workspace/logs/45-worktree-e2e-bundled.log"
RUN_BUILD=1
for arg in "$@"; do
  case "$arg" in
    --no-build) RUN_BUILD=0 ;;
    *) echo "FATAL: unknown argument $arg" >&2; exit 2 ;;
  esac
done
mkdir -p "$LOGS" "$ROOT/evidence/svn-e2e/screenshots" "$(dirname "$WORKTREE_LOG")"

# Never run the probes inside the working tree: every fixture is a /tmp directory.
if [ "$(pwd -P)" = "$ROOT" ]; then
  echo "FATAL: run the probes from OUTSIDE the VEdit working tree" >&2
  exit 1
fi
fail() { echo "FAIL: $*" >&2; exit 1; }
[ -x "$SVN" ] || fail "the bundled svn is missing: $SVN"
[ -x "$GIT" ] || fail "the bundled git is missing: $GIT"
[ -x "$ADMIN" ] || fail "the build-only svnadmin is missing: $ADMIN"

# --- 0/8 build the product (the lane and screenshots use this binary) ------
if [ "$RUN_BUILD" = "1" ]; then
  echo "== step 0/8: build-qt (the artifact the lane + screenshots use) =="
  if ! bash "$ROOT/scripts/build-qt.sh" > "$LOGS/gate-build-qt.log" 2>&1; then
    fail "scripts/build-qt.sh failed (see $LOGS/gate-build-qt.log)"
  fi
  grep -q "\[qt6\] PASS" "$LOGS/gate-build-qt.log" || fail "build-qt did not print its PASS line"
  echo "   build-qt PASS (log: gate-build-qt.log)"
fi
[ -s "$LOGS/gate-build-qt.log" ] || fail "no build log; run without --no-build first"

# --- 1/8 toolchain + capability probe --------------------------------------
echo "== step 1/8: toolchain / capability probe =="
bash "$ROOT/evidence/svn-e2e/tools/probe-toolchain.sh" >/dev/null
grep -q "svn, version 1.14.5" "$LOGS/20-toolchain-capabilities.log" \
  || fail "the bundled svn is not 1.14.5"
grep -q "result=PASS" "$LOGS/20-toolchain-capabilities.log" || fail "the probe failed"
echo "   bundled svn + loader facts recorded"

# --- 2/8 clean-container cycle ---------------------------------------------
echo "== step 2/8: SVN cycle in the pinned CentOS 7.9 image =="
bash "$ROOT/evidence/svn-e2e/tools/clean-container-cycle.sh" >/dev/null
grep -q "result=PASS" "$LOGS/40-clean-container-cycle.log" || fail "the container cycle failed"
grep -q "r3 |" "$LOGS/40-clean-container-cycle.log" || fail "the container cycle did not reach r3"
echo "   container cycle: create/checkout/add/commit/status/info/diff/revert/update/remove/log"

# --- 3/8 product-path SVN E2E (bundled client) -----------------------------
echo "== step 3/8: Rust SVN E2E through vd_vcs (bundled svn) =="
docker run --rm \
  -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 \
  -e VEDIT_SVN_BIN=/vedit-build/packaging/runtime/out/AppDir/bin/svn \
  -e VEDIT_SVNADMIN=/vedit-build/build/vcs-tools/svnadmin \
  -v "$ROOT:/vedit-build" \
  -w /vedit-build \
  vedit-build:local \
  bash -lc 'cargo test -p vd-vcs --test svn_e2e -- --nocapture --test-threads=1' \
  > "$LOGS/30-rust-svn-e2e.log" 2>&1 || fail "the Rust SVN E2E failed (see $LOGS/30-rust-svn-e2e.log)"
grep -q "test result: ok. 1 passed" "$LOGS/30-rust-svn-e2e.log" \
  || fail "the Rust SVN E2E did not run (was it skipped?)"
grep -q "EVIDENCE bundled svn=1.14.5" "$LOGS/30-rust-svn-e2e.log" \
  || fail "the Rust SVN E2E did not record the bundled version"
echo "   product path: status/info/diff + add/commit/revert/update/remove against 1.14.5"

# --- 4/8 bundled worktree E2E (AC-6.7) -------------------------------------
echo "== step 4/8: Rust worktree E2E through vd_vcs (bundled git 2.53.0) =="
docker run --rm \
  -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 \
  -e VEDIT_GIT_BIN=/vedit-build/packaging/runtime/out/AppDir/bin/git \
  -v "$ROOT:/vedit-build" \
  -w /vedit-build \
  vedit-build:local \
  bash -lc 'cargo test -p vd-vcs --test worktree_e2e -- --nocapture --test-threads=1' \
  > "$WORKTREE_LOG" 2>&1 || fail "the bundled worktree E2E failed (see $WORKTREE_LOG)"
grep -q "test result: ok. 1 passed" "$WORKTREE_LOG" || fail "the worktree E2E did not run"
grep -q "EVIDENCE bundled git version 2.53.0" "$WORKTREE_LOG" \
  || fail "the worktree E2E did not record the bundled git version"
cp "$WORKTREE_LOG" "$LOGS/45-worktree-e2e-bundled.log"
echo "   worktree add/list/remove through the product path on bundled git 2.53.0"

# --- 5/8 lane selftest ------------------------------------------------------
echo "== step 5/8: lane selftest (capability table + panel + real SVN E2E) =="
bash "$ROOT/evidence/svn-e2e/run-selftest.sh" >/dev/null
grep -q "WORKSPACE-VCS-SELFTEST ok=1 checks=" "$LOGS/10-selftest.log" \
  || fail "the lane selftest is not green"
for name in vcs.svn.e2e.backend vcs.svn.e2e.add-ok vcs.svn.e2e.commit-ok \
            vcs.svn.e2e.revert-ok vcs.svn.e2e.update-ok vcs.svn.e2e.remove-ok \
            vcs.svn.panel.capability-line vcs.capability.svn.disabled-count \
            vcs.svn.write.remove-double-confirmed vcs.i18n.svn; do
  grep -q "CHECK $name PASS" "$LOGS/10-selftest.log" || fail "lane check $name is not green"
done
echo "   lane: $(grep -oE 'checks=[0-9]+ failures=[0-9]+' "$LOGS/10-selftest.log" | head -1)"

# --- 6/8 xcb screenshots ----------------------------------------------------
echo "== step 6/8: xcb screenshots (1500x1000) =="
bash "$ROOT/evidence/svn-e2e/screenshots.sh" >/dev/null
grep -q "screenshots ok=1" "$LOGS/20-screenshots.log" || fail "the screenshot probe is not green"
for shot in 60-vcs-svn-panel-1500x1000.png 61-vcs-svn-real-1500x1000.png; do
  [ -s "$ROOT/evidence/svn-e2e/screenshots/$shot" ] || fail "missing screenshot $shot"
done
echo "   shots: 60 (stub panels) + 61 (REAL subversion working copy)"

# --- 7/8 seven gates --------------------------------------------------------
echo "== step 7/8: seven gates =="
# The build already ran in step 0 (or was reused with --no-build), so the gate
# script validates that log instead of building a second time.
bash "$ROOT/evidence/svn-e2e/run-gates.sh" --no-build-qt
echo "   seven gates green (logs: $LOGS/gate-*.log)"

# --- 8/8 credentials --------------------------------------------------------
echo "== step 8/8: credential scan (must be 0 matches) =="
CRED="$LOGS/50-credential-scan.log"
{
  echo "# credential material in this batch's evidence (AC-6.4)"
  echo "collected_at: $(date --iso-8601=seconds)"
  echo "patterns: --password, password=, Authorization: Basic, scheme://user:pass@, svn://user:pass@"
  echo "scope: evidence/svn-e2e/logs, evidence/vcs-git-workspace/logs/45-*, evidence/vcs-toolchain/logs"
} > "$CRED"
if grep -rInE -- '(--password|password=[^<]|Authorization:[[:space:]]*Basic|(svn|https?)://[^@/[:space:]]+:[^@/[:space:]]+@)' \
     "$LOGS" "$WORKTREE_LOG" 2>/dev/null >> "$CRED"; then
  echo "FAIL: credential material found (see $CRED)" >&2
  exit 1
fi
echo "matches=0" >> "$CRED"
echo "   matches=0"

echo
echo "[svn-e2e] PASS: AC-6.2/6.3/6.4/6.5/6.6/6.7 evidence complete"
