#!/usr/bin/env bash
# Verifies every pinned upstream artifact cited by evidence/r2-license/report.md.
#
# Needs network (GitHub API / raw.githubusercontent.com). It downloads only small
# text artifacts into a temp dir and never writes into the repository, so the
# "no grammar source in the delivery tree" boundary of the task holds.
#
# Usage: bash evidence/r2-license/verify.sh
# Exit: 0 = every assertion holds, 1 = at least one assertion failed.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
failures=()

ok() { pass=$((pass + 1)); printf 'PASS  %s\n' "$*"; }
bad() { fail=$((fail + 1)); failures+=("$*"); printf 'FAIL  %s\n' "$*"; }

# fetch <url> -> $F (temp path); retries transient network errors (a flaky fetch
# is not an assertion failure and must not turn the run red)
F=""
fetch() {
  local url=$1 out code attempt
  out="$TMP/$(printf '%s' "$url" | sha256sum | cut -c1-16)"
  for attempt in 1 2 3 4; do
    code=$(curl -sSL --max-time 60 --retry 2 --retry-delay 1 \
      -o "$out" -w '%{http_code}' "$url" 2>/dev/null) || code=000
    if [ "$code" = "200" ]; then
      F=$out
      return 0
    fi
    sleep 2
  done
  bad "curl/http=$code $url (after 4 attempts)"
  return 1
}

check_sha() { # <url> <sha256> <label>
  local got
  fetch "$1" || return 0
  got=$(sha256sum "$F" | cut -d' ' -f1)
  if [ "$got" = "$2" ]; then ok "sha256 $3"; else bad "sha256 $3 want=$2 got=$got"; fi
}

check_has() { # <url> <literal> <label>
  fetch "$1" || return 0
  if grep -qF -- "$2" "$F"; then ok "has $3"; else bad "missing $3 : $2"; fi
}

check_absent() { # <url> <literal> <label>
  fetch "$1" || return 0
  if grep -qF -- "$2" "$F"; then bad "unexpected $3 : $2"; else ok "absent $3"; fi
}

check_re() { # <url> <extended-regex> <label>
  fetch "$1" || return 0
  if grep -qE -- "$2" "$F"; then ok "matches $3"; else bad "no match $3 : $2"; fi
}

check_absent_i() { # <url> <literal> <label>  (case-insensitive absence)
  fetch "$1" || return 0
  if grep -qiF -- "$2" "$F"; then bad "unexpected (ci) $3 : $2"; else ok "absent (ci) $3"; fi
}

check_count() { # <url> <extended-regex> <expected-count> <label>
  fetch "$1" || return 0
  local n
  n=$(grep -cE -- "$2" "$F")
  if [ "$n" = "$3" ]; then ok "count=$3 $4"; else bad "count $4 want=$3 got=$n"; fi
}

L=https://raw.githubusercontent.com/lapce/lapce/b012cef466741528f338879fa708355217fc40bd
G=https://raw.githubusercontent.com/lapce/tree-sitter-grammars/1f9f9dd5ce93e29992bb50bff0b0aa99e664154b
H=https://raw.githubusercontent.com/helix-editor/helix/0a4432b104099534f7a25b8ea4148234db146ab6
V=https://raw.githubusercontent.com/tree-sitter/tree-sitter-verilog/4457145e795b363f072463e697dfe2f6973c9a52
S=https://raw.githubusercontent.com/gmlarumbe/tree-sitter-systemverilog/08c5e379b9b6022d4c433a9249e6d54dedd051ba
K=https://raw.githubusercontent.com/KDE/syntax-highlighting/2d07614ca2ba8565915cc7e0959fd44745702ca1

echo "== E1 tag peel: v0.4.6 -> commit =="
peeled=$(git ls-remote https://github.com/lapce/lapce 'refs/tags/v0.4.6^{}' 2>/dev/null | cut -f1)
if [ "$peeled" = "b012cef466741528f338879fa708355217fc40bd" ]; then
  ok "git ls-remote v0.4.6^{} = $peeled"
else
  bad "v0.4.6^{} want=b012cef466741528f338879fa708355217fc40bd got=${peeled:-<none>}"
fi

echo "== E3/E4/E5 Lapce v0.4.6 static reading =="
check_sha "$L/Cargo.lock" ab95631c8b2267175f604eeac7f854fed6c14d1a214d0713bc28a2a9ce2b5c4b "Cargo.lock"
check_count "$L/Cargo.lock" '^name = "tree-sitter' 1 "exactly one tree-sitter package in Cargo.lock"
check_absent "$L/Cargo.lock" 'name = "tree-sitter-verilog"' "no tree-sitter-verilog grammar crate in Cargo.lock"
check_absent "$L/Cargo.lock" 'tree-sitter-systemverilog' "no tree-sitter-systemverilog crate in Cargo.lock"
check_sha "$L/lapce-core/src/language.rs" 651cda912938c3002329bf2e65b346239d7dc8ab9603401c803c0824c3959ebe "language.rs"
check_sha "$L/lapce-app/src/app/grammars.rs" 4ea6ce659f71cc41b677af63c72dbac80fbf3ab2088801495238fbca33553140 "grammars.rs"
check_has "$L/lapce-core/src/language.rs" "        grammar: None," "language.rs TreeSitterProperties::DEFAULT grammar: None"
check_has "$L/lapce-core/src/language.rs" "        tree_sitter: TreeSitterProperties::DEFAULT," "language.rs Verilog uses DEFAULT"
check_has "$L/lapce-app/src/app/grammars.rs" "https://api.github.com/repos/lapce/tree-sitter-grammars/releases?per_page=100" "grammars.rs release endpoint"
check_has "$L/lapce-app/src/app/grammars.rs" "cmp_precedence(&version), Ordering::Equal" "grammars.rs exact-version match"
check_has "$L/lapce-app/src/app/grammars.rs" 'let file_name = format!("grammars-{}-{}"' "grammars.rs prebuilt asset name"
check_re "$L/Cargo.toml" '^version[[:space:]]*=[[:space:]]*"0\.4\.6"' "workspace version 0.4.6"

echo "== E6 no 0.4.6 grammar release =="
check_absent "https://api.github.com/repos/lapce/tree-sitter-grammars/tags?per_page=100" '"name":"v0.4.6"' "tree-sitter-grammars tag v0.4.6 (as of 2026-10-31)"
check_absent "https://api.github.com/repos/lapce/tree-sitter-grammars/releases?per_page=30" '"tag_name":"v0.4.6"' "tree-sitter-grammars release v0.4.6 (as of 2026-10-31)"

echo "== E7/E8 grammar build pipeline =="
check_sha "$G/src/main.rs" 4afbff5080eeeeeb8eb65b35ed1987aa22ec4b877262168ba6138b5a0e8f8f17 "tree-sitter-grammars main.rs"
check_has "$G/src/main.rs" '"0a4432b104099534f7a25b8ea4148234db146ab6"' "main.rs helix pin"
check_has "$G/src/main.rs" 'helix_dir.join("languages.toml")' "main.rs reads helix languages.toml"
check_sha "$H/languages.toml" 600842bec59d9cb1622ab46c2a6d7b2ca80f6de44f24f124900ce850abd72ef7 "helix languages.toml"
check_has "$H/languages.toml" 'file-types = ["v", "vh", "sv", "svh"]' "helix verilog file-types"
check_has "$H/languages.toml" 'rev = "4457145e795b363f072463e697dfe2f6973c9a52"' "helix verilog grammar rev"
check_absent_i "$H/languages.toml" "systemverilog" "no systemverilog string anywhere in helix languages.toml"

echo "== E9 grammar identity + license =="
check_sha "$V/LICENSE" 92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46 "ts-verilog LICENSE"
check_has "$V/LICENSE" "MIT License" "LICENSE is MIT"
check_has "$V/LICENSE" "Copyright (c) 2018-2021 Aliaksei Chapyzhenka" "LICENSE copyright line"
check_has "$V/LICENSE" "The above copyright notice and this permission notice shall be included in all" "MIT notice obligation"
check_sha "$V/Cargo.toml" 721c4a33e1c4d298f90723db07abdfece112964d2ce52bbe0f327c502b93756c "ts-verilog Cargo.toml"
check_has "$V/Cargo.toml" 'license = "MIT"' "Cargo.toml license = MIT"
check_sha "$V/package.json" 4e5298fe9a340e8498af31f17865c060559e6207ff113dcf94497142b1837b0b "ts-verilog package.json"
check_has "$V/package.json" '"license": "MIT"' "package.json license MIT"
check_sha "$V/grammar.js" 323c121a3e550df0f970d46c544b3f02485e5e6d754acb730d6be7b8dea7dc4b "ts-verilog grammar.js"

echo "== E11 MIT alternatives =="
check_sha "$S/LICENSE" f4ceaa91e73e623d6b30895a3b644e0e277754a81c8b28655b75d7a9113f1ac4 "gmlarumbe LICENSE"
check_has "$S/LICENSE" "Copyright (c) 2024-2025 Gonzalo M. Larumbe" "gmlarumbe copyright line"
check_sha "$S/Cargo.toml" b446910e2866a92f79a4fccf85897fe91559c274dd9ed50d92c0b61ebd94db52 "gmlarumbe Cargo.toml"
check_has "$S/Cargo.toml" 'license = "MIT"' "gmlarumbe license = MIT"
check_sha "$S/grammar.js" d844947bf6b95e7c4b5703436b1bc8174d3b7f0a9f72cfbcc2d2da8f8f540289 "gmlarumbe grammar.js"

echo "== E13 KDE definitions =="
check_sha "$K/data/syntax/systemverilog.xml" 3361d8b4ef62c4868165ef045c02d8b6e85a1e57f61c304b399661bdc06be015 "KDE systemverilog.xml"
check_has "$K/data/syntax/systemverilog.xml" 'license="GPLv3+"' "systemverilog.xml license=GPLv3+"
check_has "$K/data/syntax/systemverilog.xml" "either version 3 of the License" "systemverilog.xml GPLv3-or-later boilerplate"
check_sha "$K/data/syntax/verilog.xml" 7199e3677b0a942e65b40f95cb2481be062b1ca48eb048519e7d4efc098f5970 "KDE verilog.xml"
check_has "$K/data/syntax/verilog.xml" 'license=""' "verilog.xml license is EMPTY (undeclared)"
check_sha "$K/LICENSES/MIT.txt" 8f25018489d6fe0dec34a352314c38dc146247b7de65735790f4398a92afa84b "KDE LICENSES/MIT.txt"
check_has "$K/README.md" "Contributions to KSyntaxHighlighting shall be licensed under" "KDE contribution policy (MIT)"

echo "== E15 helix query license =="
check_sha "$H/LICENSE" 1f256ecad192880510e84ad60474eab7589218784b9a50bc7ceee34c2b91f1d5 "helix LICENSE"
check_has "$H/LICENSE" "Mozilla Public License Version 2.0" "helix LICENSE is MPL-2.0"

echo "== E12 local LGPL asset pins (no network) =="
if [ "$(find "$ROOT/assets/language-specs" -maxdepth 1 -type f | wc -l)" = "1" ] \
   && [ -s "$ROOT/assets/language-specs/README.md" ]; then
  ok "assets/language-specs holds only README.md"
else
  bad "assets/language-specs layout changed (expected exactly README.md)"
fi
if grep -q 'GSV_SHA256="63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874"' "$ROOT/scripts/versions.env"; then
  ok "GtkSourceView 4.6.1 tarball sha256 pinned in scripts/versions.env"
else
  bad "scripts/versions.env missing the GtkSourceView 4.6.1 sha256 pin"
fi
if grep -q '9d1f67411f2674d93f035620ce004da4beca5bbf469eb9955a83c332edb79b04' "$ROOT/evidence/highlight/03-langspec-provenance.txt"; then
  ok "systemverilog.lang sha256 recorded in evidence/highlight/03-langspec-provenance.txt"
else
  bad "systemverilog.lang sha256 missing from evidence/highlight/03-langspec-provenance.txt"
fi

echo "== negative controls (prove the new assertions can actually fail) =="
ctl="$TMP/ctl-grammar.lock"
cat > "$ctl" <<'FIXTURE'
[[package]]
name = "tree-sitter"
[[package]]
name = "tree-sitter-verilog"
FIXTURE
nc=$(grep -cE '^name = "tree-sitter' "$ctl")
if [ "$nc" = "2" ]; then
  ok "negative control: count detector returns 2 on a 2-grammar fixture (so want=1 is a real constraint)"
else
  bad "negative control broken: count detector returned $nc"
fi
if grep -qF 'name = "tree-sitter-verilog"' "$ctl"; then
  ok "negative control: absence detector fires when the grammar crate IS present"
else
  bad "negative control broken: absence detector missed a present grammar crate"
fi
ctl2="$TMP/ctl-clean.lock"
printf '[[package]]\nname = "tree-sitter"\n' > "$ctl2"
if grep -qF 'name = "tree-sitter-verilog"' "$ctl2"; then
  bad "negative control broken: absence detector flagged a clean fixture"
else
  ok "negative control: absence detector stays quiet on a clean fixture"
fi

echo
printf '== summary: %d passed, %d failed ==\n' "$pass" "$fail"
if [ "$fail" -ne 0 ]; then
  printf 'failed assertions:\n'
  printf '  - %s\n' "${failures[@]}"
  exit 1
fi
echo "ALL ASSERTIONS PASSED"
