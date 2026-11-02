#!/usr/bin/env bash
# T40 (independent review of board task T39 / fix-filetree-open) — read-only rerun
# of the file tree lane's acceptance suite and of the root-context seamcheck that
# asserts the REAL ApplicationShell editor state after a double-click.
#
#   bash evidence/verdicts/fix-filetree-open/run-review-seamcheck.sh green
#   bash evidence/verdicts/fix-filetree-open/run-review-seamcheck.sh before <src-copy-root>
#
# `green` uses the workspace source at the current HEAD+working tree.
# `before` uses <src-copy-root> (a throwaway copy of src/ with the two
# filetree_integration.* files restored to HEAD) — it exists to falsify the test,
# never to modify the workspace.
#
# Every build happens in container-local /tmp, so nothing outside
# evidence/verdicts/fix-filetree-open/ is written on the host.
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
mode="${1:-green}"
case "$mode" in
  green)  SRC="$ROOT" ;;
  before) SRC="${2:?before needs the path of the source copy}"; [ -d "$SRC/src/workspace/filetree" ] || { echo "bad copy root: $SRC" >&2; exit 2; } ;;
  *) echo "usage: $0 <green|before> [src-copy-root]" >&2; exit 2 ;;
esac

docker run --rm -i \
  -v "$SRC":/src -w /src \
  -e HOME=/tmp -e LANG=C.UTF-8 \
  -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg \
  -e REVIEW_MODE="$mode" \
  vedit-qt6-build:local bash -s <<'EOS'
set -uo pipefail
mkdir -p /tmp/xdg /tmp/fx
status=0
echo "== toolchain =="
echo "qt6-base-dev: $(dpkg-query -W qt6-base-dev 2>/dev/null || true)"
echo "cmake: $(cmake --version | head -1)"
echo "g++: $(g++ --version | head -1)"
echo "src root: /src"
echo
echo "== 1/4 lane standalone build =="
cmake -S /src/src/workspace/filetree -B /tmp/lane -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -5 || status=1
cmake --build /tmp/lane --parallel "$(nproc)" 2>&1 | tail -5 || status=1
echo
echo "== 2/4 lane ctest (the 111-check acceptance suite) =="
ctest --test-dir /tmp/lane --output-on-failure 2>&1 | tail -8 || status=1
echo
echo "== 3/4 fixture (the lane own --prepare) =="
/tmp/lane/vedit-filetree-demo --prepare /tmp/fx --big-count 200 || status=1
echo
echo "== 4/4 root-context seamcheck (REAL ApplicationShell) =="
cmake -S /src/src/workspace/filetree/tools/rootcheck -B /tmp/rc -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -6 || status=1
cmake --build /tmp/rc --target vedit-filetree-seamcheck --parallel "$(nproc)" 2>&1 | tail -6 || status=1
/tmp/rc/vedit-filetree-seamcheck \
  --project /tmp/fx/project \
  --state /tmp/fx/state \
  --screenshot /tmp/shot.png || status=1
echo "SEAMCHECK $REVIEW_MODE exit=$status"
exit "$status"
EOS
