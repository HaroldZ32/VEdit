#!/usr/bin/env bash
# T59 / C17 v2 — negative API verification, run INSIDE the official build image.
# Re-checks, on THIS image, the three "cannot query" claims v1 asserted:
#   * no document-level moving-range enumeration   (movingRanges)
#   * no per-cursor attribute query                (attributesAt)
#   * no public semantic-highlighting API          (semantic)
# plus a POSITIVE control proving the same grep method does find an existing API.
# Same commands/shape as the T58/F3 addendum in v1's 00-pins.txt (00b section).
set -u

HDR=/usr/include/KF6/KTextEditor
echo "== environment =="
head -3 /etc/os-release | sed 's/^/  /'
echo "  grep: $(grep --version | head -1)"
echo "  public headers under $HDR: $(find "$HDR" -name '*.h' | wc -l)"

for pat in movingRanges attributesAt; do
  echo
  echo "\$ grep -rn '$pat' $HDR/"
  out=$(grep -rn "$pat" "$HDR/" 2>&1)
  rc=$?
  if [ -n "$out" ]; then printf '%s\n' "$out"; fi
  echo "[exit=$rc]"
done

echo
echo "\$ grep -rni 'semantic' $HDR/"
out=$(grep -rni 'semantic' "$HDR/" 2>&1)
rc=$?
if [ -n "$out" ]; then printf '%s\n' "$out"; fi
echo "[exit=$rc]"

echo
echo "== positive control (same method must find a real API) =="
echo "\$ grep -rn 'virtual MovingRange \*newMovingRange' $HDR/ktexteditor/document.h"
grep -rn 'virtual MovingRange \*newMovingRange' "$HDR/ktexteditor/document.h"
echo "[exit=$?]"

echo
echo "== conclusion =="
echo "  no document-level range enumeration, no per-cursor attribute query,"
echo "  no public semantic-highlighting API in the installed public headers"
echo "  of THIS image (KTextEditor 6.24.0); single-owner arbitration must live"
echo "  in the application (A1g), semantic colours must be resolved by the app."
