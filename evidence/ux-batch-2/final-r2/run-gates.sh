#!/usr/bin/env bash
# U8.1 — the one full gate run for UX batch 2, in the order the contract lists
# (contract §5.1 step 3: build-qt / ctest / --selfcheck / clippy / fmt / docs / lrelease).
#
# Usage:  bash evidence/ux-batch-2/u8-final/run-gates.sh
# Writes one log per gate into this directory and a summary table at the end.
# Exit code: 0 only when every gate passed.
set -u -o pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1
OUT="evidence/ux-batch-2/final-r2"
mkdir -p "$OUT"

declare -a NAMES=() CODES=()
fail=0

run_gate() {
  local name="$1"; shift
  local log="$OUT/$name.log"
  echo "=== [$name] $* "
  "$@" >"$log" 2>&1
  local code=$?
  echo "=== [$name] exit=$code  log=$log"
  tail -5 "$log" | sed 's/^/    /'
  NAMES+=("$name"); CODES+=("$code")
  [ "$code" -eq 0 ] || fail=1
}

# 1. Rust lane: cargo fmt --check + clippy -D warnings + cargo test --workspace
#    (runs in the pinned CentOS 7 image when the host lacks GTK dev files).
run_gate rust-gates bash scripts/gates.sh

# 2. C++/Qt6 lane: cmake configure + build + ctest + the xcb/software-rendering smoke.
run_gate qt6-lane bash build/gates/qt6-cxx.sh

# 3. The product's own app-level selftest. It MUST run inside the pinned image:
#    the binary links libtree-sitter.so.0.25 which only exists in the image (or in
#    the packaged AppDir), so the host loader cannot start it. Same mount
#    convention as the lanes (workspace at /vedit-build).
echo "=== [selfcheck] vedit-app --selfcheck (pinned image)"
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  env QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck \
  >"$OUT/selfcheck.log" 2>&1
code=$?
echo "=== [selfcheck] exit=$code"
grep -E "APP-SHELL-SELFTEST|failures=" "$OUT/selfcheck.log" | tail -3 | sed 's/^/    /'
NAMES+=("selfcheck"); CODES+=("$code")
[ "$code" -eq 0 ] || fail=1

# 4. Docs gate.
run_gate docs bash build/gates/docs.sh

# 5. Translations gate (added in this batch; fails on ANY unfinished/untranslated message).
run_gate lrelease bash build/gates/lrelease.sh

echo
echo "================ FINAL-R2 GATE SUMMARY ================"
printf '%-14s %s\n' "GATE" "EXIT"
for i in "${!NAMES[@]}"; do
  printf '%-14s %s\n' "${NAMES[$i]}" "${CODES[$i]}"
done
echo "==================================================="
if [ "$fail" -ne 0 ]; then
  echo "FINAL-R2 FAIL — see $OUT/*.log"
  exit 1
fi
echo "FINAL-R2 PASS — every gate exited 0"
