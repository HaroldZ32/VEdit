#!/usr/bin/env bash
# Gate `manifest`: the Rust workspace manifests must be complete and consistent.
# The workspace is created by T7; until it exists this gate is honestly RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

if [ ! -f "$ROOT/Cargo.toml" ]; then
  no_gate "[manifest] 本项目无此门禁：尚无 Cargo workspace（Cargo.toml 不存在），Rust 骨架由 T7 交付；此门禁保持 RED，不跳过。"
fi

fail=0
if grep -q '^\[workspace\]' "$ROOT/Cargo.toml"; then
  members=$(sed -n '/^\[workspace\]/,/^\[/p' "$ROOT/Cargo.toml" | grep -o '"crates/[^"]*"' | tr -d '"')
  for member in $members; do
    if [ ! -f "$ROOT/$member/Cargo.toml" ]; then
      echo "[manifest] FAIL: workspace member $member has no Cargo.toml"
      fail=1
    fi
  done
else
  echo "[manifest] FAIL: root Cargo.toml has no [workspace] section"
  fail=1
fi

for manifest in "$ROOT"/crates/*/Cargo.toml; do
  [ -f "$manifest" ] || continue
  for key in name version edition; do
    grep -qE "^${key}[[:space:]]*=" "$manifest" || {
      echo "[manifest] FAIL: $manifest has no \`$key\`"
      fail=1
    }
  done
done
[ "$fail" -eq 0 ] || exit 1
echo "[manifest] PASS"
