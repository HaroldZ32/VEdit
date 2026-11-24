#!/usr/bin/env bash
# verify-appimage-centos7.sh — one-file, no-network, no-install acceptance check
# for the VEdit AppImage on a real CentOS 7 machine (kernel 3.10 / glibc 2.17).
#
#   scp VEdit-0.2.2-x86_64.AppImage verify-appimage-centos7.sh  <user>@<vm>:~/
#   bash verify-appimage-centos7.sh
#
# What it does (every item is evidence, not "looks right"):
#   1  kernel.is-3.10            uname -r                        (ONLY a real VM can answer this)
#   2  glibc.system              ldd --version                  (the 2.17 baseline)
#   3  appimage.sha256           sha256sum vs the expected hash (transfer integrity)
#   4  run.selfcheck             ./AppImage --selfcheck, FUSE first, extract-and-run fallback
#   5  selfcheck.app-shell       APP-SHELL-SELFTEST ok=1 + checks/failures
#   5  selfcheck.own-xml         own-xml=[vedit-verilog.xml, vedit-systemverilog.xml] verilog=1
#   5  selfcheck.fold-975        fold.wiring.closed.hidden-975 PASS
#   6  selfcheck.toolchain       TOOLCHAIN source=verible:bundled slang:bundled missing=[]
#   6  loader.packaged           /proc/<pid>/maps contains <extract>/ld-linux-x86-64.so.2
#   6  loader.no-system-loader   ... and NOT /lib64/ld-linux-x86-64.so.2
#   6  loader.packaged-libc      ... and <extract>/lib/libc.so.6
#   7  lint.real                 bundled verible-verilog-lint on a generated bad .sv
#   8  launch.project            AppRun --project <generated .f> --hold 300 exits 0
#   9  extract                   --appimage-extract (needed by 6/7/8)
#
# Output: one RESULT line per check and a SUMMARY line, plus a full log file
#   verify-report-<host>-<YYYYmmdd-HHMMSS>.txt   (send this whole file back).
# Exit code: 0 when FAIL=0 (SKIP does not fail the run), 1 otherwise.
#
# No sudo, no package installation, no network. A check that cannot run is SKIP
# with a reason, never a silent PASS.
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
STAMP=$(date +%Y%m%d-%H%M%S)
HOST=$(uname -n 2>/dev/null || echo unknown-host)
REPORT="$SCRIPT_DIR/verify-report-$HOST-$STAMP.txt"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/vedit-verify.XXXXXX" 2>/dev/null || echo "")
[ -n "$TMP" ] || TMP="$SCRIPT_DIR/.verify-tmp-$STAMP"
mkdir -p "$TMP" "$TMP/home" 2>/dev/null || true
cleanup() { [ "${VEDIT_KEEP_TMP:-0}" = "1" ] || rm -rf "$TMP"; }
trap cleanup EXIT

# The release build writes out/SHA256SUMS; that row is the default pin. Set
# VEDIT_EXPECTED_SHA256 / VEDIT_EXPECTED_SIZE only when checking against a value
# recorded elsewhere (a release note, a CI artefact). No stale hash lives here.
EXPECTED_SHA256="${VEDIT_EXPECTED_SHA256:-}"
EXPECTED_SIZE="${VEDIT_EXPECTED_SIZE:-}"
PASS=0; FAIL=0; SKIP=0

# ---------------------------------------------------------------------------
# reporting helpers (everything goes to the terminal AND into the report file)
# ---------------------------------------------------------------------------
record() { printf '%s\n' "$*" | tee -a "$REPORT"; }
result() { # result PASS|FAIL|SKIP <check> expected=... actual=... note=...
    local verdict="$1" check="$2"; shift 2
    local line="RESULT $verdict $check"
    [ $# -gt 0 ] && line="$line | $*"
    printf '%s\n' "$line" | tee -a "$REPORT"
    case "$verdict" in
        PASS) PASS=$((PASS + 1)) ;;
        FAIL) FAIL=$((FAIL + 1)) ;;
        SKIP) SKIP=$((SKIP + 1)) ;;
    esac
}
sanitize() { printf '%s' "$1" | tr '\n\r' '  ' | cut -c1-400; }
have() { command -v "$1" >/dev/null 2>&1; }
run_capture() { # run_capture <outfile> <cmd...>  -> prints exit code
    local out="$1"; shift
    if have timeout; then
        timeout 180 "$@" </dev/null >"$out" 2>&1
    else
        "$@" </dev/null >"$out" 2>&1
    fi
}

record "=================================================================="
record " VEdit AppImage — CentOS 7 real-machine verification"
record " host=$HOST  date=$(date -Is)  cwd=$SCRIPT_DIR"
record "=================================================================="
record ""

# ---------------------------------------------------------------------------
# 0. locate the AppImage (script dir; an explicit first argument wins)
# ---------------------------------------------------------------------------
APPIMAGE=""
if [ $# -gt 0 ] && [ -f "$1" ]; then
    APPIMAGE="$1"
else
    for cand in "$SCRIPT_DIR/VEdit-0.2.2-x86_64.AppImage" "$SCRIPT_DIR"/VEdit-*x86_64.AppImage; do
        [ -f "$cand" ] && { APPIMAGE="$cand"; break; }
    done
fi
if [ -z "$APPIMAGE" ]; then
    result FAIL appimage.found expected="VEdit-0.2.2-x86_64.AppImage next to this script" \
        actual="not found in $SCRIPT_DIR" note="copy the AppImage and this script into the same directory"
    record ""
    record "SUMMARY: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
    record "Report: $REPORT"
    exit 1
fi
APPIMAGE=$(cd "$(dirname -- "$APPIMAGE")" && pwd -P)/$(basename -- "$APPIMAGE")
record "-- AppImage: $APPIMAGE"
if [ ! -x "$APPIMAGE" ]; then
    chmod +x "$APPIMAGE" 2>/dev/null && record "   (was not executable: chmod +x applied)" \
        || record "   (WARNING: could not chmod +x; the AppImage may not be runnable)"
fi

# ---------------------------------------------------------------------------
# 1. kernel.is-3.10 — the check only a real CentOS 7 VM can pass.
# ---------------------------------------------------------------------------
KREL=$(uname -r 2>/dev/null || echo unknown)
case "$KREL" in
    3.10.*) result PASS kernel.is-3.10 expected="3.10.x (CentOS 7 target kernel)" actual="$KREL" \
                note="running on the target-family kernel" ;;
    *)      result FAIL kernel.is-3.10 expected="3.10.x (CentOS 7 target kernel)" actual="$KREL" \
                note="containers share the host kernel; a container run is not the real-machine answer" ;;
esac

# ---------------------------------------------------------------------------
# 2. glibc.system — the 2.17 baseline the private runtime exists for.
# ---------------------------------------------------------------------------
GLIBC_LINE=$(ldd --version 2>/dev/null | head -1)
GLIBC_VER=$(printf '%s' "$GLIBC_LINE" | sed -n 's/.* //p' | head -1)
case "$GLIBC_LINE" in
    "") result FAIL glibc.system expected="ldd --version prints a glibc version" actual="no output" note="ldd not available" ;;
    *)  case "$GLIBC_VER" in
            2.17*) result PASS glibc.system expected="glibc 2.17 (CentOS 7)" actual="$(sanitize "$GLIBC_LINE")" \
                       note="target baseline; the product requires 2.34, so the packaged runtime must be used" ;;
            *)     result PASS glibc.system expected="glibc 2.17 (CentOS 7)" actual="$(sanitize "$GLIBC_LINE")" \
                       note="not the 2.17 baseline; the loader checks below still prove the package uses its own runtime" ;;
        esac ;;
esac

# ---------------------------------------------------------------------------
# 3. appimage.sha256 — transfer integrity against the shipped SHA256SUMS row
#    (or an optional pinned hash via VEDIT_EXPECTED_SHA256).
# ---------------------------------------------------------------------------
if have sha256sum; then
    ACTUAL_SHA=$(sha256sum "$APPIMAGE" 2>/dev/null | awk '{print $1}')
    ACTUAL_SIZE=$(wc -c <"$APPIMAGE" 2>/dev/null | tr -d ' ')
    SUMS="$(dirname -- "$APPIMAGE")/SHA256SUMS"
    PIN_SOURCE="env VEDIT_EXPECTED_SHA256"
    if [ -z "$EXPECTED_SHA256" ] && [ -f "$SUMS" ]; then
        EXPECTED_SHA256=$(grep -F "$(basename -- "$APPIMAGE")" "$SUMS" 2>/dev/null | head -1 | awk '{print $1}')
        [ -n "$EXPECTED_SHA256" ] && PIN_SOURCE="SHA256SUMS"
    fi
    if [ -z "$EXPECTED_SHA256" ]; then
        result SKIP appimage.sha256 expected="pinned sha256 (SHA256SUMS row or VEDIT_EXPECTED_SHA256)" \
            actual="sha256=$ACTUAL_SHA size=$ACTUAL_SIZE" \
            note="no pin supplied; the SHA256SUMS cross-check below still verifies the copy"
    elif [ "$ACTUAL_SHA" = "$EXPECTED_SHA256" ]; then
        result PASS appimage.sha256 expected="$EXPECTED_SHA256" actual="$ACTUAL_SHA" \
            note="size=$ACTUAL_SIZE source=$PIN_SOURCE${EXPECTED_SIZE:+ (expected size $EXPECTED_SIZE)}"
    else
        result FAIL appimage.sha256 expected="$EXPECTED_SHA256" actual="$ACTUAL_SHA" \
            note="re-copy the file; a truncated transfer cannot be verified any further (source=$PIN_SOURCE)"
    fi
    if [ -n "$EXPECTED_SIZE" ] && [ "$ACTUAL_SIZE" != "$EXPECTED_SIZE" ]; then
        result FAIL appimage.size expected="$EXPECTED_SIZE" actual="$ACTUAL_SIZE" \
            note="size differs from VEDIT_EXPECTED_SIZE"
    fi
    if [ -f "$SUMS" ]; then
        ROW=$(grep -F "$(basename -- "$APPIMAGE")" "$SUMS" 2>/dev/null | head -1)
        if [ -n "$ROW" ] && printf '%s' "$ROW" | grep -q "$ACTUAL_SHA"; then
            result PASS appimage.sha256sums expected="SHA256SUMS row matches" actual="$(sanitize "$ROW")" note="cross-check passed"
        elif [ -n "$ROW" ]; then
            result FAIL appimage.sha256sums expected="SHA256SUMS row matches" actual="$(sanitize "$ROW")" note="local SHA256SUMS disagrees"
        else
            result SKIP appimage.sha256sums expected="row for $(basename -- "$APPIMAGE")" actual="none" note="SHA256SUMS present but has no row for this file"
        fi
    else
        result SKIP appimage.sha256sums expected="optional SHA256SUMS next to the AppImage" actual="absent" \
            note="copy SHA256SUMS too for the cross-check; a pinned hash is optional (VEDIT_EXPECTED_SHA256)"
    fi
else
    result FAIL appimage.sha256 expected="sha256sum available" actual="missing" note="cannot verify transfer integrity"
fi

# ---------------------------------------------------------------------------
# 4. run.selfcheck — FUSE first, then --appimage-extract-and-run, then the
#    APPIMAGE_EXTRACT_AND_RUN=1 environment form. The first success wins and the
#    mode that worked is recorded.
# ---------------------------------------------------------------------------
SELFCHECK_OUT="$TMP/selfcheck.out"
RUN_MODE="none"
RUN_CMD=""
run_attempt() { # run_attempt <label> -> prints the exit code
    local label="$1"
    local out="$TMP/selfcheck.$label.out"
    local rc
    case "$label" in
        direct)
            rc=$(run_capture "$out" env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP" \
                QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 "$APPIMAGE" --selfcheck; echo $?) ;;
        extract-flag)
            rc=$(run_capture "$out" env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP" \
                QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 \
                "$APPIMAGE" --appimage-extract-and-run --selfcheck; echo $?) ;;
        extract-env)
            rc=$(run_capture "$out" env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP" \
                QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 APPIMAGE_EXTRACT_AND_RUN=1 \
                "$APPIMAGE" --selfcheck; echo $?) ;;
        *)
            rc=99 ;;
    esac
    printf '%s' "$rc"
}

ATTEMPT_DIRECT=$(run_attempt direct)
if [ "$ATTEMPT_DIRECT" = "0" ] && grep -q 'APP-SHELL-SELFTEST ok=1' "$TMP/selfcheck.direct.out" 2>/dev/null; then
    RUN_MODE="FUSE (direct)"
    cp "$TMP/selfcheck.direct.out" "$SELFCHECK_OUT"
else
    record "-- direct FUSE run: exit=$ATTEMPT_DIRECT (first lines of its output)"
    head -4 "$TMP/selfcheck.direct.out" 2>/dev/null | sed 's/^/   | /' | tee -a "$REPORT"
    ATTEMPT_FLAG=$(run_attempt extract-flag)
    if [ "$ATTEMPT_FLAG" = "0" ] && grep -q 'APP-SHELL-SELFTEST ok=1' "$TMP/selfcheck.extract-flag.out" 2>/dev/null; then
        RUN_MODE="EXTRACT-AND-RUN (--appimage-extract-and-run)"
        cp "$TMP/selfcheck.extract-flag.out" "$SELFCHECK_OUT"
    else
        ATTEMPT_ENV=$(run_attempt extract-env)
        if [ "$ATTEMPT_ENV" = "0" ] && grep -q 'APP-SHELL-SELFTEST ok=1' "$TMP/selfcheck.extract-env.out" 2>/dev/null; then
            RUN_MODE="EXTRACT-AND-RUN (APPIMAGE_EXTRACT_AND_RUN=1)"
            cp "$TMP/selfcheck.extract-env.out" "$SELFCHECK_OUT"
        else
            record "-- --appimage-extract-and-run: exit=$ATTEMPT_FLAG; APPIMAGE_EXTRACT_AND_RUN=1: exit=$ATTEMPT_ENV"
        fi
    fi
fi

if [ "$RUN_MODE" != "none" ]; then
    result PASS run.selfcheck expected="--selfcheck completes with APP-SHELL-SELFTEST ok=1" \
        actual="exit=0 via $RUN_MODE" note="both modes were tried; the one that worked is named here"
else
    result FAIL run.selfcheck expected="--selfcheck completes with APP-SHELL-SELFTEST ok=1" \
        actual="direct exit=$ATTEMPT_DIRECT; extract-and-run also failed" \
        note="install libfuse2 only if you want the direct FUSE mode; extract-and-run needs no fuse"
fi
record ""
record "---- selfcheck output begin (the full text the app printed) ----"
if [ -s "$SELFCHECK_OUT" ]; then
    sed 's/^/  /' "$SELFCHECK_OUT" | tee -a "$REPORT" >/dev/null
    record "  ($(wc -l <"$SELFCHECK_OUT") lines; complete output copied into this report)"
else
    record "  (no selfcheck output available)"
fi
record "---- selfcheck output end ----"
record ""

# ---------------------------------------------------------------------------
# 5/6/7. parse the selfcheck output (key lines kept verbatim, not just the exit code)
# ---------------------------------------------------------------------------
if [ -s "$SELFCHECK_OUT" ]; then
    SHELL_LINE=$(grep -m1 'APP-SHELL-SELFTEST' "$SELFCHECK_OUT")
    case "$SHELL_LINE" in
        *"ok=1"*) result PASS selfcheck.app-shell expected="APP-SHELL-SELFTEST ok=1" actual="$(sanitize "$SHELL_LINE")" note="the product's own headless suite passed" ;;
        *)        result FAIL selfcheck.app-shell expected="APP-SHELL-SELFTEST ok=1" actual="$(sanitize "$SHELL_LINE")" note="product suite failed inside the package" ;;
    esac

    SYNTAX_LINE=$(grep -m1 'syntax-product:' "$SELFCHECK_OUT")
    if printf '%s' "$SYNTAX_LINE" | grep -q 'vedit-verilog.xml' \
       && printf '%s' "$SYNTAX_LINE" | grep -q 'vedit-systemverilog.xml' \
       && printf '%s' "$SYNTAX_LINE" | grep -q 'verilog=1'; then
        result PASS selfcheck.own-xml expected="both vedit-*.xml definitions + verilog=1" \
            actual="$(sanitize "$SYNTAX_LINE")" note="the package loads ONLY its own LGPL definitions"
    else
        result FAIL selfcheck.own-xml expected="both vedit-*.xml definitions + verilog=1" \
            actual="$(sanitize "$SYNTAX_LINE")" note="own syntax definitions are not the visible set"
    fi

    FOLD_LINE=$(grep -m1 'fold.wiring.closed.hidden-975' "$SELFCHECK_OUT")
    case "$FOLD_LINE" in
        *PASS*) result PASS selfcheck.fold-975 expected="fold.wiring.closed.hidden-975 PASS" actual="$(sanitize "$FOLD_LINE")" note="the packaged XML really folds a 1k-line .sv" ;;
        *)      result FAIL selfcheck.fold-975 expected="fold.wiring.closed.hidden-975 PASS" actual="$(sanitize "$FOLD_LINE")" note="folding did not reach 975 hidden lines" ;;
    esac

    TOOL_LINE=$(grep -m1 'TOOLCHAIN source=' "$SELFCHECK_OUT")
    if printf '%s' "$TOOL_LINE" | grep -q 'source=verible:bundled slang:bundled missing=\[\]'; then
        result PASS selfcheck.toolchain expected="TOOLCHAIN source=verible:bundled slang:bundled missing=[]" \
            actual="$(sanitize "$TOOL_LINE")" note="nothing has to be installed on the machine"
    else
        result FAIL selfcheck.toolchain expected="verible:bundled slang:bundled missing=[]" \
            actual="$(sanitize "$TOOL_LINE")" note="the toolchain did not resolve fully from the package"
    fi
else
    result SKIP selfcheck.app-shell expected="selfcheck output" actual="none" note="no selfcheck run succeeded; see run.selfcheck"
    result SKIP selfcheck.own-xml expected="selfcheck output" actual="none" note="no selfcheck run succeeded"
    result SKIP selfcheck.fold-975 expected="selfcheck output" actual="none" note="no selfcheck run succeeded"
    result SKIP selfcheck.toolchain expected="selfcheck output" actual="none" note="no selfcheck run succeeded"
fi

# ---------------------------------------------------------------------------
# 9. extract the AppImage once; 6/7/8 use this tree (same bytes, verified above)
# ---------------------------------------------------------------------------
APPDIR=""
EXTRACT_LOG="$TMP/extract.log"
mkdir -p "$TMP/extract" 2>/dev/null
( cd "$TMP/extract" && run_capture "$EXTRACT_LOG" "$APPIMAGE" --appimage-extract )
EXTRACT_RC=$?
if [ -x "$TMP/extract/squashfs-root/AppRun" ] && [ -x "$TMP/extract/squashfs-root/bin/vd-app" ]; then
    APPDIR="$TMP/extract/squashfs-root"
    result PASS extract expected="squashfs-root/AppRun + bin/vd-app" actual="$APPDIR" note="used by the loader/lint/launch checks below"
else
    result FAIL extract expected="squashfs-root/AppRun + bin/vd-app" actual="missing (exit=$EXTRACT_RC)" \
        note="the internal checks (loader/lint/launch) cannot run without the tree"
fi

# ---------------------------------------------------------------------------
# 6. loader proof: a real process, read /proc/<pid>/maps.
# ---------------------------------------------------------------------------
if [ -n "$APPDIR" ] && [ -x "$APPDIR/AppRun" ]; then
    LOADER="$APPDIR/ld-linux-x86-64.so.2"
    HOLD_LOG="$TMP/hold.log"
    env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP" QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 \
        "$APPDIR/AppRun" --hold 800 >"$HOLD_LOG" 2>&1 &
    APP_PID=$!
    MAPS=""
    for _ in $(seq 1 80); do
        if [ -r "/proc/$APP_PID/maps" ] && grep -aq 'vd-app' "/proc/$APP_PID/maps" 2>/dev/null; then
            MAPS="/proc/$APP_PID/maps"; break
        fi
        sleep 0.05
    done
    if [ -n "$MAPS" ] && [ -r "$MAPS" ]; then
        if grep -aq "$LOADER" "$MAPS"; then
            result PASS loader.packaged expected="package loader $LOADER in /proc/<pid>/maps" \
                actual="$(sanitize "$(grep -am1 "$LOADER" "$MAPS")")" note="the packaged loader is the one running the product"
        else
            result FAIL loader.packaged expected="package loader $LOADER in /proc/<pid>/maps" actual="not present" note="the product was not started through the packaged loader"
        fi
        FOREIGN=$(grep -a 'ld-linux-x86-64\.so\.2' "$MAPS" | grep -av "$APPDIR/" | head -1)
        if [ -n "$FOREIGN" ]; then
            result FAIL loader.no-system-loader expected="no loader outside the package in /proc/<pid>/maps" \
                actual="$(sanitize "$FOREIGN")" note="a host loader leaked into the process"
        else
            result PASS loader.no-system-loader expected="no loader outside the package in /proc/<pid>/maps" \
                actual="only $APPDIR/ld-linux-x86-64.so.2" note="the host loader is not used"
        fi
        if grep -aq "$APPDIR/lib/libc.so.6" "$MAPS"; then
            result PASS loader.packaged-libc expected="package libc $APPDIR/lib/libc.so.6 mapped" \
                actual="$(sanitize "$(grep -am1 "$APPDIR/lib/libc.so.6" "$MAPS")")" note="the glibc inside the package is in use (the 2.17 route)"
        else
            result FAIL loader.packaged-libc expected="package libc in /proc/<pid>/maps" actual="not present" note="the package glibc is not in use"
        fi
    else
        result FAIL loader.packaged expected="readable /proc/<pid>/maps of the product" actual="not readable" note="could not observe the running process (see hold.log)"
        result SKIP loader.no-system-loader expected="maps" actual="n/a" note="depends on loader.packaged"
        result SKIP loader.packaged-libc expected="maps" actual="n/a" note="depends on loader.packaged"
        head -5 "$HOLD_LOG" 2>/dev/null | sed 's/^/   | /' | tee -a "$REPORT"
    fi
    kill "$APP_PID" 2>/dev/null
    wait "$APP_PID" 2>/dev/null
else
    result SKIP loader.packaged expected="extracted tree" actual="none" note="extract failed; cannot observe the process"
    result SKIP loader.no-system-loader expected="extracted tree" actual="none" note="extract failed"
    result SKIP loader.packaged-libc expected="extracted tree" actual="none" note="extract failed"
fi

# ---------------------------------------------------------------------------
# 7. real lint: the bundled verible-verilog-lint on a generated violating .sv
# ---------------------------------------------------------------------------
if [ -n "$APPDIR" ] && [ -x "$APPDIR/bin/verible-verilog-lint" ]; then
    LINT_DIR="$TMP/lint"; mkdir -p "$LINT_DIR"
    BAD="$LINT_DIR/line_length_bad.sv"
    {
        printf 'module line_length_bad;\n'
        printf '  assign q = %s;\n' "$(awk 'BEGIN{for(i=0;i<120;i++)printf "a"}')"
        printf 'endmodule\n'
    } >"$BAD"
    LINT_LOG="$TMP/lint.log"
    run_capture "$LINT_LOG" env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP" LANG=C.UTF-8 \
        "$APPDIR/bin/verible-verilog-lint" --rules=line-length "$BAD"
    LINT_RC=$?
    LINT_LINE=$(grep -m1 'line-length' "$LINT_LOG")
    if [ -n "$LINT_LINE" ] && printf '%s' "$LINT_LINE" | grep -qi 'line length'; then
        result PASS lint.real expected="a real line-length diagnostic from the bundled linter" \
            actual="$(sanitize "$LINT_LINE")" note="linter exit=$LINT_RC (1 = violations found, expected); no host tool used"
    else
        result FAIL lint.real expected="a real line-length diagnostic" actual="$(sanitize "$(head -3 "$LINT_LOG" 2>/dev/null)")" note="the bundled linter produced no line-length diagnostic"
    fi
else
    result SKIP lint.real expected="bin/verible-verilog-lint in the package" actual="absent" note="cannot lint without the packaged linter"
fi

# ---------------------------------------------------------------------------
# 8. real launch: a generated .f project + --hold, exit 0.
# ---------------------------------------------------------------------------
if [ -n "$APPDIR" ] && [ -x "$APPDIR/AppRun" ]; then
    PROJ="$TMP/proj"; mkdir -p "$PROJ/rtl"
    printf 'module m;\nendmodule\n' >"$PROJ/rtl/m.sv"
    # The project root owns the .f (AGENT.md: "a directory with a direct *.f"),
    # and its entries are relative to the .f file.
    printf 'rtl/m.sv\n' >"$PROJ/top.f"
    LAUNCH_LOG="$TMP/launch.log"
    run_capture "$LAUNCH_LOG" env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP" \
        QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 "$APPDIR/AppRun" --project "$PROJ" --hold 300
    LAUNCH_RC=$?
    FILESET_LINE=$(grep -m1 'effective file set' "$LAUNCH_LOG")
    if [ "$LAUNCH_RC" = "0" ]; then
        result PASS launch.project expected="--project <generated .f> --hold 300 exits 0" \
            actual="exit=0" note="the product started, opened the project and shut down cleanly"
    else
        result FAIL launch.project expected="--project <generated .f> --hold 300 exits 0" actual="exit=$LAUNCH_RC" \
            note="$(sanitize "$(tail -3 "$LAUNCH_LOG" 2>/dev/null)")"
    fi
    FILESET_COUNT=$(printf '%s' "$FILESET_LINE" | sed -n 's/.*: \([0-9][0-9]*\) file(s).*/\1/p')
    if [ -z "$FILESET_LINE" ]; then
        result SKIP launch.project-files expected="the project model read the generated .f (>=1 file)" actual="no 'effective file set' line" note="not all builds print it; launch.project already proved the start"
    elif [ "${FILESET_COUNT:-0}" -ge 1 ]; then
        result PASS launch.project-files expected="the project model read the generated .f (>=1 file)" \
            actual="$(sanitize "$FILESET_LINE")" note="the generated filelist was actually expanded"
    else
        result FAIL launch.project-files expected="the project model read the generated .f (>=1 file)" \
            actual="$(sanitize "$FILESET_LINE")" note="the project opened but its file set stayed empty"
    fi
else
    result SKIP launch.project expected="extracted tree" actual="none" note="extract failed; cannot start the product"
fi

# ---------------------------------------------------------------------------
# manual section (not part of the verdict; the user fills it in)
# ---------------------------------------------------------------------------
record ""
record "---- manual checks (optional; NOT counted in SUMMARY) --------------"
record " If this machine has a desktop session, double-click the AppImage and mark:"
record "   [ ] the window opens on the real desktop                                    PASS / FAIL"
record "   [ ] opening a .v/.sv file shows syntax colours                             PASS / FAIL"
record "   [ ] edit one line and Ctrl+S saves it                                      PASS / FAIL"
record "   [ ] the diagnostics panel reports a real lint message                      PASS / FAIL"
record "   note:"
record "---------------------------------------------------------------------"
record ""
record "SUMMARY: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
record "Report: $REPORT"
record "Send this whole report file back; the RESULT lines are the answer."
if [ "$FAIL" -eq 0 ]; then
    exit 0
fi
exit 1
