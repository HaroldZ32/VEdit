#!/usr/bin/env bash
# Reproduce the "badge always shows verible" diagnosis.
#
#   bash probes/badge-diagnosis/run-probe.sh [out-dir]
#
# Runs the delivered product (offscreen) once per case with a DEDICATED
# --state-dir, a hand-written <state-dir>/settings.toml, and optionally a fake
# `slang-server`; captures the product's own SWITCH / BACKEND lines, which carry
# the same value the status-bar badge renders (`state_.lspBackendName()`).
#
# Nothing is installed and no production file is touched: the cases live under
# /tmp inside the Qt6 image and the report lands in the out-dir.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-$HERE/out}
mkdir -p "$OUT"

QT_IMAGE=${VEDIT_QT_IMAGE:-vedit-qt6-build:local}

docker run --rm \
  -v "$ROOT:/vedit-build" -w /vedit-build/target/qt6-build \
  -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg \
  "$QT_IMAGE" bash -c '
set -uo pipefail
APP=./src/app/vedit-app
PROBE=/tmp/badge-probe
FIXTURE=/vedit-build/tests/fixtures/verilog-project
FAKE=$PROBE/fake/bin
mkdir -p "$FAKE"
printf "#!/bin/sh\nexec sleep 600\n" > "$FAKE/slang-server"
chmod +x "$FAKE/slang-server"
rm -rf "$PROBE"/case-*

store() {
  local dir=$1; shift
  mkdir -p "$dir"
  : > "$dir/settings.toml"
  for line in "$@"; do printf "%s\n" "$line" >> "$dir/settings.toml"; done
}

show() {
  grep -E "SWITCH|is table|store holds|PROJECT-EXPAND-INPUTS" "$1" | head -8
  if ! grep -qE "SWITCH" "$1"; then echo "(no switch line)"; fi
}

run_case() {
  local name=$1 dir=$2 path_prefix=${3:-}
  local log=$PROBE/case-$name.log
  echo "=== CASE $name ==="
  echo "STORE:"
  sed -e "s|^|  |" "$dir/settings.toml"
  echo "PATH-EXTRA: ${path_prefix:-<none>}"
  if [ -n "$path_prefix" ]; then
    PATH="$path_prefix:$PATH" "$APP" --state-dir "$dir" --project "$FIXTURE" --hold 3500 >"$log" 2>&1
  else
    "$APP" --state-dir "$dir" --project "$FIXTURE" --hold 3500 >"$log" 2>&1
  fi
  echo "OBSERVED:"
  show "$log"
  echo
}

# 1 control: the baseline backend selected explicitly.
store $PROBE/case-verible "\"tools.lsp_backend\" = \"verible\""
# 2 the reported case: slang requested, no slang-server anywhere.
store $PROBE/case-slang-missing "\"tools.lsp_backend\" = \"slang\""
# 3 slang requested and the user pointed the Tools page at a server binary.
store $PROBE/case-slang-store-path "\"tools.lsp_backend\" = \"slang\"" "\"tools.slang_server\" = \"$FAKE/slang-server\""
# 4 slang requested and the server is on PATH.
store $PROBE/case-slang-on-path "\"tools.lsp_backend\" = \"slang\""
# 5 the DISPLAY spelling in the store (valid for the Rust schema).
store $PROBE/case-display-spelling "\"tools.lsp_backend\" = \"slang-server\""
# 6 a bare dotted key: TOML nests it, the store only holds strings.
mkdir -p $PROBE/case-bare-dotted-key
printf "tools.lsp_backend = \"slang\"\n" > $PROBE/case-bare-dotted-key/settings.toml

run_case verible $PROBE/case-verible
run_case slang-missing $PROBE/case-slang-missing
run_case slang-store-path $PROBE/case-slang-store-path
run_case slang-on-path $PROBE/case-slang-on-path "$FAKE"
run_case display-spelling $PROBE/case-display-spelling
run_case bare-dotted-key $PROBE/case-bare-dotted-key

echo "=== CASE live-switch ==="
mkdir -p $PROBE/case-live
: > $PROBE/case-live/settings.toml
(
  sleep 2
  printf "%s\n" "\"tools.lsp_backend\" = \"slang\"" "\"tools.slang_server\" = \"$FAKE/slang-server\"" > $PROBE/case-live/settings.toml
) &
"$APP" --state-dir $PROBE/case-live --project "$FIXTURE" --hold 6000 >$PROBE/case-live.log 2>&1
wait
echo "STORE (written while the product was running):"
sed -e "s|^|  |" $PROBE/case-live/settings.toml
echo "OBSERVED:"
show $PROBE/case-live.log
echo

echo "=== SUMMARY ===" 
printf "%-18s %-10s %-9s %-8s %s\n" case ok-switch failed backend note
for name in verible slang-missing slang-store-path slang-on-path display-spelling bare-dotted-key live-switch; do
  if [ "$name" = live-switch ]; then log=$PROBE/case-live.log; else log=$PROBE/case-$name.log; fi
  [ -f "$log" ] || continue
  ok=$(grep -cE "^SWITCH elapsed" "$log")
  failed=$(grep -cE "^SWITCH-FAILED" "$log")
  backend=$(grep -oE "^SWITCH elapsed-ms=[0-9]+ backend=[a-z-]+" "$log" | tail -1 | sed -e "s/.*backend=//")
  note=$( { grep -m1 -oE "^SWITCH-FAILED .*reason=.*" "$log" || grep -m1 -oE "is table but the store holds strings" "$log"; } | cut -c1-90)
  printf "%-18s %-10s %-9s %-8s %s\n" "$name" "$ok" "$failed" "${backend:-<none>}" "$note"
done
' 2>&1 | tee "$OUT/probe-report.txt"
