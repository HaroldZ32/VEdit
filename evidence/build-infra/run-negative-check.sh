#!/usr/bin/env bash
# Falsification of the fixed Rust-ABI check in scripts/build-qt-in-container.sh.
#
#   bash evidence/build-infra/run-negative-check.sh [APP]
#
# Proves four things, all headless:
#   1. THE MECHANISM (deterministic): under `set -o pipefail`, a producer that
#      writes a match early and keeps writing dies of SIGPIPE(141) when the
#      consumer is `grep -q`, so the pipeline reads as FAILURE although the match
#      exists. The fixed form (`grep ... > /dev/null`) returns 0 on that stream.
#      This is the exact defect that produced the real false FATAL recorded in
#      evidence/project-model/17-typecheck-gate.log.
#   2. THE CHECK STILL REJECTS A MISSING SYMBOL: the fixed predicate, run exactly
#      as in the script, returns non-zero for (a) a compiled shim that exports
#      vd_* symbols but NOT vd_abi_version and (b) /bin/true (no vd_* at all).
#   3. THE SECOND FATAL (<20 vd_* symbols) still fires for both negatives.
#   4. On the REAL product binary the fixed predicate passes (0).
# The script text is printed so the reader can compare it with the predicate run
# here. Nothing in the repository is modified.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="$ROOT/evidence/build-infra"
APP=${1:-"$ROOT/target/qt6-build/src/app/vedit-app"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$OUT"
{
  echo "# C9 evidence header"
  echo "collected_at: $(date --iso-8601=seconds)"
  echo "git_head: $(git -C "$ROOT" rev-parse HEAD)"
  echo "app: $APP"
  echo "app_sha256: $(sha256sum "$APP" | cut -d' ' -f1)"
  echo
  echo "## the check as it stands in scripts/build-qt-in-container.sh"
  sed -n '/The check must not die of SIGPIPE/,/^fi$/p' "$ROOT/scripts/build-qt-in-container.sh"
  echo
  echo "## 1. deterministic repro of the mechanism (synthetic nm-sized stream)"
} > "$OUT/01-negative-check.log"

# --- 1. the shell semantics, deterministically ---------------------------------
# An unbounded producer with the match first: `grep -q` exits at the first line,
# the producer dies on the broken pipe (`nm` is killed by SIGPIPE -> 141, as the
# recorded gate run shows in evidence/project-model/17-typecheck-gate.log; GNU
# coreutils report "Broken pipe" and exit 1), and with `set -o pipefail` the
# pipeline reports failure even though the match exists.
# A bounded nm-sized stream is used for the fixed form, because a plain grep
# reads to EOF and an unbounded producer would never finish.
bounded_with_match() {
  printf '%s\n' '00000000000fdc00 T vd_abi_version'
  seq 1 40000 | sed 's/.*/0000000000000000 T filler_symbol/'
}
bounded_without_match() {
  seq 1 40000 | sed 's/.*/0000000000000000 T filler_symbol/'
}

old_unbounded_status=0
old_bounded_status=0
new_status=0
new_missing_status=0
set +e
( set -o pipefail; yes ' T vd_abi_version' | grep -qE ' T vd_abi_version$' ) >/dev/null 2>&1
old_unbounded_status=$?
( set -o pipefail; bounded_with_match | grep -qE ' T vd_abi_version$' ) >/dev/null 2>&1
old_bounded_status=$?
( set -o pipefail; bounded_with_match | grep -E ' T vd_abi_version$' > /dev/null ) >/dev/null 2>&1
new_status=$?
( set -o pipefail; bounded_without_match | grep -E ' T vd_abi_version$' > /dev/null ) >/dev/null 2>&1
new_missing_status=$?
set -e
{
  printf 'match + OLD (grep -q), unbounded producer     -> status=%s   %s\n' "$old_unbounded_status" \
    "$([ "$old_unbounded_status" -eq 0 ] && echo PASS || echo 'FALSE-FAIL (producer dies on the broken pipe; the real gate logged 141)')"
  printf 'match + OLD (grep -q), bounded producer       -> status=%s   %s\n' "$old_bounded_status" \
    "$([ "$old_bounded_status" -eq 0 ] && echo PASS || echo 'FALSE-FAIL (producer write error; any non-zero reads as a missing symbol)')"
  printf 'match + NEW (grep ... >/dev/null), bounded    -> status=%s   %s\n' "$new_status" \
    "$([ "$new_status" -eq 0 ] && echo PASS || echo FAIL)"
  printf 'no match + NEW (grep ... >/dev/null), bounded -> status=%s   %s\n' "$new_missing_status" \
    "$([ "$new_missing_status" -ne 0 ] && echo 'FAIL (correct)' || echo 'FALSE-PASS')"
  echo
  echo "## 2. the exact fixed predicate against real and symbol-less binaries"
} | tee -a "$OUT/01-negative-check.log"

# A shim exporting vd_* symbols but deliberately NOT vd_abi_version.
cat > "$TMP/shim.c" <<'C'
/* Exports vd_* symbols but deliberately NOT vd_abi_version. */
void vd_service_create(void) {}
void vd_service_destroy(void) {}
void vd_project_parse(void) {}
void vd_project_poll(void) {}
void vd_job_cancel(void) {}
C
gcc -O0 -shared -fPIC -o "$TMP/no-abi-version.so" "$TMP/shim.c"

run_case() {
  local label=$1
  local app=$2
  local status count
  set +e
  ( set -o pipefail; nm -C "$app" | grep -E ' T vd_abi_version$' > /dev/null ) >/dev/null 2>&1
  status=$?
  set -e
  count=$(nm -C "$app" | grep -cE ' T vd_[a-z_]+$' 2>/dev/null || true)
  printf '%-30s predicate=%s  vd_* count=%s\n' "$label" "$status" "${count:-0}"
}

{
  run_case "real vedit-app" "$APP"
  run_case "shim without vd_abi_version" "$TMP/no-abi-version.so"
  run_case "/bin/true (no vd_* at all)" /bin/true
  echo
  echo "expected: real=0 (pass); shim and /bin/true non-zero (FATAL, check keeps its power)"
} | tee -a "$OUT/01-negative-check.log"

# --- 4. script-level verdict: the same `if ! ...; then FATAL; exit 1; fi` block
# Run verbatim against each binary in a subshell, so the recorded number is the
# verdict the script would reach (0 = gate continues, 1 = FATAL).
script_verdict() {
  local app=$1
  (
    set -o pipefail
    if ! nm -C "$app" | grep -E ' T vd_abi_version$' > /dev/null; then
      echo "FATAL: vedit-app does not export vd_abi_version; the Rust service layer is not linked" >&2
      exit 1
    fi
    if [ "$(nm -C "$app" | grep -cE ' T vd_[a-z_]+$')" -lt 20 ]; then
      echo "FATAL: fewer than 20 vd_* C ABI symbols in vedit-app; the seven services are not all linked" >&2
      exit 1
    fi
  ) >/dev/null 2>&1
  echo $?
}

{
  echo
  echo "## 3. script-level verdict (0 = passes to the next step, 1 = FATAL + exit)"
  printf '%-30s verdict=%s\n' "real vedit-app" "$(script_verdict "$APP")"
  printf '%-30s verdict=%s\n' "shim without vd_abi_version" "$(script_verdict "$TMP/no-abi-version.so")"
  printf '%-30s verdict=%s\n' "/bin/true (no vd_* at all)" "$(script_verdict /bin/true)"
} | tee -a "$OUT/01-negative-check.log"

echo
echo "expected: real=0, both negatives=1"
