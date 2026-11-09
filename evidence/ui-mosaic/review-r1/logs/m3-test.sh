#!/usr/bin/env bash
# Independent M3 check for the R1 review.
#
# The previous round found verify-f2-isolation.sh asserted only 4 of the 8
# hicolor tiers the installer writes. This script extracts the REAL
# check_icon_chain() from the shipping script (no copy, no paraphrase), builds a
# synthetic AppDir with real PNGs at every tier and proves:
#   positive: no failure,
#   negative: removing EACH of the four newly asserted tiers (24/32/48/128)
#             turns the check red for that exact path.
set -uo pipefail
ROOT="/home/haroldzhao/MyProj/DshProj/VEdit"
SRC="$ROOT/packaging/runtime/verify-f2-isolation.sh"
OUT="$ROOT/evidence/ui-mosaic/review-r1/logs/m3"
mkdir -p "$OUT"
FUNC="$OUT/check_icon_chain.extracted.sh"
awk '/^check_icon_chain\(\)/{f=1} f{print} f&&/^}$/{exit}' "$SRC" > "$FUNC"
grep -q '48x48' "$FUNC" || { echo "EXTRACT-FAIL: extracted function lacks 48x48"; exit 1; }

mk_appdir() { # <dir> [drop-size]
  local d=$1 drop=${2:-}
  rm -rf "$d"; mkdir -p "$d/usr/share/icons/hicolor"
  python3 - "$d" "$drop" <<'PY'
import os, sys
from PIL import Image
d, drop = sys.argv[1], sys.argv[2]
sizes = [16, 24, 32, 48, 64, 128, 256, 512]
Image.new("RGBA", (256, 256), (20, 120, 200, 255)).save(os.path.join(d, "io.github.vedit.VEdit.png"))
Image.new("RGBA", (256, 256), (20, 120, 200, 255)).save(os.path.join(d, ".DirIcon"), format="PNG")
with open(os.path.join(d, "vedit.desktop"), "w") as fh:
    fh.write("[Desktop Entry]\nName=VEdit\nIcon=io.github.vedit.VEdit\nStartupWMClass=io.github.vedit.VEdit\n")
for s in sizes:
    sub = os.path.join(d, "usr/share/icons/hicolor", "%dx%d" % (s, s), "apps")
    os.makedirs(sub, exist_ok=True)
    if drop and str(s) == drop:
        continue
    Image.new("RGBA", (s, s), (20, 120, 200, 255)).save(os.path.join(sub, "io.github.vedit.VEdit.png"))
PY
}

run_check() { # <dir> <label>
  (
    set -uo pipefail
    ok=0; bad=0
    pass() { ok=$((ok+1)); echo "CHECK PASS  $*"; }
    fail() { bad=$((bad+1)); echo "CHECK FAIL  $*"; }
    source "$FUNC"
    check_icon_chain "$2" "$1"
    echo "SUMMARY label=$2 ok=$ok bad=$bad"
  ) > "$OUT/$2.log" 2>&1
  grep -c '^CHECK FAIL' "$OUT/$2.log" || true
}

{
  mk_appdir "$OUT/AppDir-ok"
  nf=$(run_check "$OUT/AppDir-ok" positive)
  echo "positive: CHECK FAIL count = $nf (expect 0)"

  rc=0
  for s in 16 24 32 48 64 128 256 512; do
    mk_appdir "$OUT/AppDir-drop-$s" "$s"
    nf=$(run_check "$OUT/AppDir-drop-$s" "drop-$s")
    if [ "$nf" -ge 1 ] && grep -q "^CHECK FAIL  drop-${s}: icon artifact missing or empty: usr/share/icons/hicolor/${s}x${s}/apps/io.github.vedit.VEdit.png" "$OUT/drop-$s.log"; then
      echo "negative drop-${s}: RED as expected (fails=$nf)"
    else
      echo "negative drop-${s}: *** NOT RED *** (fails=$nf)"; rc=1
    fi
  done
  echo "M3-RESULT: positive-clean-and-all-eight-tier-negatives-red=$([ "$rc" -eq 0 ] && echo YES || echo NO)"
  exit "$rc"
} 2>&1 | tee "$OUT/m3-run.log"
