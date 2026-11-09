#!/usr/bin/env bash
# R1 / M1 + M3 + M4 evidence: per-finding commands and their observed results.
#
#   M1  the six product-theme names are translatable (English source text in the
#       binary, Chinese in the zh_CN catalogue), the ids are unchanged.
#   M3  verify-f2-isolation.sh asserts all EIGHT hicolor tiers assemble-appdir.sh
#       installs (16/24/32/48/64/128/256/512), with a negative control proving the
#       new assertions can actually fire.
#   M4  main.cpp has ONE icon source: themed_icons::appIcon(false).
#
# Usage: bash evidence/ui-mosaic/fix-r1/run-m1-m4.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
BIN_REL="target/qt6-build/src/app/vedit-app"
OUT="$ROOT/evidence/ui-mosaic/fix-r1"
LOG="$OUT/logs"
STATE="$OUT/state-m1"
mkdir -p "$LOG" "$STATE"

ok=0
bad=0
pass() { ok=$((ok + 1)); printf 'PASS  %s\n' "$*"; }
fail() { bad=$((bad + 1)); printf 'FAIL  %s\n' "$*"; }

run_theme_probe() { # <language> <log>
  local state="$STATE/$1" log=$2
  mkdir -p "$state"
  printf 'uiTheme = "qingkong"\n"ui.language" = "%s"\n' "$1" > "$state/settings.toml"
  docker run --rm -v "$ROOT":/vedit-build -v "$state":/state -w /vedit-build \
    -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg-r1-m1 -e VEDIT_UI_PROBE=1 \
    "$IMG" bash -lc \
    "mkdir -p /tmp/xdg-r1-m1 && chmod 700 /tmp/xdg-r1-m1 && \
     ./$BIN_REL --state-dir /state --hold 3200" > "$LOG/$log" 2>&1
  grep -o 'status-theme=[^ ]*.*' "$LOG/$log" | head -1
}

echo "== M1: translatable product-theme display names =="
en_status=$(run_theme_probe en m1-english.log)
zh_status=$(run_theme_probe zh_CN m1-chinese.log)
printf 'en  %s\n' "$en_status"
printf 'zh  %s\n' "$zh_status"
case "$en_status" in
  *"Theme: Sky Blue /"*) pass "M1: English UI shows the English source name 'Sky Blue'" ;;
  *) fail "M1: English UI did not show 'Sky Blue': $en_status" ;;
esac
case "$zh_status" in
  *"晴空蓝"*) pass "M1: zh_CN UI shows the hand-written translation 晴空蓝" ;;
  *) fail "M1: zh_CN UI did not show 晴空蓝: $zh_status" ;;
esac
# The persisted/selector id is untouched by the display-name change.
grep -q '\[ui-probe\] theme=qingkong' "$LOG/m1-english.log" \
  && pass "M1: the persisted id is still qingkong (display text does not change it)" \
  || fail "M1: probe theme id is not qingkong"
printf 'ids in product_theme.cpp: '; grep -o 'makeTheme("[a-z]*"' "$ROOT/src/app/product_theme.cpp" \
  | sed 's/makeTheme("//;s/"//' | paste -sd, -
for name in "Cloud White" "Warm Apricot" "Mint Green" "Sky Blue" "Obsidian" "Deep Navy"; do
  grep -q "\"$name\"" "$ROOT/src/app/product_theme.cpp" \
    && pass "M1: product_theme.cpp carries the English source \"$name\"" \
    || fail "M1: product_theme.cpp is missing the English source \"$name\""
done
grep -q 'QCoreApplication::translate("product_theme"' "$ROOT/src/app/product_theme.cpp" \
  && pass "M1: displayName is built through QCoreApplication::translate(\"product_theme\")" \
  || fail "M1: product_theme.cpp does not translate displayName"
for name in "Cloud White:云雾白" "Warm Apricot:暖杏黄" "Mint Green:薄荷绿" "Sky Blue:晴空蓝" "Obsidian:曜石黑" "Deep Navy:深夜蓝"; do
  src=${name%%:*}; dst=${name##*:}
  if python3 - "$ROOT/translations/vedit_zh_CN.ts" "$src" "$dst" <<'PY'
import sys
import xml.etree.ElementTree as ET
ctx = ET.parse(sys.argv[1]).getroot().find("./context[name='product_theme']")
assert ctx is not None, "no product_theme context"
found = {m.findtext("source"): m.findtext("translation") for m in ctx.findall("message")}
sys.exit(0 if found.get(sys.argv[2]) == sys.argv[3] else 1)
PY
  then pass "M1: vedit_zh_CN.ts maps $src -> $dst"
  else fail "M1: vedit_zh_CN.ts does not map $src -> $dst"
  fi
done

echo
echo "== M3: verify-f2-isolation.sh asserts every hicolor tier =="
CHECK="$OUT/check_icon_chain.extracted.sh"
sed -n '/^check_icon_chain() {/,/^}/p' "$ROOT/packaging/runtime/verify-f2-isolation.sh" > "$CHECK"
grep -c 'usr/share/icons/hicolor/[0-9]*x[0-9]*/apps/io.github.vedit.VEdit.png' "$CHECK" \
  | xargs -I{} printf 'hicolor tiers asserted by the extracted function: {}\n'
for tier in 16 24 32 48 64 128 256 512; do
  grep -q "hicolor/${tier}x${tier}/apps/io.github.vedit.VEdit.png" "$CHECK" \
    && pass "M3: ${tier}x${tier} is asserted" \
    || fail "M3: ${tier}x${tier} is NOT asserted"
done
bash -n "$ROOT/packaging/runtime/verify-f2-isolation.sh" \
  && pass "M3: verify-f2-isolation.sh parses (bash -n)" \
  || fail "M3: verify-f2-isolation.sh does not parse"

FIXTURE="$OUT/m3-appdir"
make_fixture() {
  rm -rf "$FIXTURE"
  mkdir -p "$FIXTURE/usr/share/icons/hicolor"
  printf 'Icon=io.github.vedit.VEdit\nStartupWMClass=io.github.vedit.VEdit\n' > "$FIXTURE/vedit.desktop"
  cp "$ROOT/resources/icons/app/vedit-light.png" "$FIXTURE/io.github.vedit.VEdit.png"
  cp "$ROOT/resources/icons/app/vedit-light.png" "$FIXTURE/.DirIcon"
  for tier in 16 24 32 48 64 128 256 512; do
    mkdir -p "$FIXTURE/usr/share/icons/hicolor/${tier}x${tier}/apps"
    cp "$ROOT/resources/icons/hicolor/${tier}x${tier}/apps/io.github.vedit.VEdit.png" \
       "$FIXTURE/usr/share/icons/hicolor/${tier}x${tier}/apps/io.github.vedit.VEdit.png"
  done
}
harness() { # runs the EXTRACTED check_icon_chain against $FIXTURE
  bash -c '
    set -u
    ok=0; bad=0
    pass() { ok=$((ok+1)); printf "PASS  %s\n" "$*"; }
    fail() { bad=$((bad+1)); printf "FAIL  %s\n" "$*"; }
    source "$1"
    check_icon_chain "$2" "$3"
    printf "== extracted-check pass=%d fail=%d ==\n" "$ok" "$bad"
    [ "$bad" -eq 0 ]
  ' _ "$CHECK" fixture "$FIXTURE"
}
make_fixture
if harness > "$LOG/m3-positive.log" 2>&1; then
  pass "M3: positive control — an AppDir with all eight tiers passes ($(grep -c '^PASS' "$LOG/m3-positive.log") assertions)"
else
  fail "M3: positive control failed: $(tail -3 "$LOG/m3-positive.log" | paste -sd' ')"
fi
rm -f "$FIXTURE/usr/share/icons/hicolor/48x48/apps/io.github.vedit.VEdit.png"
if harness > "$LOG/m3-negative.log" 2>&1; then
  fail "M3: negative control — a MISSING 48x48 tier still passed (the assertion cannot fire)"
else
  grep -q 'icon artifact missing or empty: usr/share/icons/hicolor/48x48' "$LOG/m3-negative.log" \
    && pass "M3: negative control — the missing 48x48 tier is reported as a FAIL" \
    || fail "M3: negative control failed for the wrong reason: $(tail -3 "$LOG/m3-negative.log" | paste -sd' ')"
fi

echo
echo "== M4: one icon source in main.cpp =="
if grep -n ':/icons/app/' "$ROOT/src/app/main.cpp"; then
  fail "M4: main.cpp still hard-codes a :/icons/app/ resource path"
else
  pass "M4: main.cpp no longer hard-codes any :/icons/app/ resource path"
fi
grep -n 'themed_icons::appIcon(false)' "$ROOT/src/app/main.cpp" \
  && pass "M4: main.cpp calls themed_icons::appIcon(false) (the single source)" \
  || fail "M4: main.cpp does not call themed_icons::appIcon(false)"
grep -q '#include "themed_icons.h"' "$ROOT/src/app/main.cpp" \
  && pass "M4: main.cpp includes themed_icons.h" \
  || fail "M4: main.cpp does not include themed_icons.h"

printf '\n== summary: pass=%d fail=%d ==\n' "$ok" "$bad"
[ "$bad" -eq 0 ] && echo "M1-M3-M4 PASS" || echo "M1-M3-M4 FAIL"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
