#!/usr/bin/env bash
# Independently compare the check-label sets of the predecessor verification
# (evidence/ux-fixes-verification), the repair logs (evidence/ux-repair-2) and
# THIS verification's own focused ctest run.
#
# A label is the first token after "CHECK " (format "CHECK <label> PASS ...") or
# after "ok   " (module-tree format "ok   <label> — ..."). ctest -V prefixes
# every line with "<test-number>: ", which is stripped first.
set -uo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
OURS="$ROOT/evidence/ux-repair-verification/logs/20-focused-ctest.log"
REPAIR="$ROOT/evidence/ux-repair-2/logs"
OLD="$ROOT/evidence/ux-fixes-verification/logs"

extract() {
    sed -E 's/^[0-9]+: //' "$1" \
        | sed -n 's/^CHECK \([^ ]*\) \(PASS\|FAIL\|SKIP\).*/\1/p; s/^ok  *\([^ ]*\).*/\1/p' \
        | LC_ALL=C sort -u
}

[ -f "$OURS" ] || { echo "missing $OURS" >&2; exit 1; }
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT
extract "$OURS" > "$TMP/ours"

echo "== THIS RUN (focused ctest) summaries =="
grep -E 'APP-SHELL-SELFTEST ok=|PROJECT-MODEL-SELFTEST ok=|PALETTE-SELFTEST ok=|INTEGRATION-SELFTEST ok=|WORKSPACE-SEARCH-SELFTEST ok=|[0-9]+ checks, [0-9]+ failure' "$OURS" || true
grep -E '100% tests passed|tests failed out of|EXIT=' "$OURS" || true

echo
echo "== predecessor label set vs THIS RUN (only-old must be empty) =="
for pair in \
    "20-app-selfcheck.log app" \
    "22-project-selftest.log project" \
    "24-palette-selftest.log palette" \
    "26-search-selftest.log search" \
    "29-module-tree-direct.log module-tree"; do
    set -- $pair
    extract "$OLD/$1" > "$TMP/old"
    old_n=$(wc -l < "$TMP/old" | tr -d ' ')
    missing=$(LC_ALL=C comm -23 "$TMP/old" "$TMP/ours" | wc -l | tr -d ' ')
    extra=$(LC_ALL=C comm -13 "$TMP/old" "$TMP/ours" | wc -l | tr -d ' ')
    echo "-- $2: predecessor-labels=$old_n missing-from-this-run=$missing extra-in-this-run-union=$extra"
    if [ "$missing" -ne 0 ]; then
        echo "   ONLY OLD:"; LC_ALL=C comm -23 "$TMP/old" "$TMP/ours"
    fi
done

echo
echo "== predecessor palette-integration baseline 28 vs THIS RUN =="
BASE="$ROOT/evidence/edit-core-regression/01-palette-selftest.log"
if [ -f "$BASE" ]; then
    extract "$BASE" > "$TMP/base-integr"
    echo "baseline-labels=$(wc -l < "$TMP/base-integr" | tr -d ' ')"
    echo "removed-from-this-run:"
    LC_ALL=C comm -23 "$TMP/base-integr" "$TMP/ours"
    echo "new integration labels in this run:"
    LC_ALL=C comm -13 "$TMP/base-integr" "$TMP/ours" | grep '^integration\.' || true
else
    echo "missing baseline $BASE"
fi

echo
echo "== repair REPORT labels vs THIS RUN (missing must be 0) =="
for pair in \
    "21-app-selfcheck.log app" \
    "22-project-selftest.log project" \
    "23-palette-selftest.log palette" \
    "24-palette-integration-selftest.log integration" \
    "25-search-selftest.log search" \
    "26-module-tree-selftest.log module-tree"; do
    set -- $pair
    extract "$REPAIR/$1" > "$TMP/rep"
    missing=$(LC_ALL=C comm -23 "$TMP/rep" "$TMP/ours" | wc -l | tr -d ' ')
    echo "-- $2: repair-labels=$(wc -l < "$TMP/rep" | tr -d ' ') missing-from-this-run=$missing"
    if [ "$missing" -ne 0 ]; then
        LC_ALL=C comm -23 "$TMP/rep" "$TMP/ours"
    fi
done

echo
echo "== project: predecessor 150 -> repair 153 -> this run 153 =="
extract "$OLD/22-project-selftest.log" > "$TMP/p-old"
extract "$REPAIR/22-project-selftest.log" > "$TMP/p-rep"
echo "predecessor=$(wc -l < "$TMP/p-old" | tr -d ' ') repair=$(wc -l < "$TMP/p-rep" | tr -d ' ')"
echo "only-new-vs-predecessor:"
LC_ALL=C comm -13 "$TMP/p-old" "$TMP/p-rep" || true
echo "only-old-vs-predecessor:"
LC_ALL=C comm -23 "$TMP/p-old" "$TMP/p-rep" || true

echo
echo "LABEL-COMPARE DONE"
