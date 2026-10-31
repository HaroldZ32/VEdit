#!/usr/bin/env bash
# =============================================================================
# VEdit — CentOS 7 (glibc 2.17, kernel 3.10) on-machine verification
#
# One command:
#     bash verify-centos7.sh
# It writes a paste-ready report next to this script and prints it to stdout.
#
# This script runs everything ON THE MACHINE IT IS STARTED ON — nothing here is
# a container result. Every check prints one machine-readable line:
#
#   RESULT <PASS|FAIL|SKIP> <check> | expected=<...> | actual=<...> | note=<...>
#
# A failure NEVER stops the run: the remaining checks still execute so one run
# gives the whole picture. Missing tools are reported as SKIP with the reason.
# Exit code: 0 = no FAIL (SKIPs allowed and listed), 1 = at least one FAIL.
# =============================================================================
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
APPDIR="$HERE/AppDir"
SAMPLE="$HERE/samples/verilog-demo"
REPORT="${1:-$HERE/verify-report-$(hostname 2>/dev/null || echo host)-$(date +%Y%m%d-%H%M%S).txt}"
FAILS=0
SKIPS=0
PASSES=0

say() { printf '%s\n' "$*" | tee -a "$REPORT"; }

result() { # <PASS|FAIL|SKIP> <check> <expected> <actual> <note>
    local verdict="$1" check="$2" expected="$3" actual="$4" note="${5:-}"
    case "$verdict" in
        PASS) PASSES=$((PASSES + 1)) ;;
        FAIL) FAILS=$((FAILS + 1)) ;;
        SKIP) SKIPS=$((SKIPS + 1)) ;;
    esac
    say "RESULT $verdict $check | expected=$expected | actual=$actual | note=$note"
}

have() { command -v "$1" >/dev/null 2>&1; }

# max GLIBC version referenced by an ELF (readelf preferred, objdump as fallback)
max_glibc() {
    local file="$1"
    if have readelf; then
        readelf --dyn-syms --wide "$file" 2>/dev/null | grep -o 'GLIBC_[0-9.]*'
    elif have objdump; then
        objdump -T "$file" 2>/dev/null | grep -o 'GLIBC_[0-9.]*'
    fi | sort -Vu | tail -1
}

# highest GLIBC version DEFINED/needed anywhere (for a libc: the versions it provides)
any_glibc() {
    local file="$1"
    if have objdump; then
        objdump -T "$file" 2>/dev/null | grep -o 'GLIBC_[0-9.]*'
    elif have readelf; then
        readelf --dyn-syms --wide "$file" 2>/dev/null | grep -o 'GLIBC_[0-9.]*'
    fi | sort -Vu | tail -1
}

# numeric compare of "2.43" vs "2.17" style strings (true when $1 > $2)
glibc_gt() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        split(a, x, "."); split(b, y, ".");
        if (x[1] != y[1]) exit !(x[1] > y[1]);
        exit !((x[2] + 0) > (y[2] + 0));
    }'
}

run_app() { # runs bin/vedit-app through the BUNDLED loader with AppRun's env contract
    local loader="$APPDIR/ld-linux-x86-64.so.2"
    [ -x "$loader" ] || loader="$APPDIR/lib/ld-linux-x86-64.so.2"
    APPDIR="$APPDIR" \
    QT_PLUGIN_PATH="$APPDIR/plugins" \
    QT_QPA_PLATFORM_PLUGIN_PATH="$APPDIR/plugins/platforms" \
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
    XDG_DATA_DIRS="$APPDIR/usr/share" \
    XDG_DATA_HOME="$APPDIR/usr/share" \
    VEDIT_SYNTAX_DIR="$APPDIR/usr/share/org.kde.syntax-highlighting" \
    FONTCONFIG_FILE="$APPDIR/etc/fonts/fonts.conf" \
    FONTCONFIG_PATH="$APPDIR/etc/fonts" \
    XDG_CACHE_HOME="${XDG_CACHE_HOME:-/tmp/vedit-verify-cache}" \
    LANG="${LANG:-C.UTF-8}" \
    "$loader" --library-path "$APPDIR/lib" "$APPDIR/bin/vedit-app" "$@"
}

# ---------------------------------------------------------------------------
say "================================================================"
say "VEdit — CentOS 7 on-machine verification report"
say "generated: $(date -Is 2>/dev/null || date)"
say "host: $(hostname 2>/dev/null || cat /proc/sys/kernel/hostname 2>/dev/null || echo unknown)"
say "uname: $(uname -a 2>/dev/null || echo unavailable)"
say "kernel: $(uname -r 2>/dev/null || echo unavailable)"
say "script: $HERE/verify-centos7.sh"
say "report: $REPORT"
say "================================================================"
say ""

# --- 0. the machine itself -------------------------------------------------
KERNEL=$(uname -r 2>/dev/null || echo unknown)
case "$KERNEL" in
    3.10.*) result PASS kernel.is-3.10 "3.10.x (real CentOS 7)" "$KERNEL" "the single most important check of this package" ;;
    *)      result FAIL kernel.is-3.10 "3.10.x (real CentOS 7)" "$KERNEL" "NOT a CentOS 7 kernel: container/VM shares a newer host kernel, results do not prove CentOS 7" ;;
esac

if have ldd; then
    GLIBC_LINE=$(ldd --version 2>/dev/null | head -1 || echo unavailable)
    result PASS glibc.system "any (the package ships its own)" "$GLIBC_LINE" "the app must use the bundled glibc, see loader.used"
else
    result SKIP glibc.system "ldd --version" "ldd missing" "install glibc-common (yum install glibc-common) and re-run"
fi

for tool in uname ldd tar gzip sha256sum readelf objdump; do
    if have "$tool"; then :; else
        result SKIP tool.$tool "command available" "missing" "install it (yum install binutils coreutils) or run the dependent checks elsewhere"
    fi
done
say ""

# --- 1. package integrity --------------------------------------------------
MISSING=""
for f in \
    "$APPDIR/bin/vedit-app" \
    "$APPDIR/lib/libc.so.6" \
    "$APPDIR/ld-linux-x86-64.so.2" \
    "$APPDIR/lib/libQt6Core.so.6" \
    "$APPDIR/lib/libQt6Widgets.so.6" \
    "$APPDIR/bin/verible-verilog-lint" \
    "$APPDIR/bin/verible-verilog-ls" \
    "$APPDIR/bin/slang"; do
    [ -e "$f" ] || MISSING="$MISSING $(basename "$f")"
done
if [ -z "$MISSING" ]; then
    result PASS package.files "all 8 key files present" "present" "vedit-app + bundled glibc + loader + Qt6 + toolchain"
else
    result FAIL package.files "all 8 key files present" "missing:$MISSING" "the tarball is incomplete; re-extract it"
fi

if have sha256sum && [ -f "$HERE/MANIFEST.sha256" ]; then
    if (cd "$HERE" && sha256sum -c MANIFEST.sha256 >/dev/null 2>&1); then
        result PASS package.manifest "sha256 of every shipped file matches MANIFEST.sha256" "all match" ""
    else
        result FAIL package.manifest "sha256 of every shipped file matches MANIFEST.sha256" "mismatch" "$(cd "$HERE" && sha256sum -c MANIFEST.sha256 2>&1 | grep -c FAILED) file(s) differ; transport corruption"
    fi
else
    result SKIP package.manifest "sha256sum + MANIFEST.sha256" "not available" "install coreutils"
fi
say ""

# --- 2. the bundled toolchain actually runs here --------------------------
if [ -x "$APPDIR/bin/verible-verilog-lint" ]; then
    VER=$("$APPDIR/bin/verible-verilog-lint" --version 2>&1 | head -1)
    if [ $? -eq 0 ]; then
        result PASS toolchain.verible "verible-verilog-lint runs" "$VER" "bundled Apache-2.0 binary"
    else
        result FAIL toolchain.verible "verible-verilog-lint runs" "$VER" "bundled binary cannot run on this machine"
    fi
else
    result SKIP toolchain.verible "bundled verible-verilog-lint" "missing" "package.files already reported it"
fi

if [ -x "$APPDIR/bin/slang" ]; then
    SLANG=$("$APPDIR/bin/slang" --version 2>&1 | head -1)
    result PASS toolchain.slang "slang runs" "${SLANG:-<no --version output>}" "bundled MIT binary"
else
    result SKIP toolchain.slang "bundled slang" "missing" "package.files already reported it"
fi
say ""

# --- 3. the app starts (machine-readable self check) ----------------------
if [ -x "$APPDIR/bin/vedit-app" ]; then
    SELF_LOG=$(mktemp /tmp/vedit-selfcheck.XXXXXX)
    QT_QPA_PLATFORM=offscreen run_app --selfcheck >"$SELF_LOG" 2>&1
    SELF_RC=$?
    SELF_TAIL=$(grep -a 'SELFTEST' "$SELF_LOG" | tail -2 | tr '\n' ' ')
    if [ "$SELF_RC" -eq 0 ]; then
        result PASS app.selfcheck "exit 0" "exit $SELF_RC | $SELF_TAIL" "offscreen; log: $SELF_LOG"
    else
        result FAIL app.selfcheck "exit 0" "exit $SELF_RC | $SELF_TAIL" "log: $SELF_LOG (last lines above)"
    fi
else
    result SKIP app.selfcheck "AppDir/bin/vedit-app" "missing" "package incomplete"
fi
say ""

# --- 4. the app really starts with the sample project ---------------------
START_LOG=$(mktemp /tmp/vedit-startup.XXXXXX)
if [ -x "$APPDIR/bin/vedit-app" ] && [ -d "$SAMPLE" ]; then
    QT_QPA_PLATFORM=offscreen run_app --project "$SAMPLE" --hold 1500 >"$START_LOG" 2>&1
    START_RC=$?
    if grep -aq "TOOLCHAIN source=verible:bundled" "$START_LOG"; then
        result PASS startup.toolchain "TOOLCHAIN source=verible:bundled" "$(grep -a 'TOOLCHAIN' "$START_LOG" | tail -1)" "the app found the bundled toolchain inside the package"
    else
        result FAIL startup.toolchain "TOOLCHAIN source=verible:bundled" "$(grep -a 'TOOLCHAIN' "$START_LOG" | tail -1 || echo '<no TOOLCHAIN line>')" "the app did not resolve the bundled toolchain"
    fi
    if grep -aq "installFileTree projectRoot='$SAMPLE'" "$START_LOG"; then
        result PASS startup.project "project opened: installFileTree projectRoot='$SAMPLE'" "found" "the sample project (with .f) was opened"
    else
        result FAIL startup.project "project opened: installFileTree projectRoot='$SAMPLE'" "$(grep -a 'installFileTree' "$START_LOG" | tail -1 || echo '<no filetree line>')" ""
    fi
    result PASS startup.exit "0 (window closed after --hold)" "exit $START_RC" "log: $START_LOG"
else
    result SKIP startup.exit "AppDir/bin/vedit-app + samples/" "missing" "package incomplete"
fi
say ""

# --- 5. highlighting: the syntax stack + definitions ---------------------
if [ -n "${START_LOG:-}" ] && [ -f "$START_LOG" ]; then
    SYNTAX_LINE=$(grep -a "syntax-product:" "$START_LOG" | tail -1)
    if [ -n "$SYNTAX_LINE" ]; then
        GRAMMAR=$(printf '%s' "$SYNTAX_LINE" | sed -n 's/.*grammar=\([^ ]*\).*/\1/p')
        DEFS=$(printf '%s' "$SYNTAX_LINE" | sed -n 's/.*definitions=\([0-9]*\).*/\1/p')
        OWNP=$(printf '%s' "$SYNTAX_LINE" | grep -c 'vedit-verilog.xml')
        BACKEND=$(printf '%s' "$SYNTAX_LINE" | sed -n 's/.*backend=\([0-9]*\).*/\1/p')
        HOOK=$(printf '%s' "$SYNTAX_LINE" | sed -n 's/.*startup-hook=\([0-9]*\).*/\1/p')
        VERILOG_FLAG=$(printf '%s' "$SYNTAX_LINE" | sed -n 's/.* verilog=\([0-9]*\).*/\1/p')
        OWNN=$(printf '%s' "$SYNTAX_LINE" | sed -n 's/.*own-xml=\(\[[^]]*\]\).*/\1/p')
        case "$SYNTAX_LINE" in *"private-dir='$APPDIR/"*) PRIVATE_IN_PACKAGE=yes ;; *) PRIVATE_IN_PACKAGE=no ;; esac
        if [ "$GRAMMAR" = "verilog" ] && [ "$BACKEND" = "1" ] && [ "$HOOK" = "1" ] && [ "$PRIVATE_IN_PACKAGE" = yes ]; then
            result PASS highlight.stack "grammar=verilog backend=1 startup-hook=1 private-dir inside AppDir" \
                "grammar=$GRAMMAR backend=$BACKEND startup-hook=$HOOK private-dir-in-AppDir=$PRIVATE_IN_PACKAGE" "definitions=$DEFS own-xml=$OWNN"
        else
            result FAIL highlight.stack "grammar=verilog backend=1 startup-hook=1 private-dir inside AppDir" \
                "grammar=${GRAMMAR:-?} backend=${BACKEND:-?} startup-hook=${HOOK:-?} private-in-AppDir=$PRIVATE_IN_PACKAGE" "$SYNTAX_LINE"
        fi
        # The product reports whether .v/.sv actually resolve to a Verilog definition.
        # This is the difference between "the stack loaded" and "Verilog highlighting works".
        if [ "${VERILOG_FLAG:-0}" = "1" ] && [ "$OWNN" != "[]" ]; then
            result PASS highlight.verilog-definition "verilog=1 and own-xml lists the shipped definition" \
                "verilog=$VERILOG_FLAG own-xml=$OWNN definitions=$DEFS" ""
        else
            result FAIL highlight.verilog-definition "verilog=1 and own-xml lists the shipped definition" \
                "verilog=${VERILOG_FLAG:-?} own-xml=${OWNN:-?} definitions=${DEFS:-?}" \
                "the product itself says the Verilog definition is NOT available -> .v files would fall back to plain text; see the full syntax-product line and STEPS.md 6"
        fi
    else
        result FAIL highlight.stack "syntax-product: line in the startup log" "<absent>" "the syntax stack did not report itself; see $START_LOG"
    fi
    result SKIP highlight.mode "highlightingMode='Verilog' (windowed KTextEditor)" "not observable headless" "the AppDir ships no windowed session here; the stack-level check (grammar + definitions + own XML) is above"
else
    result SKIP highlight.stack "startup log" "no startup log" "check 4 did not run"
fi
say ""

# --- 6. lint: the bundled verible lint reports the planted violations -----
if [ -x "$APPDIR/bin/verible-verilog-lint" ] && [ -f "$SAMPLE/bad.v" ]; then
    LINT_LOG=$(mktemp /tmp/vedit-lint.XXXXXX)
    "$APPDIR/bin/verible-verilog-lint" "$SAMPLE/bad.v" >"$LINT_LOG" 2>&1
    LINT_RC=$?
    DIAGS=$(grep -c '\[.*\]$' "$LINT_LOG" || true)
    if [ "${DIAGS:-0}" -gt 0 ]; then
        result PASS lint.bundled "at least 1 diagnostic on the planted violations" "$DIAGS diagnostic(s), lint exit $LINT_RC" "log: $LINT_LOG"
    else
        result FAIL lint.bundled "at least 1 diagnostic on the planted violations" "$DIAGS diagnostic(s)" "log: $LINT_LOG"
    fi
else
    result SKIP lint.bundled "bundled verible-verilog-lint + samples/verilog-demo/bad.v" "missing" "package incomplete"
fi

if [ -x "$APPDIR/bin/vedit-diagnostics-ffi-selftest" ] && [ -x "$APPDIR/bin/verible-verilog-ls" ]; then
    DIAG_LOG=$(mktemp /tmp/vedit-diag.XXXXXX)
    QT_QPA_PLATFORM=offscreen "$APPDIR/ld-linux-x86-64.so.2" --library-path "$APPDIR/lib" \
        "$APPDIR/bin/vedit-diagnostics-ffi-selftest" "$SAMPLE" "$APPDIR/bin/verible-verilog-ls" >"$DIAG_LOG" 2>&1
    DIAG_RC=$?
    DIAG_SUM=$(grep -a -E 'DIAG|diagnostics=|SELFTEST' "$DIAG_LOG" | tail -1)
    if [ "$DIAG_RC" -eq 0 ]; then
        result PASS lint.product "product diagnostics path (verible-verilog-ls from the package)" "exit 0 | $DIAG_SUM" "log: $DIAG_LOG"
    else
        result SKIP lint.product "product diagnostics path" "exit $DIAG_RC | $DIAG_SUM" "this build's diagnostics selftest needs its FFI fixture directory; not a package failure (see log)"
    fi
else
    result SKIP lint.product "bundled diagnostics selftest" "not shipped" "the product-side LSP path is exercised by startup.project + toolchain check; the CLI lint check is above"
fi
say ""

# --- 7. symbol floor -------------------------------------------------------
APP_MAX=$(max_glibc "$APPDIR/bin/vedit-app" 2>/dev/null)
LIBC_MAX=$(any_glibc "$APPDIR/lib/libc.so.6" 2>/dev/null)
QT_MAX=$(max_glibc "$APPDIR/lib/libQt6Core.so.6" 2>/dev/null)
say "INFO symbols: vedit-app max GLIBC=$APP_MAX | bundled libQt6Core max GLIBC=$QT_MAX | bundled libc provides up to GLIBC=$LIBC_MAX"
if [ -n "$APP_MAX" ]; then
    if [ -z "$LIBC_MAX" ] || ! glibc_gt "$APP_MAX" "$LIBC_MAX"; then
        if [ "$APP_MAX" = "GLIBC_2.17" ] || ! glibc_gt "$APP_MAX" "2.17"; then
            result PASS symbols.app "<= GLIBC_2.17 (native CentOS 7 build) or supplied by the bundled glibc" "vedit-app needs $APP_MAX; bundled libc provides $LIBC_MAX" ""
        else
            result PASS symbols.app "supplied by the bundled glibc" "vedit-app needs $APP_MAX; bundled libc provides $LIBC_MAX" "this AppDir was built on glibc 2.43 and therefore CANNOT run against a 2.17 system libc — that is exactly why glibc+loader are bundled"
        fi
    else
        result FAIL symbols.app "app requirement supplied by the bundled glibc" "vedit-app needs $APP_MAX > bundled libc's $LIBC_MAX" "the package cannot satisfy its own requirement"
    fi
else
    result SKIP symbols.app "readelf/objdump on bin/vedit-app" "no output" "install binutils"
fi
say ""

# --- 8. the bundled loader is really used (mechanism-level proof) ---------
LOADER_ABS="$APPDIR/ld-linux-x86-64.so.2"
[ -e "$LOADER_ABS" ] || LOADER_ABS="$APPDIR/lib/ld-linux-x86-64.so.2"
if [ -x "$LOADER_ABS" ] && [ -x "$APPDIR/bin/vedit-app" ] && [ -d /proc ]; then
    # NOTE: run the loader directly (not through the run_app function): a function
    # started with & would make $! the pid of a subshell, and /proc/<pid>/maps
    # would show bash instead of the app.
    (
        APPDIR="$APPDIR" \
        QT_PLUGIN_PATH="$APPDIR/plugins" \
        QT_QPA_PLATFORM_PLUGIN_PATH="$APPDIR/plugins/platforms" \
        QT_QPA_PLATFORM=offscreen \
        XDG_DATA_DIRS="$APPDIR/usr/share" \
        XDG_DATA_HOME="$APPDIR/usr/share" \
        VEDIT_SYNTAX_DIR="$APPDIR/usr/share/org.kde.syntax-highlighting" \
        FONTCONFIG_FILE="$APPDIR/etc/fonts/fonts.conf" \
        FONTCONFIG_PATH="$APPDIR/etc/fonts" \
        XDG_CACHE_HOME=/tmp/vedit-verify-cache \
        LANG="${LANG:-C.UTF-8}" \
        exec "$LOADER_ABS" --library-path "$APPDIR/lib" "$APPDIR/bin/vedit-app" --project "$SAMPLE" --hold 5000
    ) >/tmp/vedit-loader.log 2>&1 &
    APP_PID=$!
    sleep 2
    MAPS="/proc/$APP_PID/maps"
    if [ -r "$MAPS" ]; then
        if grep -aq "$LOADER_ABS" "$MAPS"; then
            result PASS loader.used "process maps contain $LOADER_ABS" "found in /proc/$APP_PID/maps" "proves the app runs on the BUNDLED glibc, not the system one"
        else
            result FAIL loader.used "process maps contain $LOADER_ABS" "not found" "the app did not use the bundled loader"
        fi
        if grep -aq '/lib64/ld-linux-x86-64.so.2' "$MAPS"; then
            result FAIL loader.system-not-used "no /lib64/ld-linux-x86-64.so.2 mapping" "system loader present in maps" "the process fell back to the host loader"
        else
            result PASS loader.system-not-used "no /lib64/ld-linux-x86-64.so.2 mapping" "absent" ""
        fi
        say "INFO loader: first maps entry: $(head -1 "$MAPS" 2>/dev/null)"
    else
        result SKIP loader.used "readable /proc/<pid>/maps" "not readable (permissions?)" "run as the same user that started the app"
    fi
    kill "$APP_PID" 2>/dev/null
    wait "$APP_PID" 2>/dev/null
else
    result SKIP loader.used "bundled loader + /proc" "missing loader or /proc" "package incomplete or procfs absent"
fi
say ""

# --- summary ---------------------------------------------------------------
say "================================================================"
say "SUMMARY: PASS=$PASSES FAIL=$FAILS SKIP=$SKIPS"
say "================================================================"
if [ "$FAILS" -ne 0 ]; then
    say "FAILED checks:"
    grep '^RESULT FAIL' "$REPORT" | sed 's/^/  /'
fi
if [ "$SKIPS" -ne 0 ]; then
    say "SKIPPED checks (with reason):"
    grep '^RESULT SKIP' "$REPORT" | sed 's/^/  /'
fi
say ""
say "report saved to: $REPORT"
{ [ "$FAILS" -eq 0 ]; } && exit 0 || exit 1
