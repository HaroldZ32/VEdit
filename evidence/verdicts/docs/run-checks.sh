#!/usr/bin/env bash
# T34 (independent review of board task T33 / docs-amendments) — read-only re-checks.
#
#   bash evidence/verdicts/docs/run-checks.sh
#
# Writes nothing (stdout only). The caller is expected to prepend the C9 header
# (collected_at / git rev-parse HEAD / git status --porcelain).
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"

pass=0; fail=0; findings=0
ok()   { pass=$((pass+1)); printf 'CHECK PASS  %s\n' "$*"; }
bad()  { fail=$((fail+1)); printf 'CHECK FAIL  %s\n' "$*"; }
note() { findings=$((findings+1)); printf 'FINDING      %s\n' "$*"; }
eq()   { [ "$2" = "$3" ] && ok "$1" || bad "$1 want=$2 got=$3"; }

echo "== A. deliverable identities vs evidence/docs-amendments/01-file-list.txt =="
eq "amendments-rev2.md sha256" 28ea2a07d34b2c5b328f53e20614e83f1be18282f88129a61297af7e2ac8279e "$(sha256sum docs/handover/appendix/amendments-rev2.md | cut -d' ' -f1)"
eq "THIRD_PARTY_NOTICES.md sha256" 8f4827688270bbcae835460c427594976b1c26c620e29021bcba6b140e551eca "$(sha256sum THIRD_PARTY_NOTICES.md | cut -d' ' -f1)"
eq "docs/decisions/r2-license.md sha256" ced6734b6c54968a35384d1a25c3a598527c6b06d2ef5e06da3224fa6034306d "$(sha256sum docs/decisions/r2-license.md | cut -d' ' -f1)"
eq "docs/handover/README.md sha256" a343259300f7bb22d43bc87dec19288142cf1ce0d791e18ed4ed59d149a12957 "$(sha256sum docs/handover/README.md | cut -d' ' -f1)"
eq "amendments-rev2.md is a NEW (untracked) file" "?? docs/handover/appendix/amendments-rev2.md" "$(git status --porcelain -- docs/handover/appendix/amendments-rev2.md)"

echo "== B. C14 artifact identity (requirements.md C14) =="
eq "vedit-verilog.xml sha256" 3d1ec41f0871c001f560ca58b3178bdfd7508ff5f25d095c15820d2feda43e49 "$(sha256sum resources/syntax/vedit-verilog.xml | cut -d' ' -f1)"
eq "vedit-systemverilog.xml sha256" dc6e0a4714762764b9386471bb09e2550c1a9cbcf2b8f67f1a1200f677726f9d "$(sha256sum resources/syntax/vedit-systemverilog.xml | cut -d' ' -f1)"
eq "vedit-verilog.xml lines/bytes" "78 5172" "$(wc -l < resources/syntax/vedit-verilog.xml | tr -d ' ') $(wc -c < resources/syntax/vedit-verilog.xml | tr -d ' ')"
eq "vedit-systemverilog.xml lines/bytes" "89 6276" "$(wc -l < resources/syntax/vedit-systemverilog.xml | tr -d ' ') $(wc -c < resources/syntax/vedit-systemverilog.xml | tr -d ' ')"
eq "vedit-verilog.xml author/license" 'author="VEdit" license="LGPL-2.1-or-later"' "$(grep -o 'author="VEdit" license="LGPL-2.1-or-later"' resources/syntax/vedit-verilog.xml)"
eq "vedit-systemverilog.xml author/license" 'author="VEdit" license="LGPL-2.1-or-later"' "$(grep -o 'author="VEdit" license="LGPL-2.1-or-later"' resources/syntax/vedit-systemverilog.xml)"

echo "== C. tree-sitter-verilog identity (THIRD_PARTY_NOTICES.md §9.2) =="
eq "LICENSE sha256/bytes" "92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46 1082" "$(sha256sum third_party/tree-sitter/verilog/LICENSE | cut -d' ' -f1) $(stat -c %s third_party/tree-sitter/verilog/LICENSE)"
eq "grammar.js sha256/bytes" "323c121a3e550df0f970d46c544b3f02485e5e6d754acb730d6be7b8dea7dc4b 133684" "$(sha256sum third_party/tree-sitter/verilog/grammar.js | cut -d' ' -f1) $(stat -c %s third_party/tree-sitter/verilog/grammar.js)"
eq "src/parser.c sha256/bytes" "a15f78b82068daeb62f3b54c8ad0101aa936a2c921a57806fe8f37dcbf0bfff2 46042888" "$(sha256sum third_party/tree-sitter/verilog/src/parser.c | cut -d' ' -f1) $(stat -c %s third_party/tree-sitter/verilog/src/parser.c)"
eq "src/tree_sitter/parser.h sha256/bytes" "ab104936984904469572a4e868149f7a22fb2929347f837ae6a1f9b790f1b173 5378" "$(sha256sum third_party/tree-sitter/verilog/src/tree_sitter/parser.h | cut -d' ' -f1) $(stat -c %s third_party/tree-sitter/verilog/src/tree_sitter/parser.h)"
eq "src/node-types.json sha256/bytes" "e99b42baf4279d3be6dec59129dda0789f196b4e5c99144a781ea6cdf255425c 271904" "$(sha256sum third_party/tree-sitter/verilog/src/node-types.json | cut -d' ' -f1) $(stat -c %s third_party/tree-sitter/verilog/src/node-types.json)"
eq "PROVENANCE pins the commit" "1" "$(grep -c '4457145e795b363f072463e697dfe2f6973c9a52' third_party/tree-sitter/PROVENANCE.md)"

echo "== D. folding-form facts stated by amendments-rev2.md §4.2-§4.5 =="
eq "no <folding> element in vedit-verilog.xml" "0" "$(grep -c '<folding' resources/syntax/vedit-verilog.xml)"
eq "no <folding> element in vedit-systemverilog.xml" "0" "$(grep -c '<folding' resources/syntax/vedit-systemverilog.xml)"
eq "verilog beginRegion/endRegion occurrences" "9 7" "$(grep -o 'beginRegion' resources/syntax/vedit-verilog.xml | wc -l | tr -d ' ') $(grep -o 'endRegion' resources/syntax/vedit-verilog.xml | wc -l | tr -d ' ')"
eq "SV beginRegion/endRegion occurrences" "14 11" "$(grep -o 'beginRegion' resources/syntax/vedit-systemverilog.xml | wc -l | tr -d ' ') $(grep -o 'endRegion' resources/syntax/vedit-systemverilog.xml | wc -l | tr -d ' ')"
eq "verilog keyword rules carrying beginRegion/endRegion" "14" "$(grep -c '<keyword[^>]*\(beginRegion\|endRegion\)' resources/syntax/vedit-verilog.xml)"
eq "SV keyword rules carrying beginRegion/endRegion" "23" "$(grep -c '<keyword[^>]*\(beginRegion\|endRegion\)' resources/syntax/vedit-systemverilog.xml)"
eq "verilog comment-region rules (Detect2Chars)" "2" "$(grep -c 'Detect2Chars.*Region' resources/syntax/vedit-verilog.xml)"
eq "SV comment-region rules (Detect2Chars)" "2" "$(grep -c 'Detect2Chars.*Region' resources/syntax/vedit-systemverilog.xml)"
# §4.3 and §4.7 claim the two files carry "14 + 14 条" region-bearing folding rules.
doc_count=$(grep -c '14 + 14' docs/handover/appendix/amendments-rev2.md)
if [ "$doc_count" -gt 0 ]; then
  note "F-1: amendments-rev2.md §4.3/§4.7 say the two XMLs carry '14 + 14 条' region rules; measured 14 (verilog) + 23 (SV); the same §4.3 also states '全文共 37 条 Unknown keyword list 警告' (14+23=37), and evidence 04 logs 23 warnings for the SV form"
else
  ok "amendments-rev2.md carries no stale '14 + 14' count"
fi

echo "== E. naming blacklist (C14 / verify-not-shipped.sh:53-57) =="
eq "no file named verilog.xml / systemverilog.xml under resources/syntax" "" "$(find resources/syntax -type f \( -name verilog.xml -o -name systemverilog.xml \) | tr '\n' ' ')"

echo "== F. write scope of the T33 change set =="
scope_bad=0
for p in $(git status --porcelain -- docs THIRD_PARTY_NOTICES.md evidence/docs-amendments | awk '{print $2}'); do
  case "$p" in
    docs/*|THIRD_PARTY_NOTICES.md|evidence/docs-amendments/*) ;;
    *) scope_bad=$((scope_bad+1)); echo "  OUT OF SCOPE: $p" ;;
  esac
done
eq "every T33-scope changed path is inside docs/ | THIRD_PARTY_NOTICES.md | evidence/docs-amendments/" "0" "$scope_bad"
eq "docs edits are pure additions (numstat, no deletions)" "55 0 41 0 1 0" "$(git diff --numstat -- THIRD_PARTY_NOTICES.md docs/decisions/r2-license.md docs/handover/README.md | awk '{print $1, $2}' | tr '\n' ' ' | sed 's/ *$//')"

echo "== G. registration and cross-document consistency =="
eq "docs/handover/README.md registers amendments-rev2.md" "1" "$(grep -c 'appendix/amendments-rev2.md' docs/handover/README.md)"
eq "THIRD_PARTY_NOTICES.md has a §9" "1" "$(grep -c '^## 9\. ' THIRD_PARTY_NOTICES.md)"
for h in 3d1ec41f0871c001f560ca58b3178bdfd7508ff5f25d095c15820d2feda43e49 dc6e0a4714762764b9386471bb09e2550c1a9cbcf2b8f67f1a1200f677726f9d 92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46 4457145e795b363f072463e697dfe2f6973c9a52; do
  eq "THIRD_PARTY_NOTICES.md registers ${h:0:12}…" "1" "$(grep -c "$h" THIRD_PARTY_NOTICES.md)"
  if [ "$(grep -c "$h" docs/handover/appendix/amendments-rev2.md)" -ge 1 ]; then
    ok "amendments-rev2.md registers ${h:0:12}…"
  else
    bad "amendments-rev2.md does not register ${h:0:12}…"
  fi
done
eq "docs/decisions/r2-license.md carries the §9 errata" "1" "$(grep -c '^## 9\. 勘误' docs/decisions/r2-license.md)"
if [ "$(sed -n '/^## 9\. 勘误/,$p' docs/decisions/r2-license.md | grep -c 'vedit-systemverilog.xml')" -ge 1 ]; then
  ok "ADR §9 errata names the actual artifact (vedit-systemverilog.xml)"
else
  bad "ADR §9 errata does not name vedit-systemverilog.xml"
fi

echo "== H. cited line references (spot-check of the documents' own pointers) =="
eq "verify-not-shipped.sh:27-28 are the KDE sha256 pins" "2" "$(sed -n '27,28p' packaging/runtime/verify-not-shipped.sh | grep -c 'KDE_')"
eq "verify-not-shipped.sh:53-57 are the basename blacklist" "1" "$(sed -n '53,57p' packaging/runtime/verify-not-shipped.sh | grep -c "for n in systemverilog.xml verilog.xml")"
eq "verify-not-shipped.sh:89-116 is the library-level scan" "1" "$(sed -n '89,116p' packaging/runtime/verify-not-shipped.sh | grep -c 'LIBRARY level')"
eq "assemble-appdir.sh:109-112 installs to usr/share/org.kde.syntax-highlighting/syntax" "1" "$(sed -n '109,112p' packaging/runtime/assemble-appdir.sh | grep -c 'usr/share/org.kde.syntax-highlighting/syntax')"
eq "AppRun:33 exports VEDIT_SYNTAX_DIR at that path" "1" "$(sed -n '33p' packaging/runtime/AppRun | grep -c 'VEDIT_SYNTAX_DIR=.*usr/share/org.kde.syntax-highlighting/syntax')"
eq "runtime-manifest:195-196 carry both repo hashes" "2" "$(sed -n '195,196p' evidence/m0-runtime/runtime-manifest.txt | grep -cE '3d1ec41f0871c001f560ca58b3178bdfd7508ff5f25d095c15820d2feda43e49|dc6e0a4714762764b9386471bb09e2550c1a9cbcf2b8f67f1a1200f677726f9d')"
eq "amendments-a1-a5.md still carries the literal A4.1 <folding> form (the thing §4.1 quotes)" "1" "$(grep -c 'folding begin="…" end="…"' docs/handover/appendix/amendments-a1-a5.md)"

echo
echo "== summary: pass=$pass fail=$fail findings=$findings =="
