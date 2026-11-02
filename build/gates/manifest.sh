#!/usr/bin/env bash
# Gate `manifest`: the Rust workspace manifests must be complete and consistent.
# The workspace is created by T7; until it exists this gate is honestly RED.
#
# Workspace inheritance is a first-class Cargo feature: `version.workspace = true`
# / `edition.workspace = true` in a crate manifest is NOT a missing field -- it is
# a field defined once in the root `[workspace.package]` table. This gate therefore
# accepts either form and, for the inherited form, verifies that the root table
# really defines the key (an inherited key with no workspace definition is a real
# defect and still fails). `name` cannot be inherited by Cargo, so it stays
# mandatory and literal in every crate manifest.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

if [ ! -f "$ROOT/Cargo.toml" ]; then
  no_gate "[manifest] 本项目无此门禁：尚无 Cargo workspace（Cargo.toml 不存在），Rust 骨架由 T7 交付；此门禁保持 RED，不跳过。"
fi

# Print the value of `key` in the root [workspace.package] table, or nothing.
workspace_package_value() {
  local key="$1"
  awk -v key="$key" '
    /^\[/ { in_pkg = ($0 ~ /^\[workspace\.package\]/); next }
    in_pkg {
      line = $0
      sub(/#.*/, "", line)
      if (line ~ "^[[:space:]]*" key "[[:space:]]*=") {
        sub("^[[:space:]]*" key "[[:space:]]*=[[:space:]]*", "", line)
        gsub(/^"|"$/, "", line)
        sub(/[[:space:]]+$/, "", line)
        print line
        exit
      }
    }
  ' "$ROOT/Cargo.toml"
}

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
  crate=$(basename "$(dirname "$manifest")")

  # `name` is never inheritable: Cargo rejects `name.workspace = true`.
  if ! grep -qE '^name[[:space:]]*=' "$manifest"; then
    echo "[manifest] FAIL: $manifest has no literal \`name\`"
    fail=1
  fi

  for key in version edition; do
    if grep -qE "^${key}\.workspace[[:space:]]*=[[:space:]]*true" "$manifest"; then
      value=$(workspace_package_value "$key")
      if [ -z "$value" ]; then
        echo "[manifest] FAIL: $crate/Cargo.toml inherits \`$key\` from [workspace.package], but the root Cargo.toml does not define [workspace.package] $key"
        fail=1
      else
        echo "[manifest] OK: $crate inherits $key=\"$value\" from [workspace.package]"
      fi
    elif grep -qE "^${key}[[:space:]]*=" "$manifest"; then
      echo "[manifest] OK: $crate declares $key literally"
    else
      echo "[manifest] FAIL: $manifest has neither \`$key\` nor \`$key.workspace = true\`"
      fail=1
    fi
  done
done
[ "$fail" -eq 0 ] || exit 1
echo "[manifest] PASS"
