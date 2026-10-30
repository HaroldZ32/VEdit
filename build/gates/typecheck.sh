#!/usr/bin/env bash
# Gate `typecheck`: cargo clippy --workspace --all-targets -- -D warnings.
# Declared fallback: if clippy is not installed, cargo check --workspace runs and
# the script says so on the same line (no silent downgrade).
# The workspace is created by T7; until it exists this gate is honestly RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

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
