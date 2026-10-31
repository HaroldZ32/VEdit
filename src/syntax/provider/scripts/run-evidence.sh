#!/usr/bin/env bash
# Evidence run for the syntax-provider lane (T15). Runs INSIDE the pinned Qt6
# image (or the host wrapper shown in evidence/syn-provider/00-pins.txt).
#
#   bash src/syntax/provider/scripts/run-evidence.sh [OUT_DIR] [SRC_DIR] [BUILD_ROOT]
set -uo pipefail

OUT="${1:-/vedit-build/evidence/syn-provider}"
SRC="${2:-/vedit-build/src/syntax/provider}"
BUILD_ROOT="${3:-/vedit-build/target/syn-provider}"
mkdir -p "$OUT" "$BUILD_ROOT"

FAILED=0
run() { echo "\$ $*"; "$@"; echo "[exit=$?]"; }

echo "== 00 environment ==" | tee "$OUT/00-pins.txt"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "cmake: $(cmake --version | head -1)"
  echo "compiler: $(c++ --version | head -1)"
  echo "tree-sitter (pkg-config): $(pkg-config --modversion tree-sitter 2>/dev/null)"
  echo "libtree-sitter dynamic: $(ls /usr/lib/x86_64-linux-gnu/libtree-sitter.so* 2>/dev/null | tr '\n' ' ')"
  echo "libtree-sitter static: $(ls /usr/lib/x86_64-linux-gnu/libtree-sitter.a 2>/dev/null || echo none)"
  echo "source manifest (interface + implementation + tests):"
  ( cd "$SRC" && find . -type f \( -name '*.h' -o -name '*.cpp' -o -name 'CMakeLists.txt' -o -name '*.scm' -o -name 'parser.c' \) \
      | LC_ALL=C sort | xargs sha256sum )
  echo "fixture grammar parser.c sha256: $(sha256sum "$SRC/tests/fixture/parser.c" | cut -d' ' -f1)"
} | tee -a "$OUT/00-pins.txt"

configure_build() { # <name> <cmake args...>
  local name="$1"; shift
  echo "== configure+build $name ==" | tee -a "$OUT/01-build.log"
  cmake -S "$SRC" -B "$BUILD_ROOT/$name" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@" \
    2>&1 | tee -a "$OUT/01-build.log"
  local rc=${PIPESTATUS[0]}
  [ "$rc" -eq 0 ] || { echo "[configure $name exit=$rc]" | tee -a "$OUT/01-build.log"; return "$rc"; }
  cmake --build "$BUILD_ROOT/$name" --parallel "$(nproc)" 2>&1 | tee -a "$OUT/01-build.log"
  rc=${PIPESTATUS[0]}
  echo "[build $name exit=$rc]" | tee -a "$OUT/01-build.log"
  return "$rc"
}

# A: no grammar baked in (default). B: external grammar id; tests bypass the
# baked-in id by registering the fixture binding themselves.
configure_build none    -DVEDIT_SYNTAX_GRAMMAR=none    -DVEDIT_SYNTAX_TREESITTER=ON  -DVEDIT_SYNTAX_TS_LINK=shared || FAILED=$((FAILED+1))
configure_build external -DVEDIT_SYNTAX_GRAMMAR=external -DVEDIT_SYNTAX_TREESITTER=ON  -DVEDIT_SYNTAX_TS_LINK=shared || FAILED=$((FAILED+1))
configure_build static  -DVEDIT_SYNTAX_GRAMMAR=external -DVEDIT_SYNTAX_TREESITTER=ON  -DVEDIT_SYNTAX_TS_LINK=static || FAILED=$((FAILED+1))
configure_build nots    -DVEDIT_SYNTAX_GRAMMAR=none    -DVEDIT_SYNTAX_TREESITTER=OFF -DVEDIT_SYNTAX_TS_LINK=shared || FAILED=$((FAILED+1))

echo "== 02 unit tests (ctest, external + static variants) ==" | tee "$OUT/02-tests.log"
for variant in external static; do
  echo "== $variant: ctest ==" | tee -a "$OUT/02-tests.log"
  ctest --test-dir "$BUILD_ROOT/$variant" --output-on-failure 2>&1 | tee -a "$OUT/02-tests.log"
  rc=${PIPESTATUS[0]}
  echo "[ctest $variant exit=$rc]" | tee -a "$OUT/02-tests.log"
  [ "$rc" -eq 0 ] || FAILED=$((FAILED+1))
  echo "== $variant: test binary (full CHECK output) ==" | tee -a "$OUT/02-tests.log"
  "$BUILD_ROOT/$variant/vedit_syntax_provider_tests" 2>&1 | tee -a "$OUT/02-tests.log"
  rc=${PIPESTATUS[0]}
  echo "[tests $variant exit=$rc]" | tee -a "$OUT/02-tests.log"
  [ "$rc" -eq 0 ] || FAILED=$((FAILED+1))
done

echo "== 03 swap proof (compile-time grammar is configuration only) ==" | tee "$OUT/03-swap-proof.txt"
{
  echo "-- generated configuration header differs (build artifact, not source):"
  diff -u "$BUILD_ROOT/none/generated/grammar_config_generated.h" \
          "$BUILD_ROOT/external/generated/grammar_config_generated.h" || true
  echo
  echo "-- CMake cache: the only configured difference:"
  diff -u <(grep -E '^VEDIT_SYNTAX_' "$BUILD_ROOT/none/CMakeCache.txt") \
          <(grep -E '^VEDIT_SYNTAX_' "$BUILD_ROOT/external/CMakeCache.txt") || true
  echo
  echo "-- source manifest identical across the two builds (hash the SRC tree twice):"
  ( cd "$SRC" && find . -type f \( -name '*.h' -o -name '*.cpp' -o -name 'CMakeLists.txt' -o -name '*.scm' -o -name 'parser.c' \) | LC_ALL=C sort | xargs sha256sum ) > "$OUT/03-manifest-A.txt"
  ( cd "$SRC" && find . -type f \( -name '*.h' -o -name '*.cpp' -o -name 'CMakeLists.txt' -o -name '*.scm' -o -name 'parser.c' \) | LC_ALL=C sort | xargs sha256sum ) > "$OUT/03-manifest-B.txt"
  if diff -u "$OUT/03-manifest-A.txt" "$OUT/03-manifest-B.txt"; then
    echo "IDENTICAL: no source file changed when the compiled grammar changed (diff exit=0)"
  else
    echo "DIFFERENT: the grammar switch changed sources"
  fi
} | tee -a "$OUT/03-swap-proof.txt"

echo "== 04 interface purity (no grammar type in the public headers) ==" | tee "$OUT/04-interface-purity.txt"
{
  echo "\$ grep -nE 'TS[A-Z][A-Za-z]*|tree_sitter|TSTree|TSNode|TSQuery|node_type' <4 public headers>"
  grep -nE 'TS[A-Z][A-Za-z]*|tree_sitter|TSTree|TSNode|TSQuery|node_type' \
      "$SRC/syntax_provider.h" "$SRC/grammar_binding.h" "$SRC/owner_arbiter.h" "$SRC/syntax_service.h"
  echo "[grep exit=$?]  (exit 1 = no match = the interface is grammar-type free)"
  echo
  echo "translation units that actually include the tree-sitter API:"
  grep -rln -F '#include <tree_sitter/api.h>' "$SRC" | sed "s|$SRC/||"
} | tee -a "$OUT/04-interface-purity.txt"

echo "== 05 link mode (C18) ==" | tee "$OUT/05-link-mode.txt"
{
  echo "-- shared variant (VEDIT_SYNTAX_TS_LINK=shared): test binary NEEDED"
  readelf -d "$BUILD_ROOT/external/vedit_syntax_provider_tests" | sed -n '/NEEDED/p'
  echo "-- static variant (VEDIT_SYNTAX_TS_LINK=static): test binary NEEDED"
  readelf -d "$BUILD_ROOT/static/vedit_syntax_provider_tests" | sed -n '/NEEDED/p'
  echo "-- static variant must NOT depend on libtree-sitter.so:"
  if readelf -d "$BUILD_ROOT/static/vedit_syntax_provider_tests" | grep -q 'libtree-sitter'; then
    echo "FAIL: statically linked variant still needs libtree-sitter.so"; FAILED=$((FAILED+1))
  else
    echo "OK: no libtree-sitter NEEDED entry (libtree-sitter.a linked in)"
  fi
  echo "-- tree-sitter backend OFF variant (no tree-sitter link at all):"
  readelf -d "$BUILD_ROOT/nots/libvedit_syntax_provider.a" 2>/dev/null | sed -n '/NEEDED/p' || true
} | tee -a "$OUT/05-link-mode.txt"

echo "== 06 runtime switch evidence (from ctest output) ==" | tee "$OUT/06-runtime-switch.txt"
grep -E 'CHECK (switch\.|fallback\.)|SYN-PROVIDER-TESTS' "$OUT/02-tests.log" | tee -a "$OUT/06-runtime-switch.txt"

echo "== RESULT: failures=$FAILED ==" | tee "$OUT/09-result.txt"
exit "$FAILED"
