#!/usr/bin/env bash
# Gate `vendor`: the bundled third-party components must be named, with their
# licenses, in THIRD_PARTY_NOTICES.md (T29's deliverable). Until that file
# exists this gate is honestly RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

notice="$ROOT/THIRD_PARTY_NOTICES.md"
if [ ! -s "$notice" ]; then
  no_gate "[vendor] 本项目暂无第三方许可清单（T29 负责）：THIRD_PARTY_NOTICES.md 缺失"
fi

fail=0
# The product bundles these components (requirements 3/4/1); each must be
# attributed in the notice file.
for token in slang verible gtk; do
  grep -qi "$token" "$notice" || {
    echo "[vendor] FAIL: THIRD_PARTY_NOTICES.md does not name $token"
    fail=1
  }
done
grep -qE 'MIT|Apache|GPL|LGPL|BSD' "$notice" || {
  echo "[vendor] FAIL: THIRD_PARTY_NOTICES.md carries no license identifier"
  fail=1
}
[ "$fail" -eq 0 ] || exit 1
echo "[vendor] PASS: bundled components and licenses are attributed"
