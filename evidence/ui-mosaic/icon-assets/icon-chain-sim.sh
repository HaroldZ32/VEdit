#!/usr/bin/env bash
# Harness for UI-Mosaic A4 icon-chain evidence.
#
# It does NOT re-implement the packaging logic: it extracts the REAL blocks from
# packaging/runtime/assemble-appdir.sh and packaging/runtime/verify-f2-isolation.sh
# (matched by their section markers, not line numbers) and runs them against a
# throw-away AppDir root. That makes the positive path and the "delete an icon
# and the build must fail" negative controls executable on a host that cannot
# run the full assemble-appdir.sh (which needs the whole Qt/KF6 AppDir payload).
#
# Usage: bash evidence/ui-mosaic/icon-assets/icon-chain-sim.sh
set -uo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
ASSEMBLE="$REPO/packaging/runtime/assemble-appdir.sh"
VERIFY="$REPO/packaging/runtime/verify-f2-isolation.sh"
WORK="$REPO/.mpd/tmp/icon-chain-sim"
OUT="$WORK/AppDir"
LOG="$WORK/verify-icon-chain.log"
ROOT="$REPO"
fails=0

log() { echo "LOG: $*"; }
VERSION="9.9.9-sim"

extract_block() { # <file> <start-regex> <end-regex>
  awk -v start="$2" -v end="$3" \
      '$0 ~ start { in_block=1 } in_block { print } in_block && $0 ~ end { exit }' "$1"
}

assemble_block() { # <start-regex> <end-regex>
  extract_block "$ASSEMBLE" "$1" "$2"
}

install_icons() {
  mkdir -p "$OUT"
  eval "$(assemble_block '^install -m 0644 "\$ROOT/packaging/runtime/assets/vedit.desktop"' '^log "icons:')"
}

check_invariants() {
  eval "$(assemble_block '^# UI-Mosaic A4: an installed launcher' '^log "icon invariants:')"
}

expect_ok() { # <label>
  if ( install_icons ) >/dev/null 2>&1 && ( check_invariants ) >/dev/null 2>&1; then
    echo "PASS  $1"
  else
    echo "FAIL  $1"; fails=$((fails+1))
  fi
}

expect_fail() { # <label> <mutation>
  eval "$2"
  if ( check_invariants ) >/dev/null 2>&1; then
    echo "FAIL  $1 (invariants stayed green)"; fails=$((fails+1))
  else
    echo "PASS  $1 (invariants refused the tree)"
  fi
  ( install_icons ) >/dev/null 2>&1
}

rm -rf "$WORK"; mkdir -p "$WORK"

echo "== positive: real §6 install block + real 7b invariant block =="
if ( install_icons ) >"$WORK/install.log" 2>&1; then
  echo "PASS  install block exited 0"
else
  echo "FAIL  install block exited non-zero"; cat "$WORK/install.log"; fails=$((fails+1))
fi
cat "$WORK/install.log"
echo "installed tree:"
( cd "$OUT" && find . -type f | sort )
if ( check_invariants ) >"$WORK/invariants.log" 2>&1; then
  echo "PASS  7b icon invariants exited 0"; tail -1 "$WORK/invariants.log"
else
  echo "FAIL  7b icon invariants exited non-zero"; cat "$WORK/invariants.log"; fails=$((fails+1))
fi
expect_ok "full tree is accepted"

echo "== negative controls: every icon-chain hole must fail the build =="
expect_fail "missing .DirIcon"                'rm -f "$OUT/.DirIcon"'
expect_fail "missing AppDir root PNG"         'rm -f "$OUT/io.github.vedit.VEdit.png"'
expect_fail "missing vedit.desktop"           'rm -f "$OUT/vedit.desktop"'
expect_fail "missing hicolor 16x16"           'rm -f "$OUT/usr/share/icons/hicolor/16x16/apps/io.github.vedit.VEdit.png"'
expect_fail "missing hicolor 256x256"         'rm -f "$OUT/usr/share/icons/hicolor/256x256/apps/io.github.vedit.VEdit.png"'
expect_fail "empty AppDir root PNG"           ': > "$OUT/io.github.vedit.VEdit.png"'
expect_fail "root icon is not a PNG"          'printf "not a png" > "$OUT/io.github.vedit.VEdit.png"'
expect_fail "desktop lacks StartupWMClass"    'sed -i "/^StartupWMClass=/d" "$OUT/vedit.desktop"'
expect_fail "desktop lacks Icon="             'sed -i "/^Icon=/d" "$OUT/vedit.desktop"'

echo "== verify-f2-isolation.sh check_icon_chain (real function) =="
ok=0; bad=0
eval "$(extract_block "$VERIFY" '^ok=0; bad=0' '^fail()')"
eval "$(extract_block "$VERIFY" '^check_icon_chain\(\)' '^}$')"
mkdir -p "$WORK"
check_icon_chain sim "$OUT"
echo "sim: pass=$ok fail=$bad"
[ "$bad" -eq 0 ] || { fails=$((fails+1)); echo "FAIL  check_icon_chain reported failures on a valid tree"; }

ok=0; bad=0
rm -f "$OUT/usr/share/icons/hicolor/64x64/apps/io.github.vedit.VEdit.png"
check_icon_chain sim-negative "$OUT" || true
echo "sim-negative: pass=$ok fail=$bad"
if [ "$bad" -gt 0 ]; then echo "PASS  check_icon_chain reports the missing 64x64 icon"; else echo "FAIL  check_icon_chain missed the missing 64x64 icon"; fails=$((fails+1)); fi

echo "== summary: fails=$fails =="
[ "$fails" -eq 0 ] && echo "ICON-CHAIN SIM PASS" || echo "ICON-CHAIN SIM FAIL"
[ "$fails" -eq 0 ]
