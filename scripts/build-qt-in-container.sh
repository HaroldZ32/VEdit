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
# Rust service layer (crates/vd-ffi, Slice 0 / task T4): the static library and the
# cbindgen header are produced by scripts/build-qt.sh BEFORE this container runs
# (the Qt6 image has no Rust toolchain). Configure fails if either is missing.
RUST_LIB="$WORK/target/release/libvd_ffi.a"
RUST_INC="$WORK/crates/vd-ffi/include"
for required in "$RUST_LIB" "$RUST_INC/vd_ffi.h"; do
  if [ ! -f "$required" ]; then
    echo "FATAL: the Rust service layer is missing: $required" >&2
    echo "       run scripts/build-qt.sh, which builds it in the pinned Rust image" >&2
    exit 1
  fi
done
cmake -S "$WORK" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra" \
  -DVEDIT_RUST_FFI_LIB="$RUST_LIB" \
  -DVEDIT_RUST_FFI_INCLUDE="$RUST_INC" 2>&1 | tee "$EVID/31-qt6-configure.log"

echo "[qt6] cmake build"
cmake --build "$BUILD_DIR" --parallel "$(nproc)" 2>&1 | tee "$EVID/32-qt6-build.log"

SMOKE="$BUILD_DIR/cmake/smoke/vedit-m0-smoke"
if [ ! -x "$SMOKE" ]; then
  echo "FATAL: the M0 smoke binary was not produced: $SMOKE" >&2
  exit 1
fi

# The product binary must exist and must carry the Rust ABI (AC-0.1/AC-S0.1).
# Static linking is the expected case, so the proof is the exported C symbols.
APP="$BUILD_DIR/src/app/vedit-app"
if [ ! -x "$APP" ]; then
  echo "FATAL: the product binary was not produced: $APP" >&2
  exit 1
fi
echo "[qt6] Rust C ABI symbols in vedit-app"
{
  echo "## file"
  file "$APP"
  echo "## exported vd_* C ABI symbols (nm)"
  nm -C "$APP" | grep -E ' T vd_[a-z_]+$' | sort || true
  echo "## count"
  nm -C "$APP" | grep -cE ' T vd_[a-z_]+$' || true
  echo "## highest GLIBC symbol the product binary requires (the private runtime supplies 2.43)"
  readelf -V "$APP" | grep -o 'GLIBC_[0-9.]*' | sort -V -u | tail -1 || true
  echo "## sha256"
  sha256sum "$APP"
} 2>&1 | tee "$EVID/37-qt6-rust-abi.txt"
# The check must not die of SIGPIPE: this script runs under `set -o pipefail`, and
# `grep -q` exits at the first match, which kills a still-writing `nm` with
# SIGPIPE(141) — the pipeline then reads as failure and the gate reports a false
# "does not export vd_abi_version" while the symbol is right there. `grep` without
# `-q` reads the whole stream, so `nm` exits normally and the status is the grep
# result only. The check itself is unchanged: match => 0, missing => 1 => FATAL.
if ! nm -C "$APP" | grep -E ' T vd_abi_version$' > /dev/null; then
  echo "FATAL: vedit-app does not export vd_abi_version; the Rust service layer is not linked" >&2
  exit 1
fi
if [ "$(nm -C "$APP" | grep -cE ' T vd_[a-z_]+$')" -lt 20 ]; then
  echo "FATAL: fewer than 20 vd_* C ABI symbols in vedit-app; the seven services are not all linked" >&2
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
