#!/usr/bin/env bash
# Run the fold-interface probe inside the probe image and collect evidence.
#
# Usage (from the repository root):
#   docker run --rm -v "$PWD":/work -w /work/probes/fold-interface \
#       vprobe-qt6kf6:1 bash scripts/run-probe.sh /work/evidence/r2-fold
set -uo pipefail

OUT="${1:-$PWD/../../evidence/r2-fold}"
mkdir -p "$OUT"
# Regenerate evidence from scratch: remove only files this probe writes.
rm -f "$OUT"/00-pins.txt "$OUT"/01-*.log "$OUT"/02*.log "$OUT"/02*.txt "$OUT"/03-*.txt \
      "$OUT"/04-*.txt "$OUT"/05-*.txt "$OUT"/06-*.txt "$OUT"/09-*.txt "$OUT"/*.png
FAILED=0

echo "== 00 environment + installed-header inventory ==" | tee "$OUT/00-pins.txt"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "image tag: vprobe-qt6kf6:1 (image id captured host-side, see 00-image-id.txt)"
  echo "qt: $(qmake6 -query QT_VERSION 2>/dev/null || echo unknown)"
  echo "kf6 packages:"
  dpkg -l | awk '/libkf6texteditor|libkf6syntaxhighlighting/{print "  " $2, $3}'
  echo "public KTextEditor headers mentioning folding:"
  grep -ril 'fold' /usr/include/KF6/KTextEditor/ktexteditor/ | sed 's|^|  |'
  echo "  (matches, all of them config-key documentation:)"
  grep -rin 'fold' /usr/include/KF6/KTextEditor/ktexteditor/*.h | head -20 | sed 's|^|  |'
  echo "public KTextEditor headers declaring a folding class/interface:"
  grep -rn -E 'class [A-Za-z_:]*Fold|FoldingModel|CodeFoldingModel' /usr/include/KF6/KTextEditor/ | sed 's|^|  |' || echo "  NONE"
  echo "internal implementation (not installed) referenced for the report:"
  echo "  src/buffer/katetextfolding.h:32  class TextFolding (KatePart internal)"
  echo "  src/view/kateview.h:827         KateView::textFolding() (internal accessor)"
  echo "  src/utils/katecmds.cpp:258      command 'fold' -> newFoldingRange(range, Persistent|Folded)"
  echo "  src/utils/katecmds.cpp:261      command 'tfold' -> newFoldingRange(range, Folded)"
  echo "  src/utils/katecmds.cpp:264      command 'unfold' -> unfoldRange at cursor line"
} | tee -a "$OUT/00-pins.txt"

echo "== 01 build ==" | tee "$OUT/01-build.log"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tee -a "$OUT/01-build.log"
rc=${PIPESTATUS[0]}
[ "$rc" -eq 0 ] || { echo "[configure exit=$rc]" | tee -a "$OUT/01-build.log"; exit "$rc"; }
cmake --build build 2>&1 | tee -a "$OUT/01-build.log"
rc=${PIPESTATUS[0]}
echo "[build exit=$rc]" | tee -a "$OUT/01-build.log"
[ "$rc" -eq 0 ] || exit "$rc"

echo "== 02 run under Xvfb ==" | tee "$OUT/02-run.log"
export HOME="${PROBE_HOME:-/tmp/fold-home}"
rm -rf "$HOME"; mkdir -p "$HOME"
echo "HOME=$HOME (empty; folding must not depend on external syntax data)" | tee -a "$OUT/02-run.log"
xvfb-run -a -s "-screen 0 1600x1000x24" ./build/fold-probe --out "$OUT" 2>&1 | tee -a "$OUT/02-run.log"
rc=${PIPESTATUS[0]}
echo "[run exit=$rc]" | tee -a "$OUT/02-run.log"
[ "$rc" -eq 0 ] || FAILED=$((FAILED+1))

echo "== 03 artifacts ==" | tee "$OUT/03-artifacts.txt"
ls -la "$OUT"/*.png 2>/dev/null | tee -a "$OUT/03-artifacts.txt" || true
sha256sum "$OUT"/*.png 2>/dev/null | tee -a "$OUT/03-artifacts.txt" || true

echo "== RESULT: failures=$FAILED ==" | tee "$OUT/09-result.txt"
exit "$FAILED"
