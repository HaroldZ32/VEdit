#!/usr/bin/env bash
# Re-runnable evidence for T117 (crash forensics + fold-crash diagnosis).
# C9: every log starts with collected_at + HEAD + branch + porcelain, emitted by
# one function so it cannot be forgotten.
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

run_qt() {
  local name="$1"; shift
  local out="evidence/crash-segv/${name}"
  {
    emit_header
    echo "command (pinned Qt image): $*"
    echo
    run_in_qt6_container bash -lc "$*" 2>&1
    local status=$?
    echo
    echo "exit_code: ${status}"
  } > "${out}"
  echo "wrote ${out}"
}

case "${1:-all}" in
  crash)
    run_qt 05-reporter-selftest.log "mkdir -p /tmp/xdg-runtime; QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/qt6-build/src/app/vedit-app-crash-reporter-selftest /tmp"
    run_qt 06-shipped-binary-check.log "bash src/app/crash_report_check.sh /vedit-build/target/qt6-build/src/app/vedit-app /tmp/crash-check"
    ;;
  gates)
    run_qt 07-ctest.log "ctest --test-dir target/qt6-build --output-on-failure"
    out="evidence/crash-segv/08-typecheck.log"
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
    ;;
  *)
    "$0" crash
    "$0" gates
    ;;
esac
