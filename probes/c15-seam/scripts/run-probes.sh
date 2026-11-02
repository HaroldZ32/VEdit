#!/usr/bin/env bash
# Run the C15 seam probe inside the probe image and collect evidence.
#
# Usage (from the repository root):
#   docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp \
#       -v "$PWD":/work -w /work/probes/c15-seam \
#       vprobe-qt6kf6:1 bash scripts/run-probes.sh /work/evidence/r2-c15
#
# The script fails loudly (non-zero exit) on the first assertion failure.
set -uo pipefail

OUT="${1:-$PWD/../../evidence/r2-c15}"
mkdir -p "$OUT"
# Regenerate evidence from scratch: remove only files this probe writes.
rm -f "$OUT"/00-pins.txt "$OUT"/01-*.log "$OUT"/02*.log "$OUT"/02*.txt "$OUT"/03-*.txt \
      "$OUT"/04-*.txt "$OUT"/05-*.txt "$OUT"/06-*.txt "$OUT"/09-*.txt "$OUT"/*.png

record() {  # record <file> <command...>
  local log="$OUT/$1"; shift
  {
    echo "\$ $*"
    "$@" 2>&1
    echo "[exit=$?]"
  } | tee -a "$log"
}

echo "== 00 environment ==" | tee "$OUT/00-pins.txt"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "image tag: vprobe-qt6kf6:1 (image id captured host-side, see 00-image-id.txt)"
  echo "cmake: $(cmake --version | head -1)"
  echo "compiler: $(c++ --version | head -1)"
  echo "tree-sitter (pkg-config): $(pkg-config --modversion tree-sitter 2>/dev/null || echo 'n/a')"
  echo "libtree-sitter dynamic: $(ls /usr/lib/x86_64-linux-gnu/libtree-sitter.so* 2>/dev/null | tr '\n' ' ')"
  echo "libtree-sitter static: $(ls /usr/lib/x86_64-linux-gnu/libtree-sitter.a 2>/dev/null || echo 'none')"
  echo "pinned grammar parser.c sha256:"
  sha256sum grammars/verilog_placeholder/src/parser.c third_party/tree-sitter-json/src/parser.c
  echo "upstream commit tree-sitter-json: 254c42a6476413b776221e03982ac8ae159eeb72"
  echo "tree-sitter CLI used to generate verilog_placeholder: 0.27.1"
  echo "interface grammar-type leak check (must be empty):"
  grep -nE 'TS[A-Z][A-Za-z]*|tree_sitter|node_type|TSTree|TSNode|TSQuery' src/syntax_provider.hpp src/main.cpp || echo "  no grammar type appears in the interface or the caller"
  echo "interface/consumer file list:"
  sha256sum src/syntax_provider.hpp src/main.cpp src/grammar_registry.cpp
} | tee -a "$OUT/00-pins.txt"

configure_and_build() {  # <name> <extra cmake args...>
  local name="$1"; shift
  echo "== configure+build $name ==" | tee -a "$OUT/01-build.log"
  cmake -S . -B "build/$name" -G Ninja "$@" 2>&1 | tee -a "$OUT/01-build.log"
  local rc=${PIPESTATUS[0]}
  [ "$rc" -eq 0 ] || { echo "configure failed for $name (exit=$rc)"; return "$rc"; }
  cmake --build "build/$name" 2>&1 | tee -a "$OUT/01-build.log"
  rc=${PIPESTATUS[0]}
  echo "[build $name exit=$rc]" | tee -a "$OUT/01-build.log"
  return "$rc"
}

FAILURES=0

# ---------- Build A: Verilog-family grammar ----------
configure_and_build verilog -DSEAM_GRAMMAR=verilog_placeholder -DSEAM_TREESITTER=ON -DSEAM_TS_LINK=shared || FAILURES=$((FAILURES+1))

# ---------- Build B: non-Verilog grammar (tree-sitter-json) ----------
configure_and_build json -DSEAM_GRAMMAR=json -DSEAM_TREESITTER=ON -DSEAM_TS_LINK=shared || FAILURES=$((FAILURES+1))

# ---------- Build C: tree-sitter not compiled in ----------
configure_and_build nots -DSEAM_GRAMMAR=json -DSEAM_TREESITTER=OFF || FAILURES=$((FAILURES+1))

# ---------- Build D: static libtree-sitter ----------
configure_and_build static -DSEAM_GRAMMAR=json -DSEAM_TREESITTER=ON -DSEAM_TS_LINK=static || FAILURES=$((FAILURES+1))

# ---------- Run A and B ----------
for pair in "verilog tests/sample.v" "json tests/sample.json"; do
  set -- $pair
  echo "== run build/$1 ==" | tee -a "$OUT/02-run-both-grammars.txt"
  "./build/$1/c15-seam" --tree-sitter=on --file "$2" 2>&1 | tee -a "$OUT/02-run-both-grammars.txt"
  rc=${PIPESTATUS[0]}
  echo "[run $1 exit=$rc]" | tee -a "$OUT/02-run-both-grammars.txt"
  [ "$rc" -eq 0 ] || FAILURES=$((FAILURES+1))
done

# ---------- Runtime switch OFF (same binary that ran with tree-sitter ON) ----------
for b in verilog json; do
  echo "== run build/$b --tree-sitter=off ==" | tee -a "$OUT/03-runtime-switch.txt"
  "./build/$b/c15-seam" --tree-sitter=off --file tests/sample.json 2>&1 | tee -a "$OUT/03-runtime-switch.txt"
  rc=${PIPESTATUS[0]}
  echo "[run $b off exit=$rc]" | tee -a "$OUT/03-runtime-switch.txt"
  [ "$rc" -eq 0 ] || FAILURES=$((FAILURES+1))
done

# ---------- Build C run ----------
echo "== run build/nots (tree-sitter not compiled in) ==" | tee -a "$OUT/03-runtime-switch.txt"
"./build/nots/c15-seam" --tree-sitter=on --file tests/sample.json 2>&1 | tee -a "$OUT/03-runtime-switch.txt"
rc=${PIPESTATUS[0]}
echo "[run nots exit=$rc]" | tee -a "$OUT/03-runtime-switch.txt"
[ "$rc" -eq 0 ] || FAILURES=$((FAILURES+1))

# ---------- Source manifests: identical across grammar builds? ----------
manifest() {
  find src grammars third_party tests -type f \
    ! -path '*/src/grammar.json' ! -path '*/src/node-types.json' \
    | LC_ALL=C sort | xargs sha256sum | sed 's|  |  |'
}
manifest > "$OUT/04-source-manifest-A.txt"
manifest > "$OUT/04-source-manifest-B.txt"
{
  echo "== diff of source manifests between the two grammar builds =="
  if diff -u "$OUT/04-source-manifest-A.txt" "$OUT/04-source-manifest-B.txt"; then
    echo "IDENTICAL: every source file hashed the same before/after the grammar switch"
    echo "[diff exit=0]"
  else
    echo "DIFFERENT: grammar switch changed source files"
    echo "[diff exit=1]"
  fi
  echo
  echo "== files that differ between the two build trees (build outputs only) =="
  diff -rq build/verilog build/json 2>/dev/null | sed "s|build/verilog|A|;s|build/json|B|" | head -30
  echo
  echo "== CMake cache: the only configured difference =="
  diff -u <(grep -E 'SEAM_' build/verilog/CMakeCache.txt) <(grep -E 'SEAM_' build/json/CMakeCache.txt)
} | tee "$OUT/04-swap-proof.txt"

# ---------- Which grammar symbol is actually linked? ----------
{
  echo "== nm: grammar symbols linked into each binary =="
  for b in verilog json; do
    echo "-- build/$b --"
    nm "build/$b/c15-seam" 2>/dev/null | grep -o 'tree_sitter_[a-z_]*' | LC_ALL=C sort -u
  done
} | tee "$OUT/05-grammar-manifest.txt"

# ---------- Link mode evidence ----------
{
  echo "== dynamic build (SEAM_TS_LINK=shared) =="
  ldd build/json/c15-seam | grep -E 'tree-sitter|not found' || echo "(no libtree-sitter line)"
  echo "[ldd exit=$?]"
  echo
  echo "== static build (SEAM_TS_LINK=static) =="
  ldd build/static/c15-seam | grep -E 'tree-sitter|not found' || echo "(no libtree-sitter line: statically linked)"
  echo "[ldd exit=$?]"
  echo
  echo "== ELF dynamic dependency listing =="
  for b in json static; do
    echo "-- build/$b --"
    readelf -d "build/$b/c15-seam" | grep -E 'NEEDED' | sed 's/^ *//'
  done
} | tee "$OUT/06-link-mode.txt"

# ---------- Static build run ----------
echo "== run build/static (libtree-sitter.a linked in) ==" | tee -a "$OUT/06-link-mode.txt"
"./build/static/c15-seam" --tree-sitter=on --file tests/sample.json 2>&1 | tee -a "$OUT/06-link-mode.txt"
rc=${PIPESTATUS[0]}
echo "[run static exit=$rc]" | tee -a "$OUT/06-link-mode.txt"
[ "$rc" -eq 0 ] || FAILURES=$((FAILURES+1))

echo
echo "== RESULT: failures=$FAILURES ==" | tee "$OUT/09-result.txt"
exit "$FAILURES"
