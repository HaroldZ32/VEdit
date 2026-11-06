#!/usr/bin/env bash
# T19 session-restore probe — runs INSIDE the pinned Qt6/KF6 image.
#
# Builds evidence/i3-session/probe/session_restore_probe.cpp against the already
# built `libvedit_app_shell.a`, then runs it TWICE:
#
#   * offscreen — no display at all (the same environment ctest uses);
#   * xcb under Xvfb — a real X server, which is the only mode in which Qt's
#     restoreGeometry() is not clamped by the plugin's fixed virtual screen, so
#     the window size AND position assertion is exact there.
#
# The probe drives two ApplicationShell instances over one state directory (the
# A/B restart scenario of AC-10.1.3.2, the split-layout scenario of AC-10.1.3.1,
# the v1/missing-file tolerance cases and the dirty-close rule), so its logs are
# the evidence.
#
#   bash evidence/i3-session/probe/container-run.sh                 # demo provider
#   VEDIT_PROBE_KEDITOR=1 bash evidence/i3-session/probe/container-run.sh
#                                                                  # real KTextEditor
set -uo pipefail
WORK="${VEDIT_WORKSPACE:-/vedit-build}"
cd "$WORK" || exit 2

LOG="evidence/i3-session/logs"
BIN="target/i3-session-probe"
mkdir -p "$LOG" "$BIN"

mode="demo"
defines=()
extra_includes=()
extra_libs=()
if [ "${VEDIT_PROBE_KEDITOR:-0}" = "1" ]; then
  mode="ktexteditor"
  defines=(-DVEDIT_PROBE_HAVE_EDIT_CORE=1)
  extra_includes=(-I"${WORK}/src/editor" -I"${WORK}/src/editor/kernel" -I/usr/include/KF6/KTextEditor)
  extra_libs=("${WORK}/target/qt6-build/src/app/editor-lane/libvedit_editor_core.a")
fi

build_log="${LOG}/00-probe-build-${mode}.txt"
echo "== probe build (mode=${mode}) ==" | tee "${build_log}"
# shellcheck disable=SC2086
g++ -std=c++20 -O2 -fPIC "${defines[@]}" \
  -I"${WORK}/src/app" -I/usr/include/KF6/SyntaxHighlighting \
  "${extra_includes[@]}" \
  $(pkg-config --cflags Qt6Widgets KF6CoreAddons) \
  evidence/i3-session/probe/session_restore_probe.cpp \
  target/qt6-build/src/app/libvedit_app_shell.a \
  "${extra_libs[@]}" \
  -o "${BIN}/session-restore-probe-${mode}" \
  $(pkg-config --libs Qt6Widgets KF6CoreAddons) \
  -lKF6SyntaxHighlighting -ldl -lm 2>&1 | tee -a "${build_log}"
build_status=${PIPESTATUS[0]}
echo "build exit=${build_status}" | tee -a "${build_log}"
if [ "${build_status}" -ne 0 ]; then
  exit "${build_status}"
fi

probe="${BIN}/session-restore-probe-${mode}"
overall=0

run_offscreen() {
  local log="${LOG}/10-probe-run-offscreen-${mode}.txt"
  echo "== probe run (mode=${mode}, QT_QPA_PLATFORM=offscreen) ==" | tee "${log}"
  rm -rf "${BIN}/work-offscreen-${mode}"
  QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="/tmp/xdg-i3-probe" \
    "${probe}" \
    --work="${BIN}/work-offscreen-${mode}" \
    --out="evidence/i3-session/run-offscreen-${mode}" \
    2>&1 | tee -a "${log}"
  local status=${PIPESTATUS[0]}
  echo "probe exit=${status}" | tee -a "${log}"
  return "${status}"
}

run_xcb() {
  local log="${LOG}/10-probe-run-xcb-${mode}.txt"
  echo "== probe run (mode=${mode}, QT_QPA_PLATFORM=xcb under Xvfb 1440x900) ==" | tee "${log}"
  rm -rf "${BIN}/work-xcb-${mode}"
  if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "SKIP: xvfb-run is not installed in this image" | tee -a "${log}"
    echo "probe exit=skip (no Xvfb)" | tee -a "${log}"
    return 0
  fi
  xvfb-run -a -s "-screen 0 1440x900x24" \
    env QT_QPA_PLATFORM=xcb XDG_RUNTIME_DIR="/tmp/xdg-i3-probe-xcb" \
    "${probe}" \
    --work="${BIN}/work-xcb-${mode}" \
    --out="evidence/i3-session/run-xcb-${mode}" \
    2>&1 | tee -a "${log}"
  local status=${PIPESTATUS[0]}
  echo "probe exit=${status}" | tee -a "${log}"
  return "${status}"
}

run_offscreen || overall=1
run_xcb || overall=1
exit "${overall}"
