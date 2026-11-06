#!/usr/bin/env bash
# Evidence runner for evidence/ux-path-and-filelist (FIX 1 + FIX 2).
#
# Every log file starts with the exact command that produced it, and ends with
# the observed EXIT code. The heavy gate is `bash scripts/build-qt.sh` (its log
# is 10-build-qt.log, produced before this script); this script re-runs the
# focused checks the two fixes added, inside the same pinned Qt6/KF6 image.
#
#   bash evidence/ux-path-and-filelist/run-checks.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
# shellcheck source=../../scripts/lib-qt.sh
source scripts/lib-qt.sh

LOG="$ROOT/evidence/ux-path-and-filelist/logs"
mkdir -p "$LOG"
fail=0

# record <log-name> <shell command run inside the pinned image, cwd /vedit-build>
record() {
    local log="$1"
    shift
    {
        printf '# container: %s (workspace mounted at /vedit-build)\n' "$QT_BUILD_IMAGE_TAG"
        printf '# cmd:\n#   %s\n' "$*"
    } >"$LOG/$log"
    run_in_qt6_container bash -lc "$*" >>"$LOG/$log" 2>&1
    local rc=$?
    printf '\nEXIT=%d\n' "$rc" >>"$LOG/$log"
    if [ "$rc" -ne 0 ]; then
        fail=1
    fi
    return "$rc"
}

# --- app layer: the shared display rule + the one open signal ----------------
record 20-app-selfcheck.log \
    'cd /vedit-build && QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/ux-selfcheck-state'
{
    printf '# cmd: grep -E "^(CHECK (path\\.display|open\\.filter|open\\.signal)|APP-SHELL-SELFTEST)" %s\n' \
        "logs/20-app-selfcheck.log"
    grep -E "^(CHECK (path\.display|open\.filter|open\.signal)|APP-SHELL-SELFTEST)" \
        "$LOG/20-app-selfcheck.log"
} >"$LOG/21-app-selfcheck-fix-lines.txt"

# --- project lane: display lists + the filelist-open rule, end to end --------
record 22-project-selftest.log \
    'cd /vedit-build && QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/qt6-build/src/app/workspace-lane-project/vedit-workspace-project-selftest --selfcheck tests/fixtures/verilog-project'
{
    printf '# cmd: grep -E "^(CHECK (display\\.|filelist-open\\.|filelist\\.plan)|PROJECT-MODEL-SELFTEST)" %s\n' \
        "logs/22-project-selftest.log"
    grep -E "^(CHECK (display\.|filelist-open\.|filelist\.plan)|PROJECT-MODEL-SELFTEST)" \
        "$LOG/22-project-selftest.log"
} >"$LOG/23-project-selftest-fix-lines.txt"

# --- palette + search: the display rule on their result rows -----------------
record 24-palette-selftest.log \
    'cd /vedit-build && QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/qt6-build/src/app/workspace-lane-palette/vedit-workspace-palette-selftest --screenshots /tmp/ux-palette-shots'
grep -E "^(CHECK index\.(outside-root-absolute|inside-root-relative|display-sorted|add-file)|PALETTE-SELFTEST)" \
    "$LOG/24-palette-selftest.log" >"$LOG/25-palette-display-lines.txt"

record 26-search-selftest.log \
    'cd /vedit-build && QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/qt6-build/src/app/workspace-lane-search/vedit-workspace-search-selftest'
grep -E "^(SEARCH-SELFTEST|CHECK .* (PASS|FAIL))" "$LOG/26-search-selftest.log" | tail -5 \
    >"$LOG/27-search-summary.txt"

# --- Files tree: its tooltip, and the standalone lane build ------------------
# The root build does not register the file-tree suite (VEDIT_BUILD_FILETREE_TESTS
# defaults to standalone-only, which keeps the ctest count at 37), so the suite is
# built and run here exactly as its own CMakeLists documents.
record 30-filetree-standalone-build.log \
    'cd /vedit-build && cmake -S src/workspace/filetree -B target/filetree-lane-build -G Ninja && cmake --build target/filetree-lane-build --parallel 8'
record 31-filetree-standalone-tests.log \
    'cd /vedit-build && QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime target/filetree-lane-build/vedit-filetree-tests'
{
    printf '# cmd: grep "tree.tooltip\\|tree.pathrole" logs/31-filetree-standalone-tests.log\n'
    grep -E "CHECK (tree\.tooltip|tree\.pathrole)" "$LOG/31-filetree-standalone-tests.log"
} >"$LOG/32-filetree-tooltip-lines.txt"

# --- project lane WITHOUT src/app (its own CMake standalone contract) --------
record 34-project-lane-standalone-build.log \
    'cd /vedit-build && cmake -S src/workspace/project -B target/project-lane-build -G Ninja -DVEDIT_RUST_FFI_LIB=/vedit-build/target/release/libvd_ffi.a -DVEDIT_RUST_FFI_INCLUDE=/vedit-build/crates/vd-ffi/include && cmake --build target/project-lane-build --parallel 8'
record 35-project-lane-standalone-test.log \
    'cd /vedit-build && QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-runtime ctest --test-dir target/project-lane-build --output-on-failure'

# --- the gate summary (the log itself is 10-build-qt.log) --------------------
{
    printf '# cmd: grep -E "tests passed|tests failed|Total Test time|PASS: Rust service layer" logs/10-build-qt.log\n'
    grep -E "tests passed|tests failed|Total Test time|PASS: Rust service layer" \
        "$LOG/10-build-qt.log"
    printf '# cmd: cat logs/10-build-qt.exit\n'
    cat "$LOG/10-build-qt.exit"
} >"$LOG/33-gate-summary.txt"

printf 'UX-EVIDENCE fail=%d logs=%s\n' "$fail" "$LOG"
exit "$fail"
