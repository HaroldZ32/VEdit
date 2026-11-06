#!/usr/bin/env bash
# Rust-side E2E probe. Runs INSIDE the pinned Rust image with cwd NOT the
# workspace (the driver passes -w /tmp).
#
# AC-0.4: the first thing this probe does is refuse to run in the VEdit working
# tree; the real work then happens in a fresh mktemp -d fixture.
set -euo pipefail

WORKSPACE=/vedit-build
if [ "$(pwd -P)" = "$WORKSPACE" ]; then
  echo "FATAL: the probe must not run inside the VEdit working tree ($WORKSPACE)" >&2
  exit 1
fi

FIXTURE=$(mktemp -d /tmp/vedit-vcs-write-XXXXXX)
echo "# rust probe"
echo "cwd=$(pwd -P)"
echo "fixture=$FIXTURE"
echo "git=$(git --version)"
echo "cargo=$(cargo --version)"

# Snapshot the worktree: the probe must not change it. `cd` into the workspace
# instead of git's `-C`: the image's git is 1.8.3 (the CentOS 7 baseline), which
# has no `-C`.
worktree_status() {
  ( cd "$WORKSPACE" && git status --porcelain -- \
      crates/vd-vcs crates/vd-ffi src/workspace/vcs translations )
}
BEFORE=$(worktree_status)
echo "worktree_snapshot_before_entries=$(printf '%s\n' "$BEFORE" | grep -cve '^$' || true)"

cd "$FIXTURE"
echo "fixture_cwd=$(pwd -P)"

echo "== vd-vcs gate tests (positive + negative groups) =="
cargo test --manifest-path "$WORKSPACE/Cargo.toml" -p vd-vcs --test write --test readonly \
  -- --nocapture

echo "== vd-ffi write job through the C ABI =="
cargo test --manifest-path "$WORKSPACE/Cargo.toml" -p vd-ffi --test abi -- vcs_write --nocapture

AFTER=$(worktree_status)
echo "worktree_snapshot_after_entries=$(printf '%s\n' "$AFTER" | grep -cve '^$' || true)"
if [ "$BEFORE" != "$AFTER" ]; then
  echo "FAIL: the probe changed the VEdit working tree" >&2
  diff <(printf '%s\n' "$BEFORE") <(printf '%s\n' "$AFTER") >&2 || true
  exit 1
fi
echo "worktree_unchanged=yes"
echo "fixture_kept_for_inspection=$FIXTURE"
