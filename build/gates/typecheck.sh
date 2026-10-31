#!/usr/bin/env bash
# Gate `typecheck`: cargo clippy --workspace --all-targets -- -D warnings, plus
# the C++/Qt6 lane's static check (configure + build with warnings-as-errors
# inside the pinned image). Declared fallback: if clippy is not installed, cargo
# check --workspace runs and the script says so on the same line (no silent
# downgrade). The Rust workspace is created by T7, the Qt6 build base by T10;
# until one exists that half is honestly RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

# --- C++/Qt6 lane (M0 build base, T10) ---------------------------------------
# The Qt6 lane's own code is compiled with -Wall -Wextra -Wpedantic -Werror in
# cmake/smoke/CMakeLists.txt, so `--no-test` is the C++ static check: a warning
# in our code fails this gate. (The pinned image carries no clang-tidy by
# design; adding a second analyser is a later decision, not a silent downgrade.)
if [ -f "$ROOT/CMakeLists.txt" ] && [ -f "$ROOT/scripts/versions-qt.env" ]; then
  echo "[typecheck] C++/Qt6 lane: configure + build with warnings-as-errors (pinned image)"
  bash "$ROOT/build/gates/qt6-cxx.sh" --no-test \
    || no_gate "[typecheck] FAIL: the C++/Qt6 lane failed to configure/build with -Werror"
else
  echo "[typecheck] N/A: no Qt6 build base yet (T10); the C++ lane has nothing to check."
fi

if [ ! -f "$ROOT/Cargo.toml" ]; then
  no_gate "[typecheck] 本项目无此门禁：尚无 Cargo workspace（Cargo.toml 不存在），Rust 骨架由 T7 交付；此门禁保持 RED，不跳过。"
fi
cd "$ROOT"
if cargo clippy --version >/dev/null 2>&1; then
  echo "[typecheck] cargo clippy --workspace --all-targets -- -D warnings"
  exec cargo clippy --workspace --all-targets -- -D warnings
fi
echo "[typecheck] clippy 未安装：声明式退化为 cargo check --workspace"
exec cargo check --workspace
