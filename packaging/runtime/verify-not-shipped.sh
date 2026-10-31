#!/usr/bin/env bash
# Gate: what must NOT be shipped in the VEdit private runtime (T11 / C7 / T56).
#
# Two independent questions:
#   (1) FILE level  — are KDE's systemverilog.xml / verilog.xml copied into the
#       AppDir (including under a renamed file)?  sha256 comparison, not names.
#   (2) LIBRARY level — does the shipped libKF6SyntaxHighlighting.so embed syntax
#       definitions?  A distro build embeds 450 of them, 36 strict-GPL including
#       SystemVerilog = GPLv3+ (T56).  Detected with the T56 zstd scanner when
#       python3-zstandard is available, else with a binutils-only resource-name
#       scan (weaker but still fails on the real problem).
#
# Modes:
#   (default)       delivery gate: every check must PASS, exit 0 only then.
#   --expect-probe  probe AppDir: the probe stamp and the embedded-GPL finding are
#                   expected; they are printed as EXPECTED-RED and the script
#                   exits 0, but it prints DELIVERY_BLOCKED=yes. A probe AppDir
#                   can never make this script report a deliverable runtime.
#
# Usage: verify-not-shipped.sh [--expect-probe] <AppDir>
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCAN="$ROOT/evidence/r2-ksh-embedded/scan-embedded-defs.py"

KDE_SV=3361d8b4ef62c4868165ef045c02d8b6e85a1e57f61c304b399661bdc06be015
KDE_V=7199e3677b0a942e65b40f95cb2481be062b1ca48eb048519e7d4efc098f5970

EXPECT_PROBE=0
if [ "${1:-}" = "--expect-probe" ]; then EXPECT_PROBE=1; shift; fi
APPDIR=${1:?usage: verify-not-shipped.sh [--expect-probe] <AppDir>}
[ -d "$APPDIR" ] || { echo "FAIL: no AppDir at $APPDIR" >&2; exit 2; }

pass=0; fail=0; expected_red=0; blocked=no
ok() { pass=$((pass+1)); printf 'CHECK PASS  %s\n' "$*"; }
bad() { fail=$((fail+1)); printf 'CHECK FAIL  %s\n' "$*"; }
red() { expected_red=$((expected_red+1)); blocked=yes; printf 'CHECK EXPECTED-RED  %s\n' "$*"; }

echo "== verify-not-shipped =="
echo "appdir: $APPDIR"

# --- (1) FILE level ----------------------------------------------------------
kde_hits=0
while IFS= read -r f; do
  s=$(sha256sum "$f" | cut -d' ' -f1)
  if [ "$s" = "$KDE_SV" ] || [ "$s" = "$KDE_V" ]; then
    bad "KDE definition shipped: $f (sha256 $s)"; kde_hits=$((kde_hits+1))
  fi
done < <(find "$APPDIR" -type f -name '*.xml')
[ "$kde_hits" -eq 0 ] && ok "no KDE systemverilog.xml / verilog.xml by sha256"

for n in systemverilog.xml verilog.xml; do
  if find "$APPDIR" -type f -name "$n" | grep -q .; then
    bad "a file named $n is present (must not ship; use our own LGPL definitions)"
  fi
done
ok "no file named systemverilog.xml / verilog.xml"

# --- F2 (T64): whitelist only, no syntax-bundled/, visible set == whitelist ---
if find "$APPDIR" -type d -name 'syntax-bundled' | grep -q .; then
  bad "syntax-bundled/ present (F2 C5: compressed definitions would be auto-loaded)"
else
  ok "no syntax-bundled/ directory (F2 C5)"
fi
SYNTAX_ACTIVE="$APPDIR/usr/share/org.kde.syntax-highlighting/syntax"
WL_FILE="$APPDIR/usr/share/vedit/ksh-whitelist.txt"
if [ -d "$SYNTAX_ACTIVE" ] && [ -s "$WL_FILE" ]; then
  active=$(python3 - "$SYNTAX_ACTIVE" <<'PY2'
import glob, os, re, sys
names=[]
for path in sorted(glob.glob(os.path.join(sys.argv[1], "*.xml"))):
    head=open(path, "r", encoding="utf-8", errors="replace").read(4096)
    m=re.search(r'<language\b[^>]*\bname="([^"]+)"', head)
    names.append(m.group(1) if m else os.path.basename(path))
print(",".join(sorted(names)))
PY2
)
  expected=$(cat "$WL_FILE")
  if [ "$active" = "$expected" ]; then
    ok "active definitions == whitelist [$active]"
  else
    bad "active definitions [$active] != whitelist [$expected]"
  fi
else
  bad "missing $SYNTAX_ACTIVE or $WL_FILE (F2 whitelist not shipped)"
fi

# --- (2) LIBRARY level -------------------------------------------------------
LIB=$(ls -1 "$APPDIR"/lib/libKF6SyntaxHighlighting.so.6* 2>/dev/null | head -1)
if [ -z "$LIB" ]; then
  bad "libKF6SyntaxHighlighting.so.6 missing from $APPDIR/lib"
else
  echo "-- shipped library: $LIB"
  echo "   sha256: $(sha256sum "$LIB" | cut -d' ' -f1)"
  if python3 -c 'import zstandard' >/dev/null 2>&1 && [ -f "$SCAN" ]; then
    echo "   detector: zstd scanner (evidence/r2-ksh-embedded/scan-embedded-defs.py)"
    if python3 "$SCAN" --expect-clean "$LIB" > /tmp/verify-embedded.txt 2>&1; then
      ok "library embeds no strict-GPL syntax definition"
    else
      gpl=$(grep -c '^  GPL  ' /tmp/verify-embedded.txt || true)
      sv=$(grep -m1 'INFO SystemVerilog' /tmp/verify-embedded.txt || true)
      line="library embeds strict-GPL definitions (n=$gpl) [$sv]"
      if [ "$EXPECT_PROBE" = 1 ]; then red "$line"; else bad "$line"; fi
    fi
    sed -n '1,12p' /tmp/verify-embedded.txt | sed 's/^/   /'
  else
    echo "   detector: binutils fallback (python3-zstandard unavailable)"
    if strings -a -el "$LIB" | grep -qx 'systemverilog.xml'; then
      line="library embeds systemverilog.xml as a Qt resource"
      if [ "$EXPECT_PROBE" = 1 ]; then red "$line"; else bad "$line"; fi
    else
      ok "no systemverilog.xml resource name in the library (fallback detector)"
    fi
  fi
fi

# --- (3) probe stamp ---------------------------------------------------------
if [ -f "$APPDIR/PROBE-NOT-FOR-DELIVERY.txt" ]; then
  line="probe AppDir is stamped PROBE-NOT-FOR-DELIVERY.txt"
  if [ "$EXPECT_PROBE" = 1 ]; then red "$line"; else bad "$line"; fi
else
  ok "no probe stamp"
fi

# --- (4) the loader/glibc contract (cheap structural checks) -----------------
[ -x "$APPDIR/ld-linux-x86-64.so.2" ] && ok "packaged loader present" || bad "packaged loader missing"
[ -f "$APPDIR/lib/libc.so.6" ] && ok "packaged libc present" || bad "packaged libc missing"

echo
echo "== summary: pass=$pass fail=$fail expected_red=$expected_red =="
echo "DELIVERY_BLOCKED=$blocked"
if [ "$fail" -ne 0 ]; then exit 1; fi
[ "$EXPECT_PROBE" = 1 ] || [ "$blocked" = no ] || exit 1
exit 0
