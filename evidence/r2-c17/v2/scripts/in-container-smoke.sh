#!/usr/bin/env bash
# T59 / C17 v2 — re-run the M0 smoke INSIDE the official build image, so the
# anchor line `M0-SMOKE ok=1 ... qt=6.10.2 kf6=6.24.0` is observed for the same
# image the C17 probe runs on.
#
# Same shape as scripts/build-qt-in-container.sh (Xvfb + xcb + software GL).
# Display :91 is used instead of :77 only to avoid colliding with a concurrent
# T10 run; the display number is not part of the measurement.
set -u

# The smoke binary resolves its custom-syntax-path fixture relative to the
# workspace root it was configured with (/vedit-build), so the repository MUST
# be mounted at /vedit-build for this step (scripts/build-qt-in-container.sh
# uses the same path). Mounting elsewhere makes custom_path=0 and the smoke
# fails on its own fixture lookup -- not on anything C17 measures.
SMOKE=/vedit-build/target/qt6-build/cmake/smoke/vedit-m0-smoke
if [ ! -x "$SMOKE" ]; then
  echo "FATAL: smoke binary missing: $SMOKE"
  exit 1
fi

Xvfb :91 -screen 0 1280x800x24 >/tmp/xvfb-smoke.log 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
sleep 2

echo "## smoke binary sha256"
sha256sum "$SMOKE"
echo "## renderer"
DISPLAY=:91 LIBGL_ALWAYS_SOFTWARE=1 glxinfo -B 2>&1 | grep -E 'OpenGL renderer|OpenGL version' || true
echo "## smoke (QT_QPA_PLATFORM=xcb, QT_OPENGL=software, LIBGL_ALWAYS_SOFTWARE=1)"
DISPLAY=:91 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
  "$SMOKE" --selfcheck 2>&1
rc=$?
echo "[smoke exit=$rc]"
exit "$rc"
