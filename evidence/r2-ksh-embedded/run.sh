#!/usr/bin/env bash
# Reproducible check: are syntax definitions embedded in libKF6SyntaxHighlighting.so?
#
#   usage: bash evidence/r2-ksh-embedded/run.sh [path/to/libKF6SyntaxHighlighting.so*]
#
# Controls (a gate that cannot fail is worthless):
#   * POSITIVE control - the distro 6.24.0 library: the scan must FIND SystemVerilog
#     and strict-GPL definitions, and `--expect-clean` must exit 1.
#   * NEGATIVE control - libKF6TextEditor.so (no syntax data): the scan must find 0
#     definitions and `--expect-clean` must exit 0.
#
# Exit code: 0 when both controls behave as stated, 1 otherwise.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCAN="$HERE/scan-embedded-defs.py"

LIB=${1:-/usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0}
if [ ! -e "$LIB" ]; then
  LIB=$(ls -1 /usr/lib/*/libKF6SyntaxHighlighting.so.6.* 2>/dev/null | head -1)
fi
if [ -z "${LIB:-}" ] || [ ! -e "$LIB" ]; then
  echo "FAIL: libKF6SyntaxHighlighting.so not found; pass its path as \$1" >&2
  exit 1
fi

NEG=$(ls -1 /usr/lib/*/libKF6TextEditor.so.6.* 2>/dev/null | head -1)

fail=0
echo "### POSITIVE control: distro library (definitions ARE embedded) ###"
python3 "$SCAN" "$LIB"; scan_rc=$?
echo "scan exit=$scan_rc (expected 0)"
python3 "$SCAN" --expect-clean "$LIB" > /dev/null 2>&1; clean_rc=$?
echo "expect-clean exit=$clean_rc (expected 1: the gate must catch the embedded GPL definitions)"
[ "$scan_rc" -eq 0 ] || fail=1
[ "$clean_rc" -eq 1 ] || { echo "FAIL: --expect-clean passed on a library that embeds GPL definitions"; fail=1; }

if [ -n "${NEG:-}" ] && [ -e "$NEG" ]; then
  echo
  echo "### NEGATIVE control: library without syntax data ($NEG) ###"
  python3 "$SCAN" "$NEG"; neg_rc=$?
  python3 "$SCAN" --expect-clean "$NEG" > /dev/null 2>&1; neg_clean_rc=$?
  echo "scan exit=$neg_rc (expected 0) / expect-clean exit=$neg_clean_rc (expected 0)"
  [ "$neg_rc" -eq 0 ] || fail=1
  [ "$neg_clean_rc" -eq 0 ] || { echo "FAIL: negative control flagged a clean library"; fail=1; }
else
  echo
  echo "SKIP: no libKF6TextEditor.so on this host (negative control not run)"
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "== controls PASSED =="
else
  echo "== controls FAILED =="
fi
exit "$fail"
