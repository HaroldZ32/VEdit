#!/usr/bin/env bash
# Run the C17 overlay probe inside the probe image and collect evidence.
#
# Usage (from the repository root):
#   docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp \
#       -v "$PWD":/work -w /work/probes/c17-overlay \
#       vprobe-qt6kf6:1 bash scripts/run-probe.sh /work/evidence/r2-c17
#
# Prerequisite: the probe image was built from image/Dockerfile (see
# evidence/r2-c17/00-pins.txt for the image id and package versions).
set -uo pipefail

OUT="${1:-$PWD/../../evidence/r2-c17}"
mkdir -p "$OUT"
# Regenerate evidence from scratch: remove only files this probe writes.
rm -f "$OUT"/00-pins.txt "$OUT"/01-*.log "$OUT"/02*.log "$OUT"/02*.txt "$OUT"/03-*.txt \
      "$OUT"/04-*.txt "$OUT"/05-*.txt "$OUT"/06-*.txt "$OUT"/09-*.txt "$OUT"/*.png
FAILED=0

echo "== 00 environment ==" | tee "$OUT/00-pins.txt"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "image: vprobe-qt6kf6:1"
  echo "cmake: $(cmake --version | head -1)"
  echo "compiler: $(c++ --version | head -1)"
  echo "qt: $(qmake6 -query QT_VERSION 2>/dev/null || echo unknown)"
  echo "image tag: vprobe-qt6kf6:1 (image id captured host-side, see 00-image-id.txt)"
  echo "KF6TextEditor version: $(grep -m1 'set(PACKAGE_VERSION' /usr/lib/x86_64-linux-gnu/cmake/KF6TextEditor/KF6TextEditorConfigVersion.cmake 2>/dev/null || echo unknown)"
  echo "kf6 packages:"
  dpkg -l | awk '/libkf6texteditor|libkf6syntaxhighlighting|qt6-base-dev /{print "  " $2, $3}'
  echo "katepart plugin: $(ls /usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/parts/katepart.so 2>/dev/null || echo missing)"
  echo "installed KTextEditor public headers: $(ls /usr/include/KF6/KTextEditor/ktexteditor/*.h | wc -l) files"
  echo "installed folding-related public API symbols (grep -c fold):"
  grep -ril 'fold' /usr/include/KF6/KTextEditor/ktexteditor/ | sed 's|^|  |'
  echo "  total header files mentioning folding: $(grep -ril 'fold' /usr/include/KF6/KTextEditor/ktexteditor/ | wc -l)"
  echo "host-side range/attribute API (installed header evidence):"
  grep -n 'virtual void setAttribute\|virtual const Attribute::Ptr &attribute\|virtual qreal zDepth\|newMovingRange' \
      /usr/include/KF6/KTextEditor/ktexteditor/movingrange.h /usr/include/KF6/KTextEditor/ktexteditor/document.h \
      | sed 's|^|  |'
  echo "definition/theme embedding statement (installed Repository header):"
  grep -n 'compiled into the shared' /usr/include/KF6/KSyntaxHighlighting/KSyntaxHighlighting/repository.h \
      | sed 's|^|  |'
  echo "external verilog.xml definitions in /usr/share: $(find /usr/share -name 'verilog.xml' 2>/dev/null | wc -l)"
} | tee -a "$OUT/00-pins.txt"

echo "== 01 build ==" | tee "$OUT/01-build.log"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tee -a "$OUT/01-build.log"
rc=${PIPESTATUS[0]}
if [ "$rc" -ne 0 ]; then
  echo "[configure exit=$rc]" | tee -a "$OUT/01-build.log"
  exit "$rc"
fi
cmake --build build 2>&1 | tee -a "$OUT/01-build.log"
rc=${PIPESTATUS[0]}
echo "[build exit=$rc]" | tee -a "$OUT/01-build.log"
if [ "$rc" -ne 0 ]; then
  exit "$rc"
fi

echo "== 02 run under Xvfb ==" | tee "$OUT/02-run.log"
# A *fresh, empty* HOME is intentional: the probe must not depend on any
# external syntax definition file, which proves the definitions are the ones
# compiled into libKF6SyntaxHighlighting.
export HOME="${PROBE_HOME:-/tmp/c17-home}"
rm -rf "$HOME"
mkdir -p "$HOME"
echo "HOME=$HOME (empty: no custom syntax definitions installed)" | tee -a "$OUT/02-run.log"

xvfb-run -a -s "-screen 0 1600x1000x24" \
  ./build/c17-overlay --out "$OUT" --lines 120000 --steps 200 --step-lines 3 \
  2>&1 | tee -a "$OUT/02-run.log"
rc=${PIPESTATUS[0]}
echo "[run exit=$rc]" | tee -a "$OUT/02-run.log"
[ "$rc" -eq 0 ] || FAILED=$((FAILED+1))

echo "== 02b run again with Verilog base highlighting active (paint cost with both layers) ==" | tee "$OUT/02b-run-verilog.log"
xvfb-run -a -s "-screen 0 1600x1000x24" \
  ./build/c17-overlay --out "$OUT" --perf-only --perf-mode Verilog --lines 120000 --steps 120 \
  --step-lines 3 2>&1 | tee -a "$OUT/02b-run-verilog.log"
rc=${PIPESTATUS[0]}
echo "[run perf-mode Verilog exit=$rc]" | tee -a "$OUT/02b-run-verilog.log"
[ "$rc" -eq 0 ] || FAILED=$((FAILED+1))

echo "== 03 screenshots ==" | tee "$OUT/03-artifacts.txt"
ls -la "$OUT"/*.png 2>/dev/null | tee -a "$OUT/03-artifacts.txt" || true
sha256sum "$OUT"/*.png 2>/dev/null | tee -a "$OUT/03-artifacts.txt" || true

echo "== RESULT: failures=$FAILED ==" | tee "$OUT/09-result.txt"
exit "$FAILED"
