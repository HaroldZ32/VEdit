#!/usr/bin/env bash
# T73 acceptance checks (text-only repairs).
#
#   bash evidence/text-fixes/verify.sh
#
# Exit 0 only when: the SLANG contradiction is gone, the backfill marker + both
# timestamps are present, USER_GUIDE no longer claims file-open decides the
# project root, and the repository docs gate passes.
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$ROOT"

pass=0
fail=0
ok() { pass=$((pass + 1)); printf 'CHECK PASS  %s\n' "$*"; }
bad() { fail=$((fail + 1)); printf 'CHECK FAIL  %s\n' "$*"; }

echo "== T73 acceptance checks =="
echo "collected_at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "git_head: $(git rev-parse HEAD)"
echo

# 1. No self-contradicting SLANG sentence remains.
if grep -rn 'NOT proven\|SLANG success is SLANG' evidence/lsp-v2/ >/dev/null 2>&1; then
    bad "found 'NOT proven' / mangled 'SLANG success is SLANG' in evidence/lsp-v2/"
    grep -rn 'NOT proven\|SLANG success is SLANG' evidence/lsp-v2/ | head -5
else
    ok "no 'NOT proven' or mangled 'SLANG success is SLANG' in evidence/lsp-v2/"
fi

# 2. The report states both SLANG paths consistently.
grep -q 'Default environment (no `VEDIT_SLANG`)' evidence/lsp-v2/09-report.md \
    && ok "09-report documents the default typed-degradation path" \
    || bad "09-report lacks the default typed-degradation wording"
grep -q 'VEDIT_SLANG=build/slang/out/slang' evidence/lsp-v2/09-report.md \
    && ok "09-report documents the VEDIT_SLANG success path" \
    || bad "09-report lacks the VEDIT_SLANG success wording"
grep -q 'requested=7 completed=7 failures=0' evidence/lsp-v2/09-report.md \
    && ok "09-report cites the 16-f1 success numbers" \
    || bad "09-report does not cite 16-f1 numbers"

# 3. Backfill marker + both moments, body values untouched.
markers=$(grep -rl '^# C9 header — backfill:' evidence/lsp-v2/ | wc -l | tr -d ' ')
[ "$markers" -ge 15 ] && ok "backfill headers present in $markers files" \
    || bad "only $markers backfill headers (expected >=15)"
grep -rq 'original collection window 2026-10-31T10:57:00Z..2026-10-31T11:11:00Z' evidence/lsp-v2/ \
    && ok "original collection window (10:57–11:11Z) recorded" \
    || bad "original collection window missing"
grep -rq 'original git_head=1f47e56421bbeaab64f8082df3cfc104a6a2c2ef' evidence/lsp-v2/ \
    && ok "original git_head 1f47e56 recorded" \
    || bad "original git_head missing"
grep -rq 'observed body range 2026-10-31T10:56:38Z..2026-10-31T11:11:06Z' evidence/lsp-v2/ \
    && ok "observed original body range recorded (10:56:38–11:11:06Z)" \
    || bad "observed original body range missing"
grep -rq 'header written at 2026-10-31T11:28:59Z on HEAD=95c496e682e3841add1a081edd78d29c34957aac' evidence/lsp-v2/ \
    && ok "backfill time/HEAD (11:28:59Z / 95c496e) recorded" \
    || bad "backfill time/HEAD missing"

# 4. USER_GUIDE: no file-open-decides-root claim; --project + negation present.
if grep -n '打开文件' docs/USER_GUIDE.md | grep -Eq '决定从|决定工程根|从打开位置'; then
    bad "USER_GUIDE still claims file-open decides the project root"
    grep -n '打开文件' docs/USER_GUIDE.md
else
    ok "USER_GUIDE does not claim file-open decides the project root"
fi
grep -q -- '--project <dir>' docs/USER_GUIDE.md \
    && ok "USER_GUIDE documents --project <dir>" \
    || bad "USER_GUIDE lacks --project <dir>"
grep -q '不是由打开文件推导' docs/USER_GUIDE.md \
    && ok "USER_GUIDE states 'not derived from the opened file'" \
    || bad "USER_GUIDE lacks the explicit negation"
grep -q 'eq_ignore_ascii_case("f")' docs/USER_GUIDE.md \
    && ok "USER_GUIDE keeps the .F case-insensitivity fact" \
    || bad "USER_GUIDE lost the .F case-insensitivity fact"

# 5. Repository docs gate.
echo
echo "cmd: bash build/gates/docs.sh"
bash build/gates/docs.sh || bad "docs gate failed"

echo
echo "== summary: pass=$pass fail=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
