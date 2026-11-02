#!/usr/bin/env bash
# Re-runnable evidence for T106 (FIM asynchronous requests).
#
# C9: EVERY log this script writes starts with the collection header
# (collected_at + git rev-parse HEAD + git status --porcelain), so a verifier can
# tell exactly which tree produced it. This cell was bounced before for missing
# porcelain; the header is emitted from one function so it cannot be forgotten.
set -u
cd "$(dirname "$0")/../.." || exit 2
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

run() {
  name="$1"; shift
  out="evidence/fim-async/${name}"
  {
    emit_header
    echo "command: $*"
    echo
    run_in_qt6_container bash -lc "$*" 2>&1
    status=$?
    echo
    echo "exit_code: ${status}"
  } > "${out}"
  echo "wrote ${out}"
}

case "${1:-all}" in
  selftests)
    run 01-lane-selftest.log "mkdir -p /tmp/xdg-runtime; QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/qt6-build/src/app/editor-lane/fim-lane/vedit-fim-selftest"
    run 02-wiring-selftest.log "mkdir -p /tmp/xdg-runtime; QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/qt6-build/src/app/editor-lane/vedit-fim-wiring-selftest --selfcheck"
    ;;
  gates)
    run 03-ctest.log "ctest --test-dir target/qt6-build --output-on-failure"
    out="evidence/fim-async/04-typecheck.log"
    {
      emit_header
      echo "command: bash build/gates/typecheck.sh"
      echo
      bash build/gates/typecheck.sh 2>&1
      status=$?
      echo
      echo "exit_code: ${status}"
    } > "${out}"
    echo "wrote ${out}"
    ;;
  *)
    "$0" selftests
    "$0" gates
    ;;
esac
