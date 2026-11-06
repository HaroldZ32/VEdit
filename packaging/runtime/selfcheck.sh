#!/usr/bin/env bash
# Runtime self-check for the VEdit private runtime — DELIVERY form (T31).
#
# Starts the product binary (bin/vd-app) through the PACKAGED loader with a
# clean environment and asserts:
#   * APP-SHELL-SELFTEST ok=1        — the product's own headless suite passed,
#   * TOOLCHAIN source=…:bundled …   — verible + slang came from the package,
#   * syntax-product … own-xml=[…]   — ALL FIVE definitions loaded from the data root,
#   * EDIT-CORE-SELFTEST ok=1        — KTextEditor kernel initialised,
#   * fold.native.view / no-auto-folds — KDE-native folding (T128); the injected
#   * /proc/<pid>/maps shows the packaged loader + libc and no system loader.
#
# Usage: selfcheck.sh --appdir <dir>   |   --appimage <file>
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MODE=""; TARGET=""
while [ $# -gt 0 ]; do
  case "$1" in
    --appdir) MODE=appdir; TARGET=${2:?}; shift 2 ;;
    --appimage) MODE=appimage; TARGET=${2:?}; shift 2 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/home" "$TMP/tmp"

PREFIX="$TARGET"
if [ "$MODE" = appimage ]; then
  [ -f "$TARGET" ] || { echo "ERROR: no AppImage: $TARGET" >&2; exit 1; }
  echo "-- extracting $TARGET (FUSE not required) --"
  ( cd "$TMP" && APPIMAGE_EXTRACT_AND_RUN=1 "$TARGET" --appimage-extract >/dev/null 2>&1 ) \
    || { echo "FAIL: --appimage-extract failed" >&2; exit 1; }
  PREFIX="$TMP/squashfs-root"
elif [ "$MODE" != appdir ]; then
  echo "usage: selfcheck.sh --appdir <dir> | --appimage <file>" >&2; exit 2
fi
[ -x "$PREFIX/AppRun" ] || { echo "FAIL: no AppRun in $PREFIX" >&2; exit 1; }
PREFIX=$(cd "$PREFIX" && pwd -P)
[ -x "$PREFIX/bin/vd-app" ] || { echo "FAIL: no bin/vd-app in $PREFIX" >&2; exit 1; }

echo "== self-check =="
echo "prefix: $PREFIX"
echo "\$ env -i PATH=... HOME=$TMP/home QT_QPA_PLATFORM=offscreen $PREFIX/AppRun --selfcheck"
OUT=$(
  env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP/tmp" \
      QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 VEDIT_DEBUG=1 \
      "$PREFIX/AppRun" --selfcheck 2>&1
)
RC=$?
printf '%s\n' "$OUT"
echo "app_exit=$RC"

fail=0
# --- AppRun caller-environment contract (F1) --------------------------------
# The packaged launch may not replace a caller's VEDIT_SLANG / VEDIT_VERIBLE_LS,
# and may only name a bundled tool that exists and is executable. The probe
# builds its own fake AppDir, so it does not depend on the prefix under test.
if bash "$HERE/probe-apprun-env.sh"; then
  echo "CHECK PASS  AppRun preserves caller tool overrides"
else
  echo "CHECK FAIL  AppRun caller-environment contract"; fail=1
fi

# --- C7 ksh_guard is locale-independent (A3 blocker 1) ----------------------
# The whitelist is produced by assemble-appdir.sh with Python's sorted()
# (code-point order) and the guard used the caller's `sort`. Under zh_CN.UTF-8 /
# en_US.UTF-8 that orders `Verilog` before `VHDL`, so a CORRECT package exited 2
# before the product started. The main run above pins LANG=C.UTF-8, which is
# exactly what masked it; the guard path is therefore exercised here under BOTH
# a C locale and a non-C one. `--hold 10` starts the product for 10 ms; the
# guard runs before exec and prints its verdict only with VEDIT_DEBUG=1.
non_c_locale=""
for candidate in zh_CN.UTF-8 en_US.UTF-8; do
  key=$(printf '%s' "$candidate" | tr -d '.-' | tr 'A-Z' 'a-z')
  if locale -a 2>/dev/null | tr -d '.-' | tr 'A-Z' 'a-z' | grep -qx "$key"; then
    non_c_locale="$candidate"
    break
  fi
done
ksh_guard_case() { # <locale> -> 0 when the guard ran and passed
  local loc=$1 guard_out guard_rc guard_file
  guard_file="$TMP/ksh-guard-$(printf '%s' "$loc" | tr -c 'A-Za-z0-9' '_').out"
  guard_out=$(env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP/tmp" \
      QT_QPA_PLATFORM=offscreen LC_ALL="$loc" LANG="$loc" VEDIT_DEBUG=1 \
      timeout 60 "$PREFIX/AppRun" --hold 10 2>&1)
  guard_rc=$?
  printf '%s\n' "$guard_out" > "$guard_file"
  # grep from the file, not a pipe: `pipefail` turns the EPIPE of a `grep -q`
  # that exits at the first match into a false failure (see the note below).
  if grep -q 'KSH-GUARD PASS' "$guard_file"; then
    return 0
  fi
  echo "   guard output under LC_ALL=$loc (AppRun exit=$guard_rc):"
  grep -E 'KSH-GUARD|KSH-ENV defs=' "$guard_file" | sed 's/^/     /'
  return 1
}
if ksh_guard_case C; then
  echo "CHECK PASS  ksh_guard under LC_ALL=C (C locale)"
else
  echo "CHECK FAIL  ksh_guard under LC_ALL=C"; fail=1
fi
if [ -z "$non_c_locale" ]; then
  echo "CHECK SKIP  ksh_guard under a non-C locale (neither zh_CN.UTF-8 nor en_US.UTF-8 installed)"
elif ksh_guard_case "$non_c_locale"; then
  echo "CHECK PASS  ksh_guard under LC_ALL=$non_c_locale (non-C locale; A3 blocker 1)"
else
  echo "CHECK FAIL  ksh_guard under LC_ALL=$non_c_locale (collation-dependent guard)"; fail=1
fi

# The product output is large and this script sets `pipefail`: a
# `printf ... | grep -q` pipeline is a race (grep exits at the first match, the
# write gets EPIPE and the check false-fails). Read it from a file instead.
OUT_FILE="$TMP/selfcheck.out"
printf '%s\n' "$OUT" > "$OUT_FILE"
grep -q 'APP-SHELL-SELFTEST ok=1' "$OUT_FILE" && echo "CHECK PASS  APP-SHELL-SELFTEST ok=1" || { echo "CHECK FAIL  APP-SHELL-SELFTEST"; fail=1; }
grep -q 'TOOLCHAIN source=verible:bundled slang:bundled missing=\[\]' "$OUT_FILE" \
  && echo "CHECK PASS  toolchain source=bundled, missing=[]" \
  || { echo "CHECK FAIL  toolchain is not fully bundled"; fail=1; }
SYNTAX_LINE=$(printf '%s\n' "$OUT" | grep -m1 'syntax-product:')
# C5/A3: the package ships FIVE definitions — Verilog, SystemVerilog, VHDL,
# Markdown, Tcl (usr/share/vedit/ksh-whitelist.txt). This check used to grep the
# two Verilog XMLs only, so a package that had lost the other three STILL passed
# the self-check. Every definition is required now and the check stays
# fail-closed: anything missing from own-xml= fails the run.
SYNTAX_DEFINITIONS=(vedit-verilog.xml vedit-systemverilog.xml vedit-vhdl.xml
                    vedit-markdown.xml vedit-tcl.xml)
# own-xml= lists what the loader really loaded, in QDirIterator order (not
# sorted), so membership is tested per entry instead of by surrounding text.
OWN_XML=$(printf '%s\n' "$SYNTAX_LINE" | sed -n 's/.*own-xml=\(\[[^]]*\]\).*/\1/p')
missing_syntax=""
for definition in "${SYNTAX_DEFINITIONS[@]}"; do
  printf '%s' "$OWN_XML" | grep -qE "(^|\[|,)${definition}(,|\])" \
    || missing_syntax="$missing_syntax $definition"
done
if [ -n "$missing_syntax" ]; then
  echo "CHECK FAIL  own XML definitions missing:${missing_syntax} (own-xml=${OWN_XML:-<none>})"
  fail=1
elif printf '%s' "$SYNTAX_LINE" | grep -q 'verilog=1' \
  && printf '%s' "$SYNTAX_LINE" | grep -q 'systemverilog=1'; then
  echo "CHECK PASS  our syntax definitions loaded (all five XMLs, order-insensitive)"
else
  echo "CHECK FAIL  all five XMLs loaded but the Verilog/SystemVerilog name lookup failed"
  fail=1
fi
grep -q 'EDIT-CORE-SELFTEST ok=1' "$OUT_FILE" && echo "CHECK PASS  KTextEditor kernel" || { echo "CHECK FAIL  edit-core"; fail=1; }
# T128: folding is KDE-native (the FoldInjector layer was deleted), so the old
# `fold.wiring.closed.hidden-975` criterion is retired (AGENT.md §10). Assert the
# native facts instead: the folding view exists AND no folds are injected on open.
if grep -q 'CHECK fold.native.view PASS' "$OUT_FILE" \
   && grep -q 'CHECK fold.native.no-auto-folds PASS hidden=0' "$OUT_FILE"; then
  echo "CHECK PASS  native folding available and no auto-folds"
else
  echo "CHECK FAIL  folding in the package"; fail=1
fi

# --- packaged CJK font + its OFL text (T119) --------------------------------
FONT="$PREFIX/share/fonts/NotoSansCJK-Regular.ttc"
FONT_LICENSE="$PREFIX/usr/share/doc/vedit/licenses/Noto-CJK-OFL-1.1.txt"
if [ -f "$FONT" ]; then
    echo "CHECK PASS  packaged CJK font present ($(stat -c%s "$FONT") bytes)"
else
    echo "CHECK FAIL  packaged CJK font missing: $FONT"; fail=1
fi
if [ -f "$FONT_LICENSE" ] && grep -q 'SIL OPEN FONT LICENSE' "$FONT_LICENSE" \
   && grep -q 'Google Corporation' "$FONT_LICENSE"; then
    echo "CHECK PASS  packaged OFL text present and carries the copyright"
else
    echo "CHECK FAIL  packaged OFL text missing/incomplete: $FONT_LICENSE"; fail=1
fi

# --- loader / glibc link fact (build a real process, read /proc/<pid>/maps) ---
echo "-- loader proof: run the product through AppRun and read /proc/<pid>/maps --"
LOADER="$PREFIX/ld-linux-x86-64.so.2"
APP_PID=""
if [ -x "$LOADER" ]; then
  env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP/tmp" \
      QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 \
      "$PREFIX/AppRun" --hold 800 >"$TMP/hold.log" 2>&1 &
  APP_PID=$!
  MAPS=""
  for _ in $(seq 1 40); do
    [ -r "/proc/$APP_PID/maps" ] && MAPS="/proc/$APP_PID/maps" && grep -aq 'vd-app' "$MAPS" && break
    sleep 0.05
  done
  if [ -n "$MAPS" ] && [ -r "$MAPS" ]; then
    if grep -aq "$LOADER" "$MAPS"; then echo "CHECK PASS  packaged loader in process maps"; else echo "CHECK FAIL  packaged loader not in maps"; fail=1; fi
    if grep -aq 'ld-linux-x86-64.so.2' "$MAPS" && grep -aq "/lib64/ld-linux-x86-64.so.2" "$MAPS"; then
      echo "CHECK FAIL  system loader present in process maps"; fail=1
    else
      echo "CHECK PASS  no system loader in process maps"
    fi
    grep -aq "$PREFIX/lib/libc.so.6" "$MAPS" && echo "CHECK PASS  packaged libc in process maps" || { echo "CHECK FAIL  packaged libc not in maps"; fail=1; }
  else
    echo "CHECK FAIL  could not read /proc/<pid>/maps of the product"; fail=1
  fi
  kill "$APP_PID" 2>/dev/null
  wait "$APP_PID" 2>/dev/null
else
  echo "CHECK FAIL  no packaged loader ($LOADER)"; fail=1
fi

# --- definitions shipped (list-level evidence) ------------------------------
SYNTAX="$PREFIX/usr/share/org.kde.syntax-highlighting/syntax"
echo "-- shipped syntax definitions in $SYNTAX --"
ls -1 "$SYNTAX" 2>/dev/null | sed 's/^/   /'
LIB=$(ls -1 "$PREFIX"/lib/libKF6SyntaxHighlighting.so.6* 2>/dev/null | head -1)
if [ -n "$LIB" ] && [ -f "$HERE/../../evidence/r2-ksh-embedded/scan-embedded-defs.py" ]; then
  echo "-- embedded in the library (names/licenses) --"
  python3 "$HERE/../../evidence/r2-ksh-embedded/scan-embedded-defs.py" "$LIB" 2>/dev/null \
    | grep -E 'resource names|zstd frames|license classes|strict-GPL' | sed 's/^/   /' || true
fi

echo
if [ "$fail" -eq 0 ] && [ "$RC" -eq 0 ]; then
  echo "SELFCHECK PASS"; exit 0
fi
echo "SELFCHECK FAIL"; exit 1
