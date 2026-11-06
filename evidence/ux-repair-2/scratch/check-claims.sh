#!/bin/sh
# Re-checks every acceptance claim in REPORT.md against the recorded logs and
# the final source. Exit 0 only when every claim has its evidence.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
L="$ROOT/evidence/ux-repair-2/logs"
fail=0
check() { # <label> <command...>
    label=$1; shift
    if "$@" >/dev/null 2>&1; then
        echo "CLAIM PASS  $label"
    else
        echo "CLAIM FAIL  $label"; fail=1
    fi
}
check "build-qt EXIT=0"                  grep -q '^EXIT=0$' "$L/10-build-qt.log"
check "build-qt ctest 37/37"             grep -q '100% tests passed, 0 tests failed out of 37' "$L/10-build-qt.log"
check "focused ctest 6/6"                grep -q '100% tests passed, 0 tests failed out of 6' "$L/20-focused-ctest.log"
check "app selfcheck 312/0"              grep -q 'checks=312 failures=0' "$L/21-app-selfcheck.log"
check "project selftest 153/0"           grep -q 'checks=153 failures=0' "$L/22-project-selftest.log"
check "palette selftest 46/0"            grep -q 'checks=46 failures=0' "$L/23-palette-selftest.log"
check "palette integration 31/0"         grep -q 'checks=31 failures=0' "$L/24-palette-integration-selftest.log"
check "search selftest 106/0/2"          grep -q 'checks=106 failures=0 skipped=2' "$L/25-search-selftest.log"
check "module-tree 61/0"                 grep -q '61 checks, 0 failure(s)' "$L/26-module-tree-selftest.log"
check "AppRun probe 9/0"                 grep -q 'checks=9 failures=0' "$L/30-apprun-probe-green.log"
check "AppRun RED control catches 4"     grep -q 'checks=9 failures=4' "$L/31-apprun-probe-red.log"
check "docs gate PASS"                   grep -q '\[docs\] PASS' "$L/32-docs-gate.log"

check "F2 checks present"                grep -q 'integration.root-follows-projectRootChanged PASS' "$L/24-palette-integration-selftest.log"
check "F2 rescan check present"          grep -q 'integration.root-rescan-new-root PASS' "$L/24-palette-integration-selftest.log"
check "F4 import-status check present"   grep -q 'filelist.import-status PASS' "$L/22-project-selftest.log"
check "F4 honest-reimport check present" grep -q 'filelist.reimport-status-honest PASS' "$L/22-project-selftest.log"
check "path.display tests intact"        grep -q 'path.display.open-key-still-absolute PASS' "$L/21-app-selfcheck.log"
check "open.signal tests intact"         grep -q 'open.signal.one-per-open PASS' "$L/21-app-selfcheck.log"
check "slang resolver tests intact"      grep -q 'toolchain.env-override-wins PASS\|toolchain.env-override-wins —\|ok   toolchain.env-override-wins' "$L/26-module-tree-selftest.log"

check "AppRun guarded slang export"      grep -qF 'if [ -z "${VEDIT_SLANG:-}" ] && [ -x "$APPDIR/bin/slang" ]; then' "$ROOT/packaging/runtime/AppRun"
check "AppRun probe wired into selfcheck" grep -q 'probe-apprun-env.sh' "$ROOT/packaging/runtime/selfcheck.sh"
check "palette subscribes root change"   grep -q 'projectRootChanged' "$ROOT/src/workspace/palette/palette_integration.cpp"
check "model reports added filelists"    grep -q 'addedFilelists' "$ROOT/src/workspace/project/project_model.cpp"
check "panel prints honest no-op"        grep -q 'Already imported: nothing changed' "$ROOT/src/workspace/project/project_panel.cpp"
check "guide names status exception"     grep -q '一次性确认与错误提示是例外' "$ROOT/docs/USER_GUIDE.md"
check "guide names Refresh re-parse"     grep -q '按 Project 面板的 `Refresh`' "$ROOT/docs/USER_GUIDE.md"
check "guide quotes honest no-op"        grep -q 'Already imported: nothing changed' "$ROOT/docs/USER_GUIDE.md"

echo "CLAIM-CHECK fail=$fail"
[ "$fail" -eq 0 ]
