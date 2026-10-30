#!/usr/bin/env bash
# Clean-tree gate capture for the acceptance report. Writes to a temporary file
# first and excludes the destination itself from git status, so the record cannot
# report itself as modified. Run inside the pinned image with the workspace mounted.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/tests/e2e/gates-frozen-run.txt"
TMP=$(mktemp)
cd "$ROOT"
{
  echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "HEAD=$(git rev-parse HEAD)"
  echo "git_status_short (excluding $OUT itself):"
  git status --short -- . ":(exclude)tests/e2e/gates-frozen-run.txt"
  echo "tracked_modified=$(git status --porcelain --untracked-files=no -- . ":(exclude)tests/e2e/gates-frozen-run.txt" | wc -l)"
  echo "\$ cargo fmt --all -- --check"; cargo fmt --all -- --check; echo "exit=$?"
  echo "\$ cargo clippy --workspace --all-targets -- -D warnings"
  cargo clippy --workspace --all-targets -- -D warnings 2>&1 | tail -3; echo "exit=${PIPESTATUS[0]}"
  echo "\$ cargo test --workspace"
  cargo test --workspace 2>&1 | grep -E "test result: (ok|FAILED)"; echo "exit=${PIPESTATUS[0]}"
} > "$TMP" 2>&1
mv "$TMP" "$OUT"
cat "$OUT"
