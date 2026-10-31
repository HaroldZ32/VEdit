#!/usr/bin/env bash
# Runs INSIDE the pinned Qt6/KF6 build image (scripts/docker/Dockerfile.build,
# stage `qt6-build`); invoked by scripts/build-qt.sh. It never builds the image
# and never touches the host: configure, build, ctest, ELF facts, and the
# xcb + software-rendering window smoke.
#
#   bash scripts/build-qt-in-container.sh --tests | --no-tests
set -euo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
BUILD_DIR="$WORK/target/qt6-build"
EVID="$WORK/evidence/m0-build/build-logs"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime}"
mkdir -p "$BUILD_DIR" "$EVID" "$RUNTIME_DIR"
chmod 700 "$RUNTIME_DIR" 2>/dev/null || true

mode="${1:---tests}"
case "$mode" in
  --tests | --no-tests) ;;
  *)
    echo "FATAL: unknown mode: $mode (supported: --tests, --no-tests)" >&2
    exit 2
    ;;
esac

echo "[qt6] container facts"
{
  head -3 /etc/os-release
  echo "glibc: $(ldd --version | head -1)"
  echo "qt6-base-dev: $(dpkg-query -W -f='${Version}' qt6-base-dev)"
  echo "kf6-texteditor: $(dpkg-query -W -f='${Version}' libkf6texteditor-dev)"
  echo "kf6-syntaxhighlighting: $(dpkg-query -W -f='${Version}' libkf6syntaxhighlighting-dev)"
  echo "tree-sitter (pkg-config): $(pkg-config --modversion tree-sitter)"
  echo "cmake: $(cmake --version | head -1)"
  echo "g++: $(g++ --version | head -1)"
} 2>&1 | tee "$EVID/30-qt6-container-facts.txt"

# Facts recorded when the image was built (locked-version assertions happened there).
cat /opt/vedit/build-image.txt 2>/dev/null | tee "$EVID/30-qt6-image-facts.txt" || true
cp /opt/vedit/package-snapshot.txt "$EVID/30-qt6-package-snapshot.txt" 2>/dev/null || true

echo "[qt6] cmake configure (versions are requested EXACT from scripts/versions-qt.env)"
cmake -S "$WORK" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra" 2>&1 | tee "$EVID/31-qt6-configure.log"

echo "[qt6] cmake build"
cmake --build "$BUILD_DIR" --parallel "$(nproc)" 2>&1 | tee "$EVID/32-qt6-build.log"

SMOKE="$BUILD_DIR/cmake/smoke/vedit-m0-smoke"
if [ ! -x "$SMOKE" ]; then
  echo "FATAL: the M0 smoke binary was not produced: $SMOKE" >&2
  exit 1
fi

echo "[qt6] ELF facts (T11 must bundle a glibc that satisfies this floor)"
{
  echo "## file"
  file "$SMOKE"
  echo "## NEEDED"
  readelf -d "$SMOKE" | sed -n '/NEEDED/p'
  echo "## highest GLIBC symbol the binary itself requires (informational; the private runtime supplies it)"
  readelf -V "$SMOKE" | grep -o 'GLIBC_[0-9.]*' | sort -V -u | tail -1 || true
  echo "## libraries resolved at build time"
  ldd "$SMOKE" | grep -E 'libc\.so|libstdc|libQt6Core|libKF6TextEditor|libKF6SyntaxHighlighting|libtree-sitter' || true
  echo "## sha256"
  sha256sum "$SMOKE"
} 2>&1 | tee "$EVID/34-qt6-elf.txt"

if [ "$mode" = "--no-tests" ]; then
  echo "[qt6] --no-tests: ctest and the xcb smoke were not run"
  exit 0
fi

echo "[qt6] ctest (offscreen: QMainWindow + KTextEditor document + KSyntaxHighlighting + tree-sitter)"
ctest --test-dir "$BUILD_DIR" --output-on-failure 2>&1 | tee "$EVID/33-qt6-ctest.log"

echo "[qt6] xcb + software-rendering window smoke under Xvfb"
Xvfb :77 -screen 0 1280x800x24 >"$EVID/35-qt6-xvfb.log" 2>&1 &
xvfb_pid=$!
cleanup() { kill "$xvfb_pid" 2>/dev/null || true; }
trap cleanup EXIT
sleep 2
{
  echo "## renderer"
  DISPLAY=:77 LIBGL_ALWAYS_SOFTWARE=1 glxinfo -B 2>&1 | grep -E 'OpenGL renderer|OpenGL version' || true
  echo "## smoke (QT_QPA_PLATFORM=xcb, QT_OPENGL=software, LIBGL_ALWAYS_SOFTWARE=1)"
  DISPLAY=:77 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
    "$SMOKE" --selfcheck 2>&1
} | tee "$EVID/36-qt6-xcb-smoke.log"
cleanup
trap - EXIT
