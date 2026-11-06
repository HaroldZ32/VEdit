#!/usr/bin/env bash
# 7c: the remaining six of the seven gates on the FINAL tree (build-qt is
# 01-build-qt.log, already run). Every log records the exact command, the
# environment it ran in and the observed exit code.
#
#   bash evidence/packaging-2/scripts/seven-gates.sh
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
G="$ROOT/evidence/packaging-2/gates"
mkdir -p "$G"
QT_IMAGE="vedit-qt6-build:local"
RUST_IMAGE="vedit-build:local"
fail=0

run() { # log, label, then the command
  local log="$1" label="$2"
  shift 2
  local rc
  {
    echo "# gate: $label"
    echo "captured_at: $(date --iso-8601=seconds)"
    echo "git_head: $(git -C "$ROOT" rev-parse --short HEAD)"
    echo "cmd: $*"
    echo
    "$@"
    rc=$?
    echo
    echo "${label^^}_EXIT=$rc"
  } > "$G/$log" 2>&1
  echo "[gates] $label exit=$rc log=$log"
  [ "$rc" = "0" ] || fail=1
}

# 2. ctest — captured by build-qt; keep a copy of its verdict + the raw lane log.
{
  echo "# gate: ctest (37 tests, run inside scripts/build-qt.sh)"
  grep -E 'tests passed|tests failed|Test #' "$G/01-build-qt.log" | tail -6
  echo "CTEST_EXIT=$(grep -q '100% tests passed, 0 tests failed out of 37' "$G/01-build-qt.log" && echo 0 || echo 1)"
} > "$G/02-ctest.log" 2>&1
grep -q 'CTEST_EXIT=0' "$G/02-ctest.log" || { echo "[gates] ctest FAILED"; fail=1; }

# 3. product selfcheck (offscreen, pinned Qt6 image)
run 03-selfcheck.log selfcheck docker run --rm -v "$ROOT":/vedit-build -w /vedit-build \
  -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -e LANG=C.UTF-8 "$QT_IMAGE" \
  bash -lc 'target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc-7c'

# 4. clippy
run 04-clippy.log clippy docker run --rm -v "$ROOT":/vedit-build -w /vedit-build \
  -e HOME=/tmp "$RUST_IMAGE" bash -lc 'cargo clippy --workspace --all-targets -- -D warnings'

# 5. fmt
run 05-fmt.log fmt docker run --rm -v "$ROOT":/vedit-build -w /vedit-build \
  -e HOME=/tmp "$RUST_IMAGE" bash -lc 'cargo fmt --all -- --check'

# 6. docs (host gate)
run 06-docs.log docs bash "$ROOT/build/gates/docs.sh"

# 7. lrelease (0 unfinished)
run 07-lrelease.log lrelease docker run --rm -v "$ROOT":/vedit-build -w /vedit-build \
  -e HOME=/tmp "$QT_IMAGE" \
  bash -lc '/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/vedit-7c.qm'

echo
echo "SEVEN-GATES steps_failed=$fail"
[ "$fail" -eq 0 ] && echo "SEVEN-GATES PASS" || echo "SEVEN-GATES FAIL"
exit "$fail"
