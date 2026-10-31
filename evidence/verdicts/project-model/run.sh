#!/usr/bin/env bash
# Reproducible headless evidence for the project panel lane (T6 / AC-S1.3).
#
#   bash evidence/project-model/run-standalone.sh
#
# What it proves, all inside the pinned Qt6 image (no display server):
#   1. the lane builds standalone with -Wall -Wextra -Wpedantic -Werror;
#   2. the in-process self-check passes (file count 7, list equals the fixture's
#      expected/files.txt, include dirs, defines, diagnostics for bad entries,
#      panel rendering, empty state, corrupt state file);
#   3. a real RESTART: `--seed` (process 1) expands the fixture and writes the
#      state file, `--restore` (process 2) starts from that state directory only
#      and must still list the same 7 members;
#   4. the bad-entry project produces file+line diagnostics.
#
# Every log starts with the C9 header (collection time, git HEAD, dirty paths).
# Writes only under evidence/project-model/.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="$ROOT/evidence/verdicts/project-model/verify-run"
FIXTURE="$ROOT/tests/fixtures/verilog-project"
IMAGE="vedit-qt6-build:local"
# The build and state directories live INSIDE the mounted workspace: every
# `docker run` is a fresh container, so a container-side /tmp would not survive
# between the seed and the restore process.
BUILD="$ROOT/target/project-t6/build"
STATE="$ROOT/target/project-t6/state"

mkdir -p "$OUT"
rm -rf "$BUILD" "$STATE"
mkdir -p "$STATE"

header() {
  local file=$1
  {
    echo "# C9 evidence header"
    echo "collected_at: $(date --iso-8601=seconds)"
    echo "git_head: $(git -C "$ROOT" rev-parse HEAD)"
    echo "git_status_porcelain:"
    git -C "$ROOT" status --porcelain | sed 's/^/  /'
    echo "host: $(uname -srm) (docker image $IMAGE; no display server)"
    echo "fixture: $FIXTURE"
    echo "build_dir: $BUILD"
    echo "state_dir: $STATE"
    echo
  } > "$file"
}

echo "== configure + build + in-lane ctest inside $IMAGE =="
header "$OUT/01-standalone-configure.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$IMAGE" bash -lc \
  "cmake -S src/workspace/project -B /vedit-build/target/project-t6/build -G Ninja" 2>&1 | tee -a "$OUT/01-standalone-configure.log"

header "$OUT/02-standalone-build.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$IMAGE" bash -lc \
  "cmake --build /vedit-build/target/project-t6/build" 2>&1 | tee -a "$OUT/02-standalone-build.log"

header "$OUT/03-standalone-ctest.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$IMAGE" bash -lc \
  "QT_QPA_PLATFORM=offscreen ctest --test-dir /vedit-build/target/project-t6/build --output-on-failure" \
  2>&1 | tee -a "$OUT/03-standalone-ctest.log"

echo "== in-process self-check (every CHECK line) =="
header "$OUT/04-selftest-selfcheck.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$IMAGE" bash -lc \
  "QT_QPA_PLATFORM=offscreen /vedit-build/target/project-t6/build/vedit-workspace-project-selftest --selfcheck /vedit-build/tests/fixtures/verilog-project" \
  2>&1 | tee -a "$OUT/04-selftest-selfcheck.log"

echo "== restart persistence: TWO processes sharing $STATE =="
header "$OUT/05-restart-seed.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$IMAGE" bash -lc \
  "QT_QPA_PLATFORM=offscreen /vedit-build/target/project-t6/build/vedit-workspace-project-selftest --seed /vedit-build/tests/fixtures/verilog-project /vedit-build/target/project-t6/state" \
  2>&1 | tee -a "$OUT/05-restart-seed.log"

# The state file written by the seed process is the entire input of the second
# process; copy it into evidence BEFORE the restore run so the verifier can read
# what "members still there" is based on.
header "$OUT/06-persisted-state.json"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$IMAGE" bash -lc \
  "cat /vedit-build/target/project-t6/state/project-model.json" 2>&1 | tee -a "$OUT/06-persisted-state.json"

header "$OUT/07-restart-restore.log"
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$IMAGE" bash -lc \
  "QT_QPA_PLATFORM=offscreen /vedit-build/target/project-t6/build/vedit-workspace-project-selftest --restore /vedit-build/target/project-t6/state 7" \
  2>&1 | tee -a "$OUT/07-restart-restore.log"

echo "== fixture expectations (the values the service is compared against) =="
header "$OUT/08-fixture-expectations.txt"
{
  echo "fixture files:"
  ( cd "$FIXTURE" && find . -type f | sort | sed 's/^/  /' )
  echo "expected/files.txt:"
  sed 's/^/  /' "$FIXTURE/expected/files.txt"
  echo "expected/incdirs.txt:"
  sed 's/^/  /' "$FIXTURE/expected/incdirs.txt"
  echo "expected/defines.txt:"
  sed 's/^/  /' "$FIXTURE/expected/defines.txt"
} >> "$OUT/08-fixture-expectations.txt"

echo "== bad-entry diagnostics (file:line the UI shows) =="
header "$OUT/09-bad-entry-diagnostics.txt"
grep -E 'filelist\.(define_invalid|unknown_token|missing|duplicate_entry)' \
  "$OUT/04-selftest-selfcheck.log" >> "$OUT/09-bad-entry-diagnostics.txt" || true
# The self-check prints CHECK lines only; re-derive the actual strings through
# the service by expanding a deliberately broken project and dumping the model.
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$IMAGE" bash -lc \
  'set -e; B=/tmp/bad-project-t6; rm -rf "$B"; mkdir -p "$B/rtl"
   printf "+define+1BAD=1\n+libext+.v\n-f missing.f\nrtl/a.sv\n" > "$B/project.f"
   printf "module a; endmodule\n" > "$B/rtl/a.sv"
   QT_QPA_PLATFORM=offscreen /vedit-build/target/project-t6/build/vedit-workspace-project-selftest --seed "$B" /vedit-build/target/project-t6/bad-state >/dev/null 2>&1 || true
   cat /vedit-build/target/project-t6/bad-state/project-model.json' >> "$OUT/09-bad-entry-diagnostics.txt" 2>&1 || true

echo
echo "all logs written under $OUT"
grep -h 'PROJECT-MODEL-SELFTEST\|^SEED \|^RESTORE \|tests passed' "$OUT"/0*.log
