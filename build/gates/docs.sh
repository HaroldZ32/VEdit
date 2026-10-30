#!/usr/bin/env bash
# Gate `docs`: the user-facing documents must exist and carry real content.
# README/BUILD/USER_GUIDE/THIRD_PARTY_NOTICES are T29's deliverable; until they
# exist this gate is honestly RED and says which files are missing.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

required=(README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md)
missing=()
for rel in "${required[@]}"; do
  [ -s "$ROOT/$rel" ] || missing+=("$rel")
done
if [ "${#missing[@]}" -gt 0 ]; then
  no_gate "[docs] 本项目暂无文档门禁产物（T29 负责）：缺少 ${missing[*]}"
fi

fail=0
for rel in "${required[@]}"; do
  # A doc that is one line long does not pass; require at least 10 non-empty lines.
  lines=$(grep -cve '^[[:space:]]*$' "$ROOT/$rel" || true)
  if [ "${lines:-0}" -lt 10 ]; then
    echo "[docs] FAIL: $rel has only ${lines:-0} non-empty lines"
    fail=1
  fi
done
[ "$fail" -eq 0 ] || exit 1
echo "[docs] PASS: ${required[*]}"
