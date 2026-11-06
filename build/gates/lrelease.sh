#!/usr/bin/env bash
# Gate `lrelease`: the delivered Chinese catalogue must compile with ZERO
# unfinished AND ZERO untranslated entries.
#
# Why this gate exists (contract §5.1, review RF4): `qt_add_lrelease` in
# src/app/CMakeLists.txt compiles the .qm during the build but does NOT fail when
# entries are unfinished — lrelease exits 0 and only prints
# "(N finished and K unfinished)" / "Ignored U untranslated source text(s)".
# Without this gate "0 unfinished" would be a manual command, so the gate parses
# those counts and fails on either class. It never skips and never passes when it
# cannot prove the counts.
#
# Usage: build/gates/lrelease.sh [catalogue.ts]
#   The optional argument exists for this gate's own negative control (a copy
#   that still has unfinished/untranslated entries); the delivered run uses the
#   default, translations/vedit_zh_CN.ts.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

LRELEASE=${LRELEASE:-/usr/lib/qt6/bin/lrelease}
TS=${1:-translations/vedit_zh_CN.ts}
case "$TS" in
  /*) ;;
  *) TS="$ROOT/$TS" ;;
esac

if [ ! -x "$LRELEASE" ]; then
  no_gate "[lrelease] FAIL: the Qt6 lrelease tool is not available: $LRELEASE
       Install  : the Qt6 l10n tools (qt6-l10n-tools), or point LRELEASE at the binary.
       Host pin : /usr/lib/qt6/bin/lrelease (AGENT.md §4, docs/BUILD.md §4).
       This gate does NOT skip and does NOT pass: without lrelease the catalogue
       is not compiled at all, so nothing is verified."
fi
if [ ! -s "$TS" ]; then
  no_gate "[lrelease] FAIL: the translation catalogue is missing or empty: $TS
       Expected : translations/vedit_zh_CN.ts (the delivered zh_CN catalogue).
       This gate does NOT skip and does NOT pass without the catalogue."
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
QM="$TMP/$(basename "${TS%.ts}").qm"

echo "[lrelease] $LRELEASE $TS -qm $QM"
if ! out=$(LC_ALL=C.UTF-8 "$LRELEASE" "$TS" -qm "$QM" 2>&1); then
  printf '%s\n' "$out"
  no_gate "[lrelease] FAIL: lrelease rejected the catalogue (non-zero exit); the reason is above."
fi
printf '%s\n' "$out"

# lrelease's summary line: `Generated N translation(s) (M finished and K unfinished)`,
# plus `Ignored U untranslated source text(s)` when U > 0. Both numbers matter and
# they are NOT the same defect class: "unfinished" = a translation exists but is
# flagged partial, "untranslated" = no translation at all (silently an English
# string). Checking only `0 unfinished` would pass a catalogue with 177 empty
# messages, so both must be zero. An unparsable run is a FAIL, never a pass.
counts=$(printf '%s\n' "$out" | grep -oE '\([0-9]+ finished and [0-9]+ unfinished\)' | tail -1 || true)
if [ -z "$counts" ]; then
  no_gate "[lrelease] FAIL: could not read the counts line '(N finished and K unfinished)' from lrelease's output.
       This gate refuses to guess: an unparsable run is not a pass."
fi
total=$(printf '%s\n' "$out" | grep -oE 'Generated [0-9]+ translation' | tail -1 | grep -oE '[0-9]+' || true)
finished=$(printf '%s' "$counts" | grep -oE '[0-9]+ finished' | grep -oE '[0-9]+')
unfinished=$(printf '%s' "$counts" | grep -oE '[0-9]+ unfinished' | grep -oE '[0-9]+')
untranslated=$(printf '%s\n' "$out" | grep -oE 'Ignored [0-9]+ untranslated' | tail -1 | grep -oE '[0-9]+' || true)
: "${untranslated:=0}"
if [ "${total:-0}" -le 0 ]; then
  no_gate "[lrelease] FAIL: the catalogue produced 0 translations; an empty catalogue is not a pass."
fi

if [ "${unfinished:-1}" -ne 0 ] || [ "$untranslated" -ne 0 ]; then
  no_gate "[lrelease] FAIL: ${unfinished:-?} unfinished and ${untranslated} untranslated translation(s)
       (${finished} finished of ${total:-?} generated).
       An unfinished entry ships as a partial translation and an untranslated one
       ships as English, both invisible in a Chinese UI review, so the delivered
       catalogue must be complete.
       Fix      : translate the missing entries in translations/vedit_zh_CN.ts
                  (evidence/ux-batch-2/integration-translations/02-unfinished-before.txt
                  lists what the last lupdate added), then re-run this gate.
       This gate does NOT skip and does NOT pass with unfinished/untranslated entries."
fi
if [ ! -s "$QM" ]; then
  no_gate "[lrelease] FAIL: lrelease reported success but produced no .qm ($QM)."
fi

echo "[lrelease] counts: ${total} translation(s) (${finished} finished, ${unfinished} unfinished, ${untranslated} untranslated)"
echo "[lrelease] qm: $(stat -c%s "$QM") bytes sha256=$(sha256sum "$QM" | cut -d' ' -f1)"
echo "[lrelease] PASS: 0 unfinished and 0 untranslated in $TS"
