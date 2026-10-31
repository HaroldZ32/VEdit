#!/usr/bin/env bash
# Re-runnable before/after evidence for board task T47 (fix-shell-terminal).
#
#   bash evidence/fix-shell-terminal/repro/run-before-after.sh all       # after + defect 2/3 before/after
#   bash evidence/fix-shell-terminal/repro/run-before-after.sh defect1   # defect 1: pre-fix restore order vs deferred
#   bash evidence/fix-shell-terminal/repro/run-before-after.sh defect4   # defect 4: VEDIT_SYNTAX_GRAMMAR=none|verilog
#
# Requires: docker + the pinned image (vedit-qt6-build:local). The "before" runs
# temporarily swap ONE source file for its pre-fix variant and restore it on exit
# (trap), so a run never leaves the tree in the pre-fix state:
#   * defect 2/3: src/terminal/pty_session.cpp  <- repro/pty_session.prefix.cpp
#                 (git HEAD's pty_session.cpp plus the byte counters; no cap, no
#                  child close(master), no O_CLOEXEC, no finalize drain)
#   * defect 1:   src/app/main.cpp panel-seam probe with deferSessionRestore=false
#                 (== the pre-fix order: the session is restored inside the
#                  constructor, before main() registers any panel factory)
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"
EVIDENCE="$ROOT/evidence/fix-shell-terminal"
IMAGE="${VEDIT_IMAGE:-vedit-qt6-build:local}"
BUILD="${VEDIT_BUILD_DIR:-target/qt6-build}"
SCRATCH="$ROOT/target/t47-repro"
mkdir -p "$SCRATCH"

PTY_FIXED="$ROOT/src/terminal/pty_session.cpp"
PTY_PREFIX="$EVIDENCE/repro/pty_session.prefix.cpp"
MAIN_CPP="$ROOT/src/app/main.cpp"
BACKUPS=()

cleanup() {
    local entry
    for entry in "${BACKUPS[@]:-}"; do
        [ -n "$entry" ] || continue
        cp "${entry#*:}" "${entry%%:*}"
    done
    BACKUPS=()
}
trap cleanup EXIT

backup_and_replace() { # <target> <replacement>
    local target="$1" replacement="$2" saved
    saved="$SCRATCH/$(basename "$target").$$.bak"
    cp "$target" "$saved"
    BACKUPS+=("$target:$saved")
    cp "$replacement" "$target"
}

in_image() { docker run --rm -v "$ROOT":/vedit-build -w /vedit-build "$IMAGE" bash -lc "$1"; }

build() { # <target...>
    local targets="$*" log="$SCRATCH/build-$$.log"
    in_image "cmake --build $BUILD --target $targets --parallel > /tmp/build.log 2>&1; \
              rc=\$?; grep -E 'error:' /tmp/build.log | head -5; echo BUILD_EXIT=\$rc"
}

terminal_selftest() {
    in_image "QT_QPA_PLATFORM=offscreen $BUILD/src/terminal/vedit-terminal-selftest --selfcheck \
              2>&1 | grep -a -E 'master-fd|read-limit|SELFTEST|FAIL'"
}

app_selftest() {
    in_image "QT_QPA_PLATFORM=offscreen $BUILD/src/app/vedit-app --selfcheck \
              2>&1 | grep -a -E 'panel-seam|SELFTEST|FAIL'"
}

after_terminal() {
    echo "=== AFTER (defect 2 + 3): current src/terminal"
    build vedit-terminal-selftest
    terminal_selftest
}

before_terminal() {
    echo "=== BEFORE (defect 2 + 3): pre-fix pty_session.cpp (auto-restored afterwards)"
    backup_and_replace "$PTY_FIXED" "$PTY_PREFIX"
    build vedit-terminal-selftest
    terminal_selftest
    cleanup
}

after_app() {
    echo "=== AFTER (defect 1): deferred session restore (product order)"
    build vedit-app
    app_selftest
}

before_app() {
    echo "=== BEFORE (defect 1): restore inside the constructor, factories registered after"
    local flipped="$SCRATCH/main.prefix-order.cpp"
    sed 's/seamRestored\.deferSessionRestore = true;/seamRestored.deferSessionRestore = false;/' \
        "$MAIN_CPP" > "$flipped"
    if cmp -s "$MAIN_CPP" "$flipped"; then
        echo "BEFORE (defect 1): the probe line was not found; cannot reproduce" >&2
        return 1
    fi
    backup_and_replace "$MAIN_CPP" "$flipped"
    build vedit-app
    app_selftest
    cleanup
}

defect4() {
    echo "=== defect 4: VEDIT_SYNTAX_GRAMMAR provider->cache priority (configure only)"
    local variant extra dir
    for variant in none default verilog; do
        case "$variant" in
            none) extra="-DVEDIT_SYNTAX_GRAMMAR=none" ;;
            default) extra="" ;;
            verilog) extra="-DVEDIT_SYNTAX_GRAMMAR=verilog" ;;
        esac
        dir="target/t47-repro/grammar-$variant"
        echo "--- $variant ($extra)"
        in_image "rm -rf /vedit-build/$dir; \
                  cmake -S /vedit-build -B /vedit-build/$dir -G Ninja \
                        -DVEDIT_RUST_FFI_LIB=/vedit-build/target/release/libvd_ffi.a \
                        -DVEDIT_RUST_FFI_INCLUDE=/vedit-build/crates/vd-ffi/include $extra \
                        > /vedit-build/target/t47-repro-configure-$variant.log 2>&1; \
                  echo CONFIGURE_EXIT=\$?; \
                  grep -a -E \"syn-(grammars|provider):\" /vedit-build/target/t47-repro-configure-$variant.log"
        grep -a "^VEDIT_SYNTAX_GRAMMAR:STRING" "$ROOT/$dir/CMakeCache.txt" | sed 's/^/cache: /'
        grep -a "kCompiledGrammar" "$ROOT/$dir/src/syntax/provider/generated/grammar_config_generated.h" \
            | sed 's/^/header: /'
        grep -a "^VEDIT_SYNTAX_GRAMMAR=" "$ROOT/$dir/src/syntax/grammars/grammars_manifest.txt" \
            | sed 's/^/manifest: /'
    done
}

case "${1:-all}" in
    defect1) after_app; before_app ;;
    defect4) defect4 ;;
    after) after_terminal; after_app ;;
    before) before_terminal; before_app ;;
    all) after_terminal; after_app; before_terminal; before_app ;;
    *) echo "usage: $0 {all|after|before|defect1|defect4}" >&2; exit 2 ;;
esac
