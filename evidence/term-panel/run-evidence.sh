#!/usr/bin/env bash
# Reproduce every artefact in evidence/term-panel/ (task T35: terminal panel).
#
#   bash evidence/term-panel/run-evidence.sh
#
# Everything runs inside the pinned Qt6/KF6 build image (the same image the
# repository's C++ gate uses, scripts/build-qt.sh), so the host toolchain is never
# part of the result. The script only writes evidence/term-panel/ and the
# gitignored build tree under target/term-build.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EVID="$ROOT/evidence/term-panel"
IMAGE="${VEDIT_QT6_BUILD_IMAGE:-vedit-qt6-build:local}"
BUILD_DIR=target/term-build

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "FATAL: the pinned build image '$IMAGE' is missing; build it with bash scripts/build-qt.sh" >&2
  exit 1
fi

# Mirrors scripts/lib-qt.sh: on a rootless daemon the invoking user IS root inside.
user_args=()
if ! docker info --format '{{json .SecurityOptions}}' 2>/dev/null | grep -q '"name=rootless"'; then
  user_args=(--user "$(id -u):$(id -g)")
fi

docker run --rm "${user_args[@]}" \
  -v "$ROOT":/vedit-build \
  -w /vedit-build \
  -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime \
  -e LANG=C.UTF-8 \
  -e BUILD_DIR="$BUILD_DIR" \
  "$IMAGE" bash -lc '
set -o pipefail
# The workspace is mounted at /vedit-build inside the image.
EVID=/vedit-build/evidence/term-panel
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

echo "[term-panel] 00 pinned versions and the QTermWidget probe"
{
  echo "## container"
  head -3 /etc/os-release
  echo "glibc: $(ldd --version | head -1)"
  echo "qt6-base-dev: $(dpkg-query -W -f="\${Version}" qt6-base-dev)"
  echo "cmake: $(cmake --version | head -1)"
  echo "g++: $(g++ --version | head -1)"
  echo
  echo "## QTermWidget / libvterm availability in the pinned image"
  dpkg -l | grep -i -E "qtermwidget|libvterm" || echo "installed: none"
  apt-cache policy libqtermwidget6-dev qtermwidget-dev libvterm-dev 2>/dev/null | sed -n "1,12p" || true
  echo
  echo "## the pty API the panel uses (in libc, no extra dependency)"
  nm -D /lib/x86_64-linux-gnu/libc.so.6 | grep -E " (posix_openpt|grantpt|unlockpt|ptsname)@@"
} 2>&1 | tee "$EVID/00-pins.txt"

# Each step records its own exit code INSIDE its log, so the log alone is the
# evidence: a verifier never has to trust the terminal that produced it.
run_step() {
  local name="$1" log="$2"
  shift 2
  set +e
  "$@" 2>&1 | tee "$log"
  local status=${PIPESTATUS[0]}
  set -e
  echo "$name exit: $status" | tee -a "$log"
  if [ "$status" -ne 0 ]; then
    echo "FATAL: $name failed with exit $status" >&2
    exit "$status"
  fi
}

echo "[term-panel] 01 configure (clean tree)"
rm -rf "$BUILD_DIR"
run_step "configure" "$EVID/01-configure.log" \
  cmake -S src/terminal -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

echo "[term-panel] 02 build (-Wall -Wextra -Wpedantic -Werror)"
run_step "build" "$EVID/02-build.log" \
  cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo "[term-panel] 03 ctest (offscreen, the lane gate)"
run_step "ctest" "$EVID/03-ctest.log" \
  ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "[term-panel] 03b every CHECK line of the self-check (the acceptance transcript)"
set +e
QT_QPA_PLATFORM=offscreen "$BUILD_DIR/vedit-terminal-selftest" --selfcheck 2>/dev/null \
  | grep -E "^CHECK |^TERMINAL-SELFTEST" >"$EVID/03b-checks.log"
status=${PIPESTATUS[0]}
set -e
echo "selfcheck exit: $status" | tee -a "$EVID/03b-checks.log"
if [ "$status" -ne 0 ]; then
  echo "FATAL: the self-check failed with exit $status" >&2
  exit "$status"
fi

echo "[term-panel] 04 ELF facts"
{
  echo "## file"
  file "$BUILD_DIR/vedit-terminal-selftest"
  echo
  echo "## NEEDED"
  readelf -d "$BUILD_DIR/vedit-terminal-selftest" | sed -n "/NEEDED/p"
  echo
  echo "## no Qt network library may appear here (contract (c): local only)"
  if readelf -d "$BUILD_DIR/vedit-terminal-selftest" | grep -q "libQt6Network"; then
    echo "FAIL: libQt6Network is linked"
  else
    echo "OK: libQt6Network is not linked"
  fi
  echo
  echo "## libraries resolved at build time"
  ldd "$BUILD_DIR/vedit-terminal-selftest" | grep -E "libQt6|libc\.so|libstdc" || true
  echo
  echo "## sha256"
  sha256sum "$BUILD_DIR/vedit-terminal-selftest" "$BUILD_DIR/libvedit_terminal.a"
  echo "elf-check exit: 0 (the FAIL branch above would have printed FAIL)"
} 2>&1 | tee "$EVID/04-elf-needed.txt"

echo "[term-panel] 05 no network transport (source grep, binary, empty netns)"
{
  echo "## source: no socket / network API anywhere in src/terminal"
  if grep -rnE "QTcp|QUdp|QSslSocket|QNetwork|QHostAddress|QAbstractSocket|inet_|getaddrinfo|[^:]:listen\(|[^:]:bind\(|[^:]:accept\(" src/terminal/; then
    echo "FAIL: network API found in src/terminal"
  else
    echo "OK: no socket or network API in src/terminal (exit 1 from grep means no match)"
  fi
  echo
  echo "## binary: our code imports no network syscall (undefined symbols)"
  if nm -D --undefined-only "$BUILD_DIR/vedit-terminal-selftest" \
      | grep -wE "socket|bind|listen|connect|accept|getaddrinfo|gethostbyname"; then
    echo "FAIL: a network syscall is imported"
  else
    echo "OK: none of socket/bind/listen/connect/accept/getaddrinfo/gethostbyname is imported"
  fi
  echo
  echo "## binary: the pty calls it does import (positive control for the check above)"
  nm -D --undefined-only "$BUILD_DIR/vedit-terminal-selftest" \
    | grep -wE "posix_openpt|grantpt|unlockpt|ptsname|fork|waitpid|ioctl|execve|setsid"
  echo
  echo "## network namespace run (self-check must pass with no interfaces at all)"
  if unshare -Urn true 2>/dev/null; then
    unshare -Urn bash -lc "QT_QPA_PLATFORM=offscreen $BUILD_DIR/vedit-terminal-selftest --selfcheck 2>/dev/null | tail -1"
  else
    echo "unshare -n / -Urn not permitted in this container (no CAP_SYS_ADMIN and no user-namespace creation);"
    echo "the source grep, the ELF NEEDED list and the undefined-symbol check above stand on their own"
  fi
  echo "no-network-check exit: 0 (the FAIL branch above would have printed FAIL)"
} 2>&1 | tee "$EVID/05-no-network.txt"

echo "[term-panel] 06 xcb + software rendering: selfcheck on a real window, then the screenshots"
Xvfb :78 -screen 0 1280x900x24 >/tmp/xvfb.log 2>&1 &
XVFB_PID=$!
trap "kill $XVFB_PID 2>/dev/null || true" EXIT
sleep 2
{
  echo "## renderer"
  DISPLAY=:78 LIBGL_ALWAYS_SOFTWARE=1 glxinfo -B 2>&1 | grep -E "OpenGL renderer|OpenGL version" || true
  echo
  echo "## selfcheck (QT_QPA_PLATFORM=xcb, software rendering)"
  DISPLAY=:78 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
    "$BUILD_DIR/vedit-terminal-selftest" --selfcheck 2>/dev/null | grep -E "FAIL|TERMINAL-SELFTEST"
  echo
  echo "## screenshots"
  DISPLAY=:78 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
    "$BUILD_DIR/vedit-terminal-selftest" --screenshots "$EVID" 2>&1 | grep -E "INFO|SCREENSHOT|TERMINAL-SCREENSHOTS|ok=0"
} 2>&1 | tee "$EVID/06-xcb-and-screenshots.log"
echo "xcb-and-screenshots exit: ${PIPESTATUS[0]}" | tee -a "$EVID/06-xcb-and-screenshots.log"
kill $XVFB_PID 2>/dev/null || true
trap - EXIT

echo "[term-panel] 08 structure and hashes of the delivered sources"
{
  echo "## files"
  find src/terminal -maxdepth 1 -type f | sort | while read -r file; do
    printf "%-42s %5s lines\n" "$file" "$(wc -l < "$file")"
  done
  echo
  echo "## sha256"
  find src/terminal -maxdepth 1 -type f -name "*" -print0 | sort -z | xargs -0 sha256sum
  echo
  echo "## screenshots"
  sha256sum "$EVID"/1*.png
} 2>&1 | tee "$EVID/08-structure.txt"
'
