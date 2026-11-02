#!/usr/bin/env bash
# Gate `tests`: the Rust workspace test suite (cargo test --workspace) and the
# C++/Qt6 lane's ctest suite (the M0 smoke test, T10).
# NOTE: this id is UNREACHABLE in the MPD ledger - the plugin's fixed gate table
# hardcodes `bun test packages` for `tests`, so this script is for humans only and
# is never an evidence source. The ledger-visible test run is folded into
# build/gates/gates.sh (`bun run verify:gates`).
# The Rust workspace is created by T7, the Qt6 build base by T10; until one exists
# that half is honestly RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

# --- C++/Qt6 lane (M0 build base, T10) ---------------------------------------
# No MPD-reachable id covers the C++ tests, so the reachable ledger run is
# folded into build/gates/gates.sh; this copy serves humans running the gate by
# hand and keeps the two lanes' test commands together.
if [ -f "$ROOT/CMakeLists.txt" ] && [ -f "$ROOT/scripts/versions-qt.env" ]; then
  echo "[tests] C++/Qt6 lane: ctest + xcb/software-rendering smoke (pinned image)"
  bash "$ROOT/build/gates/qt6-cxx.sh" \
    || no_gate "[tests] FAIL: the C++/Qt6 lane's ctest/xcb smoke failed"
else
  echo "[tests] N/A: no Qt6 build base yet (T10); the C++ lane has no tests to run."
fi

if [ ! -f "$ROOT/Cargo.toml" ]; then
  no_gate "[tests] 本项目无此门禁：尚无 Cargo workspace（Cargo.toml 不存在），Rust 骨架由 T7 交付；此门禁保持 RED，不跳过。"
fi
cd "$ROOT"
echo "[tests] cargo test --workspace"
exec cargo test --workspace
