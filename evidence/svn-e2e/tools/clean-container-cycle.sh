#!/usr/bin/env bash
# Batch 6b (AC-6.2 / AC-6.4): the real Subversion working-copy cycle INSIDE the
# pinned CentOS 7.9 (glibc 2.17) build image, with the BUNDLED svn through its
# AppDir loader wrapper and the build-only svnadmin.
#
#   bash evidence/svn-e2e/tools/clean-container-cycle.sh
#
# This is the "构建镜像内 svnadmin create 造库 + svn checkout 工作复制" evidence:
# every command and its output is recorded, and the whole cycle runs where the
# deliverable runs (CentOS 7 / glibc 2.17), not only on the host.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
LOG="$ROOT/evidence/svn-e2e/logs/40-clean-container-cycle.log"
mkdir -p "$(dirname "$LOG")"

docker run --rm -v "$ROOT:/vedit-build:ro" -w /tmp vedit-build:local bash -lc '
set -u
echo "# batch 6b: bundled svn working-copy cycle inside the pinned build image"
echo "container: $(cat /etc/redhat-release 2>/dev/null || echo unknown)"
echo "glibc: $(ldd --version | head -1)"
SVN=/vedit-build/packaging/runtime/out/AppDir/bin/svn
ADMIN=/vedit-build/build/vcs-tools/svnadmin
echo "svn=$(  "$SVN" --version --quiet)"
echo "svnadmin=$("$ADMIN" --version --quiet)"
echo "wrapper runs under: $(head -1 "$SVN")"
rm -rf /tmp/c6b && mkdir -p /tmp/c6b && cd /tmp/c6b
step() { echo; echo "\$ $*"; "$@"; echo "  exit=$?"; }
step "$ADMIN" create repo
step "$SVN" checkout -q file:///tmp/c6b/repo wc
cd wc
printf "module a;\nendmodule\n" > a.sv
step "$SVN" add a.sv
step "$SVN" commit -q -m "clean container base"
step "$SVN" status
step "$SVN" info --xml
printf "module a;\n// changed\nendmodule\n" > a.sv
step "$SVN" status
step "$SVN" diff --internal-diff a.sv
step "$SVN" revert a.sv
step "$SVN" status
cd /tmp/c6b
step "$SVN" checkout -q file:///tmp/c6b/repo wc2
cd wc2
printf "module b;\nendmodule\n" > b.sv
step "$SVN" add b.sv
step "$SVN" commit -q -m "second working copy"
cd /tmp/c6b/wc
step "$SVN" update
step "$SVN" status
step "$SVN" remove b.sv
step "$SVN" status
step "$SVN" commit -q -m "remove b"
step "$SVN" log -q file:///tmp/c6b/repo
echo
echo "result=PASS"
' 2>&1 | tee "$LOG"
