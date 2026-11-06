#!/usr/bin/env bash
# ADDENDUM A-12 (+ T44 nested variant): regenerate the scale fixtures, build the
# measurement harness and record every number — expansion, real-lint pass, UI
# tick, cancel latency, over-scale warn/cancel, and the nested-depth AST proof.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
EV="$ROOT/evidence/w4-lint/scale"
LOGS="$EV/logs"
mkdir -p "$LOGS" "$ROOT/target/tmp"

python3 "$EV/generate.py" --out "$ROOT/target/tmp/scale-flat-1k" --files 1000 --variant flat
python3 "$EV/generate.py" --out "$ROOT/target/tmp/scale-nested-1k" --files 1000 --variant nested
python3 "$EV/generate.py" --out "$ROOT/target/tmp/scale-2k" --files 2000 --variant flat

docker run --rm -v "$ROOT":/vedit-build vedit-qt6-build:local bash -lc '
  set -euo pipefail
  cd /vedit-build
  mkdir -p target/tmp/scale target/tmp/scale-baseline /tmp/xdg && chmod 700 /tmp/xdg
  printf "module tiny; endmodule\n" > target/tmp/scale-baseline/tiny.sv
  g++ -std=c++20 -O2 -fPIC -I src/workspace/diagnostics -I crates/vd-ffi/include \
      $(pkg-config --cflags Qt6Core) evidence/w4-lint/scale/scale_harness.cpp \
      target/qt6-build/src/app/workspace-lane-diagnostics/libvedit_workspace_diagnostics.a \
      target/release/libvd_ffi.a $(pkg-config --libs Qt6Core) -lpthread -ldl -lm \
      -o target/tmp/scale/scale_harness
  export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg
  BIN=target/qt6-build/src/app/workspace-lane-project/vedit-workspace-project-selftest
  H=target/tmp/scale/scale_harness
  M="python3 evidence/w4-lint/scale/measure.py --"
  SLANG=/vedit-build/build/slang/out/slang
  echo "slang: $($SLANG --version)"
  echo "=== expansion, flat vs nested 1k ==="
  $M $BIN --expansion target/tmp/scale-flat-1k SCALE_ROOT=/vedit-build/target/tmp/scale-flat-1k | grep -E "MEASURE|EXPANSION"
  $M $BIN --expansion target/tmp/scale-nested-1k SCALE_ROOT=/vedit-build/target/tmp/scale-nested-1k | grep -E "MEASURE|EXPANSION"
  echo "=== nested AST proof: depth distribution + parameter propagation (one file) ==="
  $SLANG --ast-json /vedit-build/evidence/w4-lint/scale/logs/ast-nested-one-file.json \
      --top l0_m_0_0 target/tmp/scale-nested-1k/rtl/d0/m_0_0.sv >/dev/null
  python3 - <<"PY"
import json
ast = json.load(open("evidence/w4-lint/scale/logs/ast-nested-one-file.json"))
instances = []
def walk(node, depth=0):
    if isinstance(node, dict):
        if node.get("kind") == "Instance":
            instances.append((depth, node.get("name")))
            walk(node.get("body") or {}, depth + 1)
        else:
            for value in node.values():
                walk(value, depth)
    elif isinstance(node, list):
        for value in node:
            walk(value, depth)
walk(ast)
from collections import Counter
print("AST instances:", len(instances), "depth-distribution:",
      dict(sorted(Counter(d for d, _ in instances).items())))
PY
  echo "=== full pass, flat vs nested 1k (real slang), 3 runs each ==="
  for run in 1 2 3; do
    for variant in flat nested; do
      $M $H --root /vedit-build/target/tmp/scale-$variant-1k --slang $SLANG --mode full --tick-ms 10 \
        | grep -E "RESULT|MEASURE" | sed "s/^/run=$run variant=$variant /"
    done
  done
  echo "=== cancel mid-pass, nested 1k ==="
  $M $H --root /vedit-build/target/tmp/scale-nested-1k --slang $SLANG --mode full --tick-ms 10 --cancel-after-ms 500 \
    | grep -E "RESULT|CANCEL|AFTER-CANCEL|CHECK|LINT-SCALE|MEASURE"
  echo "=== over-scale 2k: warn + cancel ==="
  $M $H --root /vedit-build/target/tmp/scale-2k --slang $SLANG --mode overscale --tick-ms 10 --cancel-after-ms 300 \
    | grep -E "START|RESULT|CANCEL|AFTER-CANCEL|CHECK|LINT-SCALE|MEASURE"
  echo "=== full 2k pass ==="
  $M $H --root /vedit-build/target/tmp/scale-2k --slang $SLANG --mode full --tick-ms 10 \
    | grep -E "RESULT|CHECK|LINT-SCALE|MEASURE"
  echo "=== ELABORATION at scale (AC-3.5, T7 C ABI, real slang) ==="
  {
    echo "=== elaboration at scale (AC-3.5): shipped C ABI vd_hier_start/vd_hier_poll, real slang ==="
    for variant in flat nested; do
      if [ "$variant" = nested ]; then top=l0_m_0_0; else top=m_0_0; fi
      echo "--- $variant 1k, one hierarchy (--top $top) ---"
      $M $H --root /vedit-build/target/tmp/scale-$variant-1k --slang $SLANG --mode elaborate --top $top \
        | grep -E "ELABORATE|MEASURE"
      echo "--- $variant 1k, whole project (no top) ---"
      $M $H --root /vedit-build/target/tmp/scale-$variant-1k --slang $SLANG --mode elaborate \
        | grep -E "ELABORATE|MEASURE"
    done
    echo "--- nested scaling curve, whole project (no top) ---"
    for limit in 100 250 500; do
      echo "files=$limit"
      $M $H --root /vedit-build/target/tmp/scale-nested-1k --slang $SLANG --mode elaborate --limit $limit \
        | grep -E "ELABORATE|MEASURE"
    done
    echo "--- ground truth: Instance nodes in the whole-project AST (both variants) ---"
    for variant in flat nested; do
      $SLANG --ast-json /tmp/scale-$variant-1k-ast.json --ast-json-source-info \
        -I target/tmp/scale-$variant-1k/inc target/tmp/scale-$variant-1k/rtl/d*/m_*.sv >/dev/null 2>&1 || true
      echo -n "$variant "
      python3 evidence/w4-lint/scale/ast_count.py /tmp/scale-$variant-1k-ast.json
    done
  } | tee /vedit-build/evidence/w4-lint/scale/logs/50-elaboration.log
  echo "=== one slang child (per-file peak RSS) ==="
  $M $SLANG --lint-only -I target/tmp/scale-nested-1k/inc -DSCALE=1 target/tmp/scale-nested-1k/rtl/d0/m_0_0.sv \
    | grep -E "MEASURE|succeeded"
' | tee "$LOGS/10-40-scale-run.log"
