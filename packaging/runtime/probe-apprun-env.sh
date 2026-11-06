#!/bin/sh
# AppRun environment contract probe (no product build required).
#
# F1 (reviewer finding): AppRun must never replace a caller-provided
# VEDIT_SLANG / VEDIT_VERIBLE_LS, and must export the bundled path only when the
# caller left the variable unset AND `$APPDIR/bin/<tool>` exists and is
# executable. This is not cosmetic: the Qt module-tree lane reads VEDIT_SLANG
# first (module_tree_integration.cpp, moduleTreeSlangBinary()), so a stale or
# dead bundled value masks the diagnostics-resolved toolchain and kills the
# documented environment override (docs/USER_GUIDE.md troubleshooting).
#
# The probe builds a fake AppDir whose "loader" only prints the environment the
# product would receive, then runs the REAL AppRun on it. Neither the product
# binary nor the packaged loader/package is needed.
#
# Usage: probe-apprun-env.sh          (exit 0 = contract holds, 1 = broken)
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
APPRUN="$HERE/AppRun"
[ -f "$APPRUN" ] || { echo "FAIL: no AppRun at $APPRUN" >&2; exit 1; }

TMP=$(mktemp -d) || { echo "FAIL: mktemp -d" >&2; exit 1; }
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/home"

checks=0
failures=0
check() {
    checks=$((checks + 1))
    if [ "$2" = 1 ]; then
        echo "CHECK PASS  $1"
    else
        failures=$((failures + 1))
        echo "CHECK FAIL  $1"
    fi
}

# make_appdir <dir> <slang: present|absent|noexec> <verible: present|absent>
make_appdir() {
    dir=$1
    slang=$2
    verible=$3
    mkdir -p "$dir/bin"
    # The real launcher runs FROM the AppDir root (its $0 decides APPDIR).
    cp "$APPRUN" "$dir/AppRun"
    chmod +x "$dir/AppRun"
    # Fake private loader: print the environment of the process AppRun starts.
    cat > "$dir/ld-linux-x86-64.so.2" <<'EOF'
#!/bin/sh
env | LC_ALL=C sort | sed -n 's/^\(VEDIT_[A-Z_]*\)=\(.*\)$/LOADER-SEEN \1=\2/p'
exit 0
EOF
    chmod +x "$dir/ld-linux-x86-64.so.2"
    printf '#!/bin/sh\nexit 0\n' > "$dir/bin/vd-app"
    chmod +x "$dir/bin/vd-app"
    case "$slang" in
        present)
            printf '#!/bin/sh\nexit 0\n' > "$dir/bin/slang"
            chmod +x "$dir/bin/slang"
            ;;
        noexec)
            printf '#!/bin/sh\nexit 0\n' > "$dir/bin/slang"
            chmod -x "$dir/bin/slang"
            ;;
        absent) : ;;
        *) echo "FAIL: bad slang mode $slang" >&2; exit 1 ;;
    esac
    if [ "$verible" = present ]; then
        printf '#!/bin/sh\nexit 0\n' > "$dir/bin/verible-verilog-ls"
        chmod +x "$dir/bin/verible-verilog-ls"
    fi
}

# run_apprun <dir> [VAR=value ...] — a clean caller environment plus the given
# overrides; stderr (locale notes) is dropped, the loader's stdout is returned.
run_apprun() {
    dir=$1
    shift
    env -i PATH=/usr/bin:/bin HOME="$TMP/home" "$@" \
        "$dir/AppRun" 2>/dev/null
}

has_env() { # <output> <exact LOADER-SEEN line>
    printf '%s\n' "$1" | grep -Fqx -- "$2" >/dev/null 2>&1
}

has_var() { # <output> <VEDIT_ name, exact prefix match>
    printf '%s\n' "$1" | grep -Fq -- "LOADER-SEEN $2=" >/dev/null 2>&1
}

# 0. The launch script itself must stay POSIX-shell valid.
if sh -n "$APPRUN" >/dev/null 2>&1; then check "apprun.syntax" 1; else check "apprun.syntax" 0; fi

# 1. Nothing set, both tools present -> AppRun names the bundled paths.
APP_A="$TMP/appdir-a"
make_appdir "$APP_A" present present
OUT_A=$(run_apprun "$APP_A")
if has_env "$OUT_A" "LOADER-SEEN VEDIT_SLANG=$APP_A/bin/slang"; then
    check "default.slang-bundled" 1
else
    check "default.slang-bundled" 0
fi
if has_env "$OUT_A" "LOADER-SEEN VEDIT_VERIBLE_LS=$APP_A/bin/verible-verilog-ls"; then
    check "default.verible-bundled" 1
else
    check "default.verible-bundled" 0
fi

# 2. A caller-provided value always wins (this is the F1 regression case).
OUT_B=$(run_apprun "$APP_A" VEDIT_SLANG=/caller/slang VEDIT_VERIBLE_LS=/caller/verible-ls)
if has_env "$OUT_B" "LOADER-SEEN VEDIT_SLANG=/caller/slang"; then
    check "override.slang-preserved" 1
else
    check "override.slang-preserved" 0
fi
if has_env "$OUT_B" "LOADER-SEEN VEDIT_VERIBLE_LS=/caller/verible-ls"; then
    check "override.verible-preserved" 1
else
    check "override.verible-preserved" 0
fi

# 3. No bundled binary and no caller value -> the variable stays UNSET. Exporting
#    a path that does not exist would make the module tree prefer a dead tool.
APP_C="$TMP/appdir-c"
make_appdir "$APP_C" absent absent
OUT_C=$(run_apprun "$APP_C")
if has_var "$OUT_C" VEDIT_SLANG; then
    check "absent.slang-not-exported" 0
else
    check "absent.slang-not-exported" 1
fi
if has_var "$OUT_C" VEDIT_VERIBLE_LS; then
    check "absent.verible-not-exported" 0
else
    check "absent.verible-not-exported" 1
fi
# 3b. ... and a caller value survives even when the package has no bundled tool
#     at all (the stale/dead-package scenario behind F1).
OUT_C2=$(run_apprun "$APP_C" VEDIT_SLANG=/caller/slang)
if has_env "$OUT_C2" "LOADER-SEEN VEDIT_SLANG=/caller/slang"; then
    check "absent.slang-caller-preserved" 1
else
    check "absent.slang-caller-preserved" 0
fi

# 4. A present but NON-executable file is not usable: same rule as `[ -x ]`.
APP_D="$TMP/appdir-d"
make_appdir "$APP_D" noexec absent
OUT_D=$(run_apprun "$APP_D")
if has_var "$OUT_D" VEDIT_SLANG; then
    check "non-executable.slang-not-exported" 0
else
    check "non-executable.slang-not-exported" 1
fi

echo "APPRUN-ENV-PROBE ok=$([ "$failures" -eq 0 ] && echo 1 || echo 0) checks=$checks failures=$failures"
[ "$failures" -eq 0 ]
