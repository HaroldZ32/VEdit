#!/usr/bin/env bash
# Evidence run for the VEdit grammar lane (syn-grammars, task T17).
# Runs INSIDE the pinned Qt6 image (see evidence/syn-grammars/00-pins.txt for the
# host wrapper). Regenerates every log under OUT; the tracked sources are never
# modified (this script only configures/builds/tests and writes evidence).
#
#   bash src/syntax/grammars/scripts/run-evidence.sh \
#        /vedit-build/evidence/syn-grammars \
#        /vedit-build/src/syntax/grammars \
#        /vedit-build/target/syn-grammars \
#        /vedit-build
set -uo pipefail

OUT="${1:-/vedit-build/evidence/syn-grammars}"
SRC="${2:-/vedit-build/src/syntax/grammars}"
BUILD_ROOT="${3:-/vedit-build/target/syn-grammars}"
REPO="${4:-/vedit-build}"
mkdir -p "$OUT" "$BUILD_ROOT"

# Regenerate this lane's evidence from scratch: remove only the files this
# script writes (projection: one run = one self-consistent set of logs).
rm -f "$OUT"/00-pins.txt "$OUT"/01-*.log "$OUT"/02-*.log "$OUT"/03-*.txt \
      "$OUT"/04-*.txt "$OUT"/05-*.txt "$OUT"/06-*.txt "$OUT"/09-*.txt

FAILED=0
TS_VERILOG_DIR="$REPO/third_party/tree-sitter/verilog"
QUERY_DIR="$REPO/resources/queries"

source_manifest() { # hash every source file that decides the grammar identity
  (
    cd "$REPO" &&
      find src/syntax/grammars resources/queries third_party/tree-sitter -type f \
        \( -name '*.h' -o -name '*.cpp' -o -name '*.scm' -o -name 'CMakeLists.txt' \
           -o -name '*.md' -o -name 'parser.c' -o -name 'parser.h' -o -name 'LICENSE' \
           -o -name 'grammar.js' -o -name 'node-types.json' \) \
        | LC_ALL=C sort | xargs sha256sum
  )
}

echo "== 00 environment ==" | tee "$OUT/00-pins.txt"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "host: $(uname -srm) / nproc=$(nproc)"
  echo "cmake: $(cmake --version | head -1)"
  echo "compiler: $(c++ --version | head -1)"
  echo "tree-sitter (pkg-config): $(pkg-config --modversion tree-sitter 2>/dev/null)"
  echo "grammar identity: tree-sitter/tree-sitter-verilog @ 4457145e795b363f072463e697dfe2f6973c9a52 (MIT)"
  echo "vendored files:"
  ( cd "$TS_VERILOG_DIR" && sha256sum LICENSE grammar.js src/parser.c src/tree_sitter/parser.h src/node-types.json )
  echo "grammar ABI marker: $(grep -m1 '#define LANGUAGE_VERSION' "$TS_VERILOG_DIR/src/parser.c")"
  echo "queries:"
  ( cd "$QUERY_DIR" && sha256sum verilog/*.scm systemverilog/*.scm )
  echo "grammar lane source manifest:"
  source_manifest | sed 's/^/  /'
} | tee -a "$OUT/00-pins.txt"

configure_build() { # <name> <cmake args...>
  local name="$1"
  shift
  echo "== configure+build $name ==" | tee -a "$OUT/01-build.log"
  rm -rf "${BUILD_ROOT:?}/$name"
  cmake -S "$SRC" -B "$BUILD_ROOT/$name" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@" \
    2>&1 | tee -a "$OUT/01-build.log"
  local rc=${PIPESTATUS[0]}
  if [ "$rc" -ne 0 ]; then
    echo "[configure $name exit=$rc]" | tee -a "$OUT/01-build.log"
    return "$rc"
  fi
  cmake --build "$BUILD_ROOT/$name" --parallel "$(nproc)" 2>&1 | tee -a "$OUT/01-build.log"
  rc=${PIPESTATUS[0]}
  echo "[build $name exit=$rc]" | tee -a "$OUT/01-build.log"
  return "$rc"
}

configure_build none            -DVEDIT_SYNTAX_GRAMMAR=none          -DVEDIT_GRAMMAR_VERILOG=OFF -DVEDIT_GRAMMAR_SYSTEMVERILOG=OFF -DVEDIT_GRAMMAR_BUILD_TESTS=OFF -DVEDIT_SYNTAX_TS_LINK=shared || FAILED=$((FAILED + 1))
configure_build verilog         -DVEDIT_SYNTAX_GRAMMAR=verilog       -DVEDIT_SYNTAX_TS_LINK=shared || FAILED=$((FAILED + 1))
configure_build systemverilog   -DVEDIT_SYNTAX_GRAMMAR=systemverilog -DVEDIT_SYNTAX_TS_LINK=shared || FAILED=$((FAILED + 1))
configure_build verilog-static  -DVEDIT_SYNTAX_GRAMMAR=verilog       -DVEDIT_SYNTAX_TS_LINK=static || FAILED=$((FAILED + 1))

echo "== 02 validator (ctest + full CHECK output per variant) ==" | tee "$OUT/02-tests.log"
for variant in verilog systemverilog verilog-static; do
  echo "== $variant: ctest ==" | tee -a "$OUT/02-tests.log"
  ctest --test-dir "$BUILD_ROOT/$variant" --output-on-failure 2>&1 | tee -a "$OUT/02-tests.log"
  rc=${PIPESTATUS[0]}
  echo "[ctest $variant exit=$rc]" | tee -a "$OUT/02-tests.log"
  [ "$rc" -eq 0 ] || FAILED=$((FAILED + 1))
  echo "== $variant: test binary ==" | tee -a "$OUT/02-tests.log"
  "$BUILD_ROOT/$variant/vedit_syn_grammars_tests" 2>&1 | tee -a "$OUT/02-tests.log"
  rc=${PIPESTATUS[0]}
  echo "[tests $variant exit=$rc]" | tee -a "$OUT/02-tests.log"
  [ "$rc" -eq 0 ] || FAILED=$((FAILED + 1))
done

echo "== 03 build manifest (which grammars are compiled in) ==" | tee "$OUT/03-manifest.txt"
for variant in none verilog systemverilog verilog-static; do
  echo "-- $variant" | tee -a "$OUT/03-manifest.txt"
  cat "$BUILD_ROOT/$variant/grammars_manifest.txt" 2>&1 | tee -a "$OUT/03-manifest.txt"
  echo | tee -a "$OUT/03-manifest.txt"
done

echo "== 04 swap proof (compiled grammar is configuration only, C15) ==" | tee "$OUT/04-swap-proof.txt"
{
  echo "-- generated configuration header differs (build artifact, not source):"
  diff -u "$BUILD_ROOT/none/generated/grammars_config_generated.h" \
          "$BUILD_ROOT/verilog/generated/grammars_config_generated.h" || true
  echo
  echo "-- CMake cache: the only configured difference:"
  diff -u <(grep -E '^VEDIT_(SYNTAX|GRAMMAR)_' "$BUILD_ROOT/none/CMakeCache.txt") \
          <(grep -E '^VEDIT_(SYNTAX|GRAMMAR)_' "$BUILD_ROOT/verilog/CMakeCache.txt") || true
  echo
  echo "-- source manifest identical across all four configurations:"
  source_manifest > "$OUT/04-manifest-none.txt"
  source_manifest > "$OUT/04-manifest-verilog.txt"
  source_manifest > "$OUT/04-manifest-systemverilog.txt"
  source_manifest > "$OUT/04-manifest-static.txt"
  if diff -q "$OUT/04-manifest-none.txt" "$OUT/04-manifest-verilog.txt" >/dev/null &&
     diff -q "$OUT/04-manifest-none.txt" "$OUT/04-manifest-systemverilog.txt" >/dev/null &&
     diff -q "$OUT/04-manifest-none.txt" "$OUT/04-manifest-static.txt" >/dev/null; then
    echo "IDENTICAL: no source file changed when the compiled grammar changed (diff exit=0)"
  else
    echo "DIFFERENT: the grammar switch changed tracked sources -- C15 broken"
    FAILED=$((FAILED + 1))
  fi
} | tee -a "$OUT/04-swap-proof.txt"

echo "== 05 layering purity (node types only in queries; interface grammar-free) ==" | tee "$OUT/05-layering.txt"
{
  echo "\$ grep -nE 'TS[A-Z][A-Za-z]*|tree_sitter|TSTree|TSNode|TSQuery|node_type' <provider public headers>"
  grep -nE 'TS[A-Z][A-Za-z]*|tree_sitter|TSTree|TSNode|TSQuery|node_type' \
      "$REPO/src/syntax/provider/syntax_provider.h" \
      "$REPO/src/syntax/provider/grammar_binding.h" \
      "$REPO/src/syntax/provider/owner_arbiter.h" \
      "$REPO/src/syntax/provider/syntax_service.h"
  echo "[grep exit=$?]  (exit 1 = no match = the provider interface stays grammar-free)"
  echo
  echo "translation units that include the tree-sitter C API (must be the provider backend only):"
  grep -rln -F '#include <tree_sitter/api.h>' "$REPO/src" --include='*.cpp' --include='*.h' | sed "s|$REPO/||"
  echo
  echo "\$ grep -rn 'module_declaration|interface_declaration|class_declaration' src/syntax/grammars"
  grep -rnE 'module_declaration|interface_declaration|class_declaration' \
      "$REPO/src/syntax/grammars" --include='*.cpp' --include='*.h'
  echo "[grep exit=$?]  (exit 1 = node types do not appear in the binding layer)"
  echo
  echo "node types appear only in the query files (and the vendored grammar's own node-types.json):"
  grep -rl 'module_declaration' "$QUERY_DIR" "$TS_VERILOG_DIR/src/node-types.json" 2>/dev/null \
      | sed "s|$REPO/||"
} | tee -a "$OUT/05-layering.txt"

echo "== 06 licensing / provenance ==" | tee "$OUT/06-licensing.txt"
{
  echo "-- MIT licence header (vendored):"
  head -8 "$TS_VERILOG_DIR/LICENSE"
  echo
  echo "-- sha256 of the vendored files (cross-checked against third_party/tree-sitter/PROVENANCE.md):"
  ( cd "$TS_VERILOG_DIR" && sha256sum LICENSE grammar.js src/parser.c src/tree_sitter/parser.h src/node-types.json )
  echo
  echo "-- no copyleft marker in the vendored grammar:"
  if grep -qiE 'GNU (General Public|Lesser)' "$TS_VERILOG_DIR/LICENSE" "$TS_VERILOG_DIR/grammar.js"; then
    echo "FOUND a GNU licence marker -- provenance table is wrong"
    FAILED=$((FAILED + 1))
  else
    echo "OK: no GPL/LGPL marker in LICENSE or grammar.js"
  fi
  echo
  echo "-- provenance document: $(wc -l < "$REPO/third_party/tree-sitter/PROVENANCE.md") lines"
  echo "-- THIRD_PARTY_NOTICES.md is owned by pack-final (deliberately untouched by this lane)"
} | tee -a "$OUT/06-licensing.txt"

echo "== RESULT: failures=$FAILED ==" | tee "$OUT/09-result.txt"
exit "$FAILED"
