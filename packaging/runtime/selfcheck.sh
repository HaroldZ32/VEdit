#!/usr/bin/env bash
# Runtime self-check for the VEdit private runtime — DELIVERY form (T31).
#
# Starts the product binary (bin/vd-app) through the PACKAGED loader with a
# clean environment and asserts:
#   * APP-SHELL-SELFTEST ok=1        — the product's own headless suite passed,
#   * TOOLCHAIN source=…:bundled …   — verible + slang came from the package,
#   * syntax-product … own-xml=[…]   — OUR definitions loaded from the data root,
#   * EDIT-CORE-SELFTEST ok=1        — KTextEditor kernel initialised,
#   * fold.wiring.closed.hidden-975  — the packaged XML actually folds a 1k .sv,
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
      QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 \
      "$PREFIX/AppRun" --selfcheck 2>&1
)
RC=$?
printf '%s\n' "$OUT"
echo "app_exit=$RC"

fail=0
printf '%s' "$OUT" | grep -q 'APP-SHELL-SELFTEST ok=1' && echo "CHECK PASS  APP-SHELL-SELFTEST ok=1" || { echo "CHECK FAIL  APP-SHELL-SELFTEST"; fail=1; }
printf '%s' "$OUT" | grep -q 'TOOLCHAIN source=verible:bundled slang:bundled missing=\[\]' \
  && echo "CHECK PASS  toolchain source=bundled, missing=[]" \
  || { echo "CHECK FAIL  toolchain is not fully bundled"; fail=1; }
SYNTAX_LINE=$(printf '%s\n' "$OUT" | grep -m1 'syntax-product:')
if printf '%s' "$SYNTAX_LINE" | grep -q 'own-xml=.*vedit-verilog.xml' \
   && printf '%s' "$SYNTAX_LINE" | grep -q 'own-xml=.*vedit-systemverilog.xml' \
   && printf '%s' "$SYNTAX_LINE" | grep -q 'verilog=1'; then
  echo "CHECK PASS  our syntax definitions loaded (both XMLs, order-insensitive)"
else
  echo "CHECK FAIL  own XML definitions"; fail=1
fi
printf '%s' "$OUT" | grep -q 'EDIT-CORE-SELFTEST ok=1' && echo "CHECK PASS  KTextEditor kernel" || { echo "CHECK FAIL  edit-core"; fail=1; }
printf '%s' "$OUT" | grep -q 'CHECK fold.wiring.closed.hidden-975 PASS' \
  && echo "CHECK PASS  folded 1k .sv with packaged XML" \
  || { echo "CHECK FAIL  folding in the package"; fail=1; }

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
