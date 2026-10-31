#!/usr/bin/env bash
# Rehearsal of the ONE line the root build needs to compile this lane (task T35).
#
#   bash evidence/term-panel/run-root-rehearsal.sh
#
# The root CMakeLists.txt (T63) includes lanes through the explicit
# VEDIT_LANE_DIRECTORIES list, and that list is outside this lane's write scope.
# So the rehearsal copies the tree to target/term-rehearsal/, adds `src/terminal`
# to the COPY's list, and builds/tests the result in the pinned image. The
# repository itself is never edited: the copy lives under the gitignored target/.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EVID="$ROOT/evidence/term-panel"
IMAGE="${VEDIT_QT6_BUILD_IMAGE:-vedit-qt6-build:local}"
COPY=target/term-rehearsal
BUILD=target/term-rehearsal-build

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "FATAL: the pinned build image '$IMAGE' is missing; build it with bash scripts/build-qt.sh" >&2
  exit 1
fi

rm -rf "$ROOT/$COPY" "$ROOT/$BUILD"
mkdir -p "$ROOT/$COPY"
rsync -a --exclude '.git' --exclude 'target' --exclude 'packaging' --exclude 'evidence' \
  --exclude '.codegraph' --exclude '.mpd' "$ROOT/" "$ROOT/$COPY/"

python3 - "$ROOT/$COPY/CMakeLists.txt" <<'PY'
import sys

path = sys.argv[1]
text = open(path).read()
needle = "  src/syntax/kde        # syn-kde-data lane (T16), added once it lands\n)"
if needle not in text:
    raise SystemExit("FATAL: the lane list in the copied CMakeLists.txt has changed; update this rehearsal")
text = text.replace(needle, needle[:-1] + "  src/terminal          # term-panel lane (T35) -- THE ONE LINE THIS REHEARSAL ADDS\n)")
open(path, "w").write(text)
PY

user_args=()
if ! docker info --format '{{json .SecurityOptions}}' 2>/dev/null | grep -q '"name=rootless"'; then
  user_args=(--user "$(id -u):$(id -g)")
fi

docker run --rm "${user_args[@]}" \
  -v "$ROOT":/vedit-build \
  -w /vedit-build \
  -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime \
  -e LANG=C.UTF-8 \
  -e BUILD="$BUILD" \
  "$IMAGE" bash -lc '
set -o pipefail
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime
EVID=/vedit-build/evidence/term-panel
{
  echo "## Rehearsal of the root-build wire-up (this lane edits no repository file)"
  echo "## A copy of the tree under target/term-rehearsal/ adds ONE line to CMakeLists.txt:"
  echo "##     VEDIT_LANE_DIRECTORIES += src/terminal          # T63 mechanism"
  echo
  echo "### configure"
  cmake -S target/term-rehearsal -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 \
    | grep -E "lane directory|building src/app|Configuring done|Generating done|Error"
  echo "configure exit: ${PIPESTATUS[0]}"
  echo
  echo "### build (every lane + src/app + src/terminal, warnings are errors)"
  cmake --build "$BUILD" --parallel "$(nproc)" 2>&1 | tail -6
  echo "build exit: ${PIPESTATUS[0]}"
  echo
  echo "### ctest of the whole root project"
  QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD" --output-on-failure 2>&1 | tail -18
  echo "ctest exit: ${PIPESTATUS[0]}"
  echo
  echo "### binaries produced by the root build"
  ls -l "$BUILD/src/terminal/vedit-terminal-selftest" "$BUILD/src/app/vedit-app" 2>&1
} 2>&1 | tee "$EVID/09-root-build-with-lane.log"
'
