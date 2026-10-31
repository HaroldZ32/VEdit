#!/usr/bin/env bash
# Reproducible identity/naming checks for AMENDMENTS rev 2 (C15 / board task T33).
#
#   bash evidence/docs-amendments/verify.sh
#
# Exit 0 only when every check passes. Covers the C14 artifact identities, the
# tree-sitter-verilog provenance, the AppDir install-path consistency, the
# THIRD_PARTY_NOTICES registration, and the AMENDMENTS rev 2 sections.
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$ROOT"

pass=0
fail=0
ok() { pass=$((pass + 1)); printf 'CHECK PASS  %s\n' "$*"; }
bad() { fail=$((fail + 1)); printf 'CHECK FAIL  %s\n' "$*"; }

check_sha() {
    local want=$1 path=$2 got
    got=$(sha256sum "$path" 2>/dev/null | cut -d' ' -f1)
    if [ "$got" = "$want" ]; then ok "$path sha256=$got"; else bad "$path sha256 got=$got want=$want"; fi
}

check_size() {
    local path=$1 want_lines=$2 want_bytes=$3 lines bytes
    lines=$(wc -l <"$path" | tr -d ' ')
    bytes=$(wc -c <"$path" | tr -d ' ')
    if [ "$lines" = "$want_lines" ] && [ "$bytes" = "$want_bytes" ]; then
        ok "$path lines=$lines bytes=$bytes (C14)"
    else
        bad "$path lines=$lines bytes=$bytes want lines=$want_lines bytes=$want_bytes"
    fi
}

echo "== AMENDMENTS rev 2 identity checks =="
echo "collected_at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "git_head: $(git rev-parse HEAD)"
echo

# --- 1. C14: the two own LGPL-2.1-or-later definitions -----------------------
check_sha 3d1ec41f0871c001f560ca58b3178bdfd7508ff5f25d095c15820d2feda43e49 resources/syntax/vedit-verilog.xml
check_sha dc6e0a4714762764b9386471bb09e2550c1a9cbcf2b8f67f1a1200f677726f9d resources/syntax/vedit-systemverilog.xml
check_size resources/syntax/vedit-verilog.xml 78 5172
check_size resources/syntax/vedit-systemverilog.xml 89 6276

for f in resources/syntax/vedit-verilog.xml resources/syntax/vedit-systemverilog.xml; do
    grep -q 'license="LGPL-2.1-or-later"' "$f" && ok "$f declares license=LGPL-2.1-or-later" ||
        bad "$f lacks license=LGPL-2.1-or-later"
    grep -q 'GtkSourceView 4.6.1' "$f" && ok "$f carries GtkSourceView 4.6.1 attribution" ||
        bad "$f lacks GtkSourceView 4.6.1 attribution"
done

# The shipped files must not use the prohibited KDE names.
for n in verilog.xml systemverilog.xml; do
    if [ -e "resources/syntax/$n" ]; then bad "resources/syntax/$n exists (prohibited name)"; else
        ok "resources/syntax has no file named $n"; fi
done

# --- 2. MIT tree-sitter-verilog provenance ----------------------------------
check_sha 92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46 third_party/tree-sitter/verilog/LICENSE
check_sha 323c121a3e550df0f970d46c544b3f02485e5e6d754acb730d6be7b8dea7dc4b third_party/tree-sitter/verilog/grammar.js
check_sha a15f78b82068daeb62f3b54c8ad0101aa936a2c921a57806fe8f37dcbf0bfff2 third_party/tree-sitter/verilog/src/parser.c
check_sha ab104936984904469572a4e868149f7a22fb2929347f837ae6a1f9b790f1b173 third_party/tree-sitter/verilog/src/tree_sitter/parser.h
check_sha e99b42baf4279d3be6dec59129dda0789f196b4e5c99144a781ea6cdf255425c third_party/tree-sitter/verilog/src/node-types.json
grep -q '4457145e795b363f072463e697dfe2f6973c9a52' third_party/tree-sitter/PROVENANCE.md &&
    ok "PROVENANCE.md pins commit 4457145e…" || bad "PROVENANCE.md lacks the pinned commit"
grep -q 'MIT' third_party/tree-sitter/verilog/LICENSE &&
    ok "grammar LICENSE is the MIT text" || bad "grammar LICENSE is not MIT"

# --- 3. AppDir install-path consistency (evidence/m0-runtime manifest) ------
for pair in \
    "dc6e0a4714762764b9386471bb09e2550c1a9cbcf2b8f67f1a1200f677726f9d usr/share/org.kde.syntax-highlighting/syntax/vedit-systemverilog.xml" \
    "3d1ec41f0871c001f560ca58b3178bdfd7508ff5f25d095c15820d2feda43e49 usr/share/org.kde.syntax-highlighting/syntax/vedit-verilog.xml"; do
    set -- $pair
    want=$1
    path=$2
    got=$(awk -v p="$path" '$3 == p { print $1 }' evidence/m0-runtime/runtime-manifest.txt | head -1)
    if [ "$got" = "$want" ]; then ok "runtime-manifest $path sha256=$got"; else
        bad "runtime-manifest $path sha256 got=${got:-<absent>} want=$want"; fi
done

# --- 4. Registration in THIRD_PARTY_NOTICES.md ------------------------------
for token in \
    3d1ec41f0871c001f560ca58b3178bdfd7508ff5f25d095c15820d2feda43e49 \
    dc6e0a4714762764b9386471bb09e2550c1a9cbcf2b8f67f1a1200f677726f9d \
    92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46 \
    4457145e795b363f072463e697dfe2f6973c9a52; do
    grep -q "$token" THIRD_PARTY_NOTICES.md && ok "THIRD_PARTY_NOTICES.md registers $token" ||
        bad "THIRD_PARTY_NOTICES.md misses $token"
done
grep -q '^## 9\.' THIRD_PARTY_NOTICES.md && ok "THIRD_PARTY_NOTICES.md has section 9" ||
    bad "THIRD_PARTY_NOTICES.md has no section 9"

# --- 5. AMENDMENTS rev 2 exists and covers the four required items ----------
rev2=docs/handover/appendix/amendments-rev2.md
for marker in '①' '②' '③' '④'; do
    grep -q "$marker" "$rev2" && ok "amendments-rev2.md covers $marker" ||
        bad "amendments-rev2.md lacks $marker"
done
grep -q 'A4.5' "$rev2" && ok "amendments-rev2.md states the A4.5 status" ||
    bad "amendments-rev2.md does not mention A4.5"
grep -q 'F2 交付库' "$rev2" && ok "amendments-rev2.md carries the F2 evidence rule" ||
    bad "amendments-rev2.md lacks the F2 evidence rule"
grep -q 'amendments-rev2.md' docs/handover/README.md &&
    ok "docs/handover/README.md registers amendments-rev2.md" ||
    bad "docs/handover/README.md does not register amendments-rev2.md"

# --- 6. ADR errata present --------------------------------------------------
grep -q '^## 9\. 勘误' docs/decisions/r2-license.md &&
    ok "r2-license.md carries the §9 errata" || bad "r2-license.md lacks the §9 errata"
grep -q 'vedit-systemverilog.xml' docs/decisions/r2-license.md &&
    ok "r2-license.md errata names the actual artifact" ||
    bad "r2-license.md errata does not name the actual artifact"

echo
echo "== summary: pass=$pass fail=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
