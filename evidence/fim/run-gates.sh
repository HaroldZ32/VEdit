#!/bin/sh
# T19 scope gates. Run from the repository root inside the pinned CentOS 7 build
# image (see README.md). Writes evidence/fim/02-scope-gates.log.
#
# The repository-wide gates are deliberately NOT run here: every lane shares one
# working tree, so `cargo test --workspace` also compiles other lanes' in-flight
# code. `-p vd-fim` is this lane's isolated judgement; the workspace gate is
# re-run at the integration points (T21/T27/T28).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
LOG="$HERE/02-scope-gates.log"
cd "$ROOT" || exit 1

{
  echo "# T19 scope gates — pinned centos:7 build image, run at $(date -u +%FT%TZ)"
  echo "# working tree (uncommitted at run time); repo-wide gates are not this lane's judgement"
  echo
  echo "\$ cargo fmt -p vd-fim -- --check"
  cargo fmt -p vd-fim -- --check 2>&1
  echo "EXIT=$?"
  echo
  echo "\$ cargo clippy -p vd-fim --all-targets -- -D warnings"
  cargo clippy -p vd-fim --all-targets -- -D warnings 2>&1
  echo "EXIT=$?"
  echo
  echo "\$ cargo test -p vd-fim"
  cargo test -p vd-fim 2>&1
  echo "EXIT=$?"
} > "$LOG" 2>&1
