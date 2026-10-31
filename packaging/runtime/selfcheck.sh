#!/usr/bin/env bash
# Runtime self-check for the VEdit private runtime (T11 / A0c hook).
#
# Starts the smoke binary through the PACKAGED loader (AppRun does that) with a
# clean environment, and asserts:
#   * M0-SMOKE ok=1                — window / KTextEditor / KSyntaxHighlighting /
#                                    tree-sitter all came up,
#   * libc=<prefix>/...            — the process resolved libc from the package,
#                                    not from the host (A0c),
#   * the loaded syntax definitions — printed for the evidence file.
#
# Usage: selfcheck.sh --appdir <dir>   |   --appimage <file>
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MODE=""; TARGET=""
while [ $# -gt 0 ]; do
  case "$1" in
    --appdir) MODE=appdir; TARGET=${2:?}; shift 2 ;;
    --appimage) MODE=appimage; TARGET=${2:?}; shift 2 ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
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

echo "== self-check =="
echo "prefix: $PREFIX"
echo "\$ env -i PATH=... HOME=$TMP/home QT_QPA_PLATFORM=offscreen $PREFIX/AppRun --selfcheck --expect-libc-prefix $PREFIX"
OUT=$(
  env -i PATH=/usr/bin:/bin HOME="$TMP/home" TMPDIR="$TMP/tmp" \
      QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 \
      "$PREFIX/AppRun" --selfcheck --expect-libc-prefix "$PREFIX" 2>&1
)
RC=$?
printf '%s\n' "$OUT"
echo "smoke_exit=$RC"

fail=0
printf '%s' "$OUT" | grep -q 'M0-SMOKE ok=1'      && echo "CHECK PASS  M0-SMOKE ok=1"           || { echo "CHECK FAIL  M0-SMOKE ok=1"; fail=1; }
printf '%s' "$OUT" | grep -q "libc=$PREFIX/"       && echo "CHECK PASS  libc resolved under package" || { echo "CHECK FAIL  libc not under $PREFIX"; fail=1; }
printf '%s' "$OUT" | grep -q 'ktexteditor=1'       && echo "CHECK PASS  KTextEditor loaded"       || { echo "CHECK FAIL  KTextEditor"; fail=1; }
printf '%s' "$OUT" | grep -q 'tree_sitter_parser=1' && echo "CHECK PASS  tree-sitter parser"      || { echo "CHECK FAIL  tree-sitter"; fail=1; }

# Which definitions did the runtime load? (list-level evidence)
LIB=$(ls -1 "$PREFIX"/lib/libKF6SyntaxHighlighting.so.6* 2>/dev/null | head -1)
if [ -n "$LIB" ]; then
  echo "-- shipped syntax definitions in $PREFIX/share/ksyntaxhighlighting/syntax --"
  ls -1 "$PREFIX/share/ksyntaxhighlighting/syntax" 2>/dev/null | sed 's/^/   /'
  if python3 "$HERE/../../evidence/r2-ksh-embedded/scan-embedded-defs.py" "$LIB" >"$TMP/embedded.txt" 2>/dev/null; then
    echo "-- embedded in the library (names/licenses) --"
    grep -E 'resource names|zstd frames|license classes|strict-GPL' "$TMP/embedded.txt" | sed 's/^/   /'
  fi
fi

echo
if [ "$fail" -eq 0 ] && [ "$RC" -eq 0 ]; then
  echo "SELFCHECK PASS"; exit 0
fi
echo "SELFCHECK FAIL"; exit 1
