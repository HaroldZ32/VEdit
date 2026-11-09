#!/usr/bin/env bash
# T4 independent check of assemble-appdir.sh §7's icon invariants.
# The block under test is EXTRACTED VERBATIM from the real script (sed range
# anchored on its comments), so this exercises the shipped code, not a
# re-implementation. A fake $OUT is populated like a successful AppDir and then
# damaged in five ways; every damage must make the block exit non-zero.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
LOGS="$ROOT/evidence/ui-mosaic/review/logs"
SCRIPT="$ROOT/packaging/runtime/assemble-appdir.sh"
BLOCK="$LOGS/appdir-invariant-block.sh"
SCRATCH="$LOGS/appdir-invariants"
ICONS="$ROOT/resources/icons/hicolor"

sed -n '/^# UI-Mosaic A4: an installed launcher/,/^log "icon invariants:/p' "$SCRIPT" > "$BLOCK"
echo "extracted block: $(wc -l < "$BLOCK") lines from $SCRIPT"
bash -n "$BLOCK" && echo "bash -n block: OK"

populate() {
  rm -rf "$SCRATCH"; mkdir -p "$SCRATCH/out/usr/share/icons/hicolor"
  local out="$SCRATCH/out"
  cp "$ROOT/packaging/runtime/assets/vedit.desktop" "$out/vedit.desktop"
  cp "$ICONS/256x256/apps/io.github.vedit.VEdit.png" "$out/io.github.vedit.VEdit.png"
  cp "$ICONS/256x256/apps/io.github.vedit.VEdit.png" "$out/.DirIcon"
  for size in 16 24 32 48 64 128 256 512; do
    mkdir -p "$out/usr/share/icons/hicolor/${size}x${size}/apps"
    cp "$ICONS/${size}x${size}/apps/io.github.vedit.VEdit.png" \
       "$out/usr/share/icons/hicolor/${size}x${size}/apps/io.github.vedit.VEdit.png"
  done
}

run_block() { # run_block <label> <expected-exit>
  local label=$1 expected=$2 status
  set +e
  OUT="$SCRATCH/out" bash -c 'set -euo pipefail; log() { echo "LOG: $*"; }; source "$1"' _ "$BLOCK" \
    > "$SCRATCH/$label.log" 2>&1
  status=$?
  set -e
  if [ "$status" -eq "$expected" ]; then
    echo "PASS $label: exit=$status (expected $expected)"
  else
    echo "FAIL $label: exit=$status (expected $expected)"
    tail -3 "$SCRATCH/$label.log"
  fi
}

echo "--- positive control ---"
populate
run_block positive 0

echo "--- negative 1: 64x64 hicolor removed ---"
populate; rm "$SCRATCH/out/usr/share/icons/hicolor/64x64/apps/io.github.vedit.VEdit.png"
run_block missing-64 1
grep -o 'ERROR: icon-chain artifact missing or empty:.*' "$SCRATCH/missing-64.log" | head -1

echo "--- negative 2: .DirIcon truncated to 0 bytes ---"
populate; : > "$SCRATCH/out/.DirIcon"
run_block empty-diricon 1
grep -o 'ERROR: icon-chain artifact missing or empty:.*' "$SCRATCH/empty-diricon.log" | head -1

echo "--- negative 3: AppDir root icon is not a PNG ---"
populate; printf 'not-a-png' > "$SCRATCH/out/io.github.vedit.VEdit.png"
run_block corrupt-png 1
grep -o 'ERROR: io.github.vedit.VEdit.png is not a PNG.*' "$SCRATCH/corrupt-png.log" | head -1

echo "--- negative 4: desktop entry loses StartupWMClass ---"
populate; grep -v '^StartupWMClass=' "$SCRATCH/out/vedit.desktop" > "$SCRATCH/out/d" && mv "$SCRATCH/out/d" "$SCRATCH/out/vedit.desktop"
run_block no-wmclass 1
grep -o 'ERROR: vedit.desktop lacks StartupWMClass.*' "$SCRATCH/no-wmclass.log" | head -1

echo "--- negative 5: desktop entry missing entirely ---"
populate; rm "$SCRATCH/out/vedit.desktop"
run_block no-desktop 1
grep -o 'ERROR: icon-chain artifact missing or empty: vedit.desktop' "$SCRATCH/no-desktop.log" | head -1
