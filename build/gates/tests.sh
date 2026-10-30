#!/usr/bin/env bash
# Gate `tests`: the Rust workspace test suite (cargo test --workspace).
# The workspace is created by T7; until it exists this gate is honestly RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

if [ ! -f "$ROOT/Cargo.toml" ]; then
  no_gate "[tests] 本项目无此门禁：尚无 Cargo workspace（Cargo.toml 不存在），Rust 骨架由 T7 交付；此门禁保持 RED，不跳过。"
fi
cd "$ROOT"
echo "[tests] cargo test --workspace"
exec cargo test --workspace
