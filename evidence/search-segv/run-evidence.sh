#!/usr/bin/env bash
# Re-runnable evidence for T121 (search selftest SIGSEGV).
# C9: every log starts with collected_at + HEAD + branch + porcelain.
set -u
cd "$(dirname "$0")/../.." || exit 2
source scripts/lib.sh >/dev/null 2>&1
load_versions >/dev/null 2>&1
source scripts/lib-qt.sh >/dev/null 2>&1
load_qt_versions >/dev/null 2>&1
emit_header() {
  echo "collected_at: $(date -Is)"
  echo "git_head: $(git rev-parse HEAD)"
  echo "git_branch: $(git rev-parse --abbrev-ref HEAD)"
  echo "git_status:"
  git status --porcelain | sed 's/^/  /'
  echo
}
out="evidence/search-segv/04-ctest.log"
{
  emit_header
  echo "command (pinned Qt image): ctest --test-dir target/qt6-build --output-on-failure"
  echo
  run_in_qt6_container bash -lc "ctest --test-dir target/qt6-build --output-on-failure" 2>&1
  status=$?
  echo
  echo "exit_code: ${status}"
} > "${out}"
echo "wrote ${out}"
out="evidence/search-segv/05-typecheck.log"
{
  emit_header
  echo "command (host): bash build/gates/typecheck.sh"
  echo
  bash build/gates/typecheck.sh 2>&1
  status=$?
  echo
  echo "exit_code: ${status}"
} > "${out}"
echo "wrote ${out}"
