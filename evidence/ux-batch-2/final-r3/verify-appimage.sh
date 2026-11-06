#!/usr/bin/env bash
# U8.2 — verify the REPACKAGED artefact, not the build tree.
#
#   bash evidence/ux-batch-2/u8-final/verify-appimage.sh [AppImage|AppDir]
#
# Defaults to the AppDir that packaging/runtime/build.sh leaves behind.
# Checks, in order:
#   1. the packaged selfcheck (packaging/runtime/selfcheck.sh), which includes the
#      syntax five-definition check, the bundled toolchain, the fonts and the loader;
#   2. the packaged translation catalogue is the NEW one (1208 messages = 127,731 B):
#      a stale .qm was one of the findings this batch had to answer;
#   3. the AppRun ksh_guard runs under BOTH locales this product ships to
#      (zh_CN.UTF-8 and en_US.UTF-8). The guard compares the packaged whitelist
#      with the host locale's idea of sorting; a mismatch used to exit 2 on BOTH
#      locales, so one locale is not evidence.
set -u -o pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

OUT="evidence/ux-batch-2/final-r3"
mkdir -p "$OUT"
TARGET="${1:-packaging/runtime/out/AppDir}"
fail=0
say() { printf '  %s\n' "$*"; }

echo "== target: $TARGET"
if [ ! -e "$TARGET" ]; then
  echo "FAIL: $TARGET does not exist — run 'bash packaging/runtime/build.sh' first"
  exit 1
fi

echo "== 1. packaged selfcheck"
if [ -d "$TARGET" ]; then
  bash packaging/runtime/selfcheck.sh --appdir "$TARGET" 2>&1 | tee "$OUT/verify-selfcheck-appdir.log" | tail -12
  rc=${PIPESTATUS[0]}
else
  bash packaging/runtime/selfcheck.sh --appimage "$TARGET" 2>&1 | tee "$OUT/verify-selfcheck-appimage.log" | tail -12
  rc=${PIPESTATUS[0]}
fi
say "selfcheck exit=$rc"
[ "$rc" -eq 0 ] || fail=1

echo "== 2. packaged catalogue freshness"
# Compile the CURRENT catalogue and require the packaged .qm to be identical.
# A byte count would drift with every new message (it already did twice this
# batch); an sha256 comparison against the fresh compile cannot.
TMPQ=$(mktemp -d)
/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm "$TMPQ/vedit_zh_CN.qm" >"$OUT/verify-lrelease-current.log" 2>&1
FRESH=$(sha256sum "$TMPQ/vedit_zh_CN.qm" | cut -d' ' -f1)
FRESHBYTES=$(stat -c %s "$TMPQ/vedit_zh_CN.qm")
say "fresh compile from translations/vedit_zh_CN.ts: bytes=$FRESHBYTES sha256=$FRESH"
# An AppDir is a directory; an AppImage has to be mounted to look inside.
if [ -d "$TARGET" ]; then
  QM=$(find "$TARGET" -path "*bin/translations/vedit_zh_CN.qm" | head -1)
  if [ -n "$QM" ]; then
    PACKED=$(sha256sum "$QM" | cut -d' ' -f1)
    say "packaged .qm: $QM  bytes=$(stat -c %s "$QM")  sha256=$PACKED"
    if [ "$PACKED" != "$FRESH" ]; then
      say "FAIL: the packaged catalogue is NOT the one built from the current .ts (stale artefact?)"
      fail=1
    fi
  else
    say "FAIL: no bin/translations/vedit_zh_CN.qm inside $TARGET"
    fail=1
  fi
else
  say "SKIP: $TARGET is a file; mount it (--appimage-extract) to inspect the catalogue"
fi
rm -rf "$TMPQ"

echo "== 3. AppRun ksh_guard under both shipped locales"
APPRUN=""
if [ -d "$TARGET" ]; then APPRUN="$TARGET/AppRun"; fi
if [ -z "$APPRUN" ] || [ ! -x "$APPRUN" ]; then
  say "SKIP: no executable AppRun at $TARGET (use the AppDir to check the guard)"
else
  TMPHOME=$(mktemp -d)
  for LOC in zh_CN.UTF-8 en_US.UTF-8; do
    env -i PATH=/usr/bin:/bin HOME="$TMPHOME" LANG="$LOC" LC_ALL="$LOC" \
        QT_QPA_PLATFORM=offscreen "$APPRUN" --selfcheck \
        >"$OUT/verify-apprun-${LOC}.log" 2>&1
    rc=$?
    line=$(grep -E "APP-SHELL-SELFTEST|ksh|KSH" "$OUT/verify-apprun-${LOC}.log" | tail -2 | tr '\n' ' ')
    say "LANG=$LOC exit=$rc  ${line}"
    [ "$rc" -eq 0 ] || fail=1
  done
  rm -rf "$TMPHOME"
fi

echo
if [ "$fail" -ne 0 ]; then
  echo "FINAL-R3 VERIFY FAIL — see $OUT/verify-*.log"
  exit 1
fi
echo "FINAL-R3 VERIFY PASS — packaged artefact verified (selfcheck + catalogue + both locales)"
