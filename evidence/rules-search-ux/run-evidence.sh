#!/usr/bin/env bash
# Re-runnable evidence for T114 (optional `.rules.verible_lint` is not an error).
#
# C9: every log starts with collected_at + git rev-parse HEAD + git branch +
# git status --porcelain, emitted by ONE function so a header can never be
# forgotten.
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

run_rust() {
  local name="$1"; shift
  local out="evidence/rules-search-ux/${name}"
  {
    emit_header
    echo "command (pinned Rust image): $*"
    echo
    run_in_build_container bash -lc "$*" 2>&1
    local status=$?
    echo
    echo "exit_code: ${status}"
  } > "${out}"
  echo "wrote ${out}"
}

run_qt() {
  local name="$1"; shift
  local out="evidence/rules-search-ux/${name}"
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
  rust)
    run_rust 01-vd-config-tests.log "cd /vedit-build && cargo test -p vd-config -- --nocapture"
    run_rust 02-vd-ffi-tests.log "cd /vedit-build && cargo test -p vd-ffi -- --nocapture"
    run_rust 03-vd-lsp-line-length.log "cd /vedit-build && VEDIT_VERIBLE_LS=/vedit-build/.mpd/recon/verible/bin/verible-verilog-ls cargo test -p vd-lsp --test verible_ls"
    ;;
  qt)
    run_qt 04-project-lane.log "mkdir -p /tmp/xdg-runtime; QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/qt6-build/src/app/workspace-lane-project/vedit-workspace-project-selftest --selfcheck /vedit-build/tests/fixtures/verilog-project"
    run_qt 05-diagnostics-real-lint.log "mkdir -p /tmp/xdg-runtime; QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/qt6-build/src/app/workspace-lane-diagnostics/vedit-diagnostics-ffi-selftest /vedit-build/tests/fixtures/verilog-project /vedit-build/.mpd/recon/verible/bin/verible-verilog-ls"
    run_qt 06-ctest.log "ctest --test-dir target/qt6-build --output-on-failure"
    ;;
  typecheck)
    out="evidence/rules-search-ux/10-typecheck.log"
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
    "$0" rust
    "$0" qt
    "$0" typecheck
    ;;
esac
