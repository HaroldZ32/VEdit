#!/usr/bin/env bash
# Gate helper: the Qt6/KF6 C++ lane (M0 build base, task T10).
#
#   bash build/gates/qt6-cxx.sh            # configure + build + ctest + xcb smoke
#   bash build/gates/qt6-cxx.sh --no-test  # configure + build only (static check)
#
# There is no separate MPD gate id for this lane (the ledger's id table is fixed),
# so this helper is called by the three gate scripts that can cover it:
#   * build/gates/gates.sh      -- the one reachable heavy gate, gets the full lane,
#   * build/gates/typecheck.sh  -- the C++ static check (-Werror configure+build),
#   * build/gates/tests.sh      -- humans-only, gets ctest too.
# It is a real gate: a failing configure/build/ctest/xcb-smoke makes the caller RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

if [ ! -f "$ROOT/CMakeLists.txt" ] || [ ! -f "$ROOT/scripts/versions-qt.env" ]; then
  no_gate "[qt6-cxx] 本项目无此门禁：尚无 Qt6 构建底座（CMakeLists.txt / scripts/versions-qt.env 不存在），由 T10 交付；此门禁保持 RED，不跳过。"
fi

mode=""
case "${1:-}" in
  "") ;;
  --no-test) mode="--no-test" ;;
  *)
    echo "[qt6-cxx] unknown argument: $1 (supported: --no-test)" >&2
    exit 2
    ;;
esac

echo "[qt6-cxx] pinned image + cmake configure/build${mode:-/ctest/xcb-smoke}: bash scripts/build-qt.sh ${mode}"
if [ -n "$mode" ]; then
  bash "$ROOT/scripts/build-qt.sh" "$mode"
else
  bash "$ROOT/scripts/build-qt.sh"
fi
