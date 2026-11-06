#!/usr/bin/env bash
# 7c clean-container E2E on the AppDir that packaging/runtime/build.sh extracted
# from the produced AppImage (packaging/runtime/out/centos7-appdir).
#
# What this ADDS to build.sh's own step 7/7 (evidence/packaging-2/70-centos7-probe.log):
#   * `VCS-TOOLCHAIN ... source=bundled` on the packaged product's stdout;
#   * an ORDINARY start (no --project) whose root comes only from the persisted
#     session archive, with all four lanes logging the restored root;
#   * the bundled VCS client versions again, next to those lines.
#
# Run from the repository root:  bash evidence/packaging-2/scripts/clean-container-e2e.sh
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
EVID="$ROOT/evidence/packaging-2"
OUT="$ROOT/packaging/runtime/out"
APP="$OUT/centos7-appdir"
APPIMAGE="$OUT/VEdit-0.1.1-x86_64.AppImage"
CENTOS_IMAGE="docker.m.daocloud.io/library/centos@sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4"
LOG="$EVID/72-clean-container-e2e.log"
mkdir -p "$EVID"

exec > >(tee "$LOG") 2>&1

echo "# 7c clean-container E2E — CentOS Linux 7.9.2009, glibc 2.17"
echo "captured_at: $(date --iso-8601=seconds)"
echo "appdir: $APP"
[ -d "$APP" ] || { echo "FATAL: $APP is missing; run bash packaging/runtime/build.sh first" >&2; exit 2; }
echo "appimage: $APPIMAGE  bytes=$(stat -c%s "$APPIMAGE" 2>/dev/null || echo missing)"
echo "appimage_sha256=$(sha256sum "$APPIMAGE" 2>/dev/null | cut -d' ' -f1)"

fail=0

echo
echo "== 0. container facts =="
docker run --rm "$CENTOS_IMAGE" bash -lc 'cat /etc/centos-release; ldd --version | head -1' || fail=1

echo
echo "== 1. bundled VCS clients in the extracted AppDir =="
docker run --rm -v "$APP:/app:ro" "$CENTOS_IMAGE" bash -lc '
  set -u
  /app/bin/git --version; echo "git_exit=$?"
  /app/bin/svn --version --quiet; echo "svn_exit=$?"
  find /app -name svnadmin -print -quit | grep -q . && echo "svnadmin=PRESENT" || echo "svnadmin=absent"
' || fail=1

echo
echo "== 2. ordinary start (NO --project): persisted root -> panels + VCS-TOOLCHAIN =="
docker run --rm -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -e VEDIT_DEBUG=1 \
  -v "$APP:/app:ro" "$CENTOS_IMAGE" bash -lc '
  set -u
  rm -rf /tmp/proj /tmp/state
  mkdir -p /tmp/proj/rtl /tmp/state
  printf "module real_top;\nendmodule\n" > /tmp/proj/rtl/real.sv
  printf %s "{\"version\":2,\"project\":\"/tmp/proj\",\"panels\":[\"file-tree\",\"search\",\"vcs\",\"module-tree\"]}" > /tmp/state/session.json
  echo "session.json: $(cat /tmp/state/session.json)"
  echo "--- product stdout/stderr (filtered) ---"
  timeout 120 /app/AppRun --state-dir /tmp/state --hold 3000 > /tmp/start.log 2>&1
  rc=$?
  echo "ordinary_start_exit=$rc"
  grep -E "VCS-TOOLCHAIN|filetree: installFileTree|search: root=|vcs: root=|module-tree: installed|TOOLCHAIN source=|KSH-GUARD" /tmp/start.log || true
  echo "--- lane root lines (exact) ---"
  grep -cE "filetree: installFileTree projectRoot=.\/tmp\/proj" /tmp/start.log | sed "s/^/filetree_root_hits=/"
  grep -cE "search: root=.\/tmp\/proj" /tmp/start.log | sed "s/^/search_root_hits=/"
  grep -cE "vcs: root=.\/tmp\/proj" /tmp/start.log | sed "s/^/vcs_root_hits=/"
  grep -cE "module-tree: installed \(root=.\/tmp\/proj" /tmp/start.log | sed "s/^/module_tree_root_hits=/"
  grep -q "VCS-TOOLCHAIN .*source=bundled" /tmp/start.log && echo "vcs_toolchain_bundled=yes" || echo "vcs_toolchain_bundled=NO"
  cp /tmp/start.log /tmp/ordinary-start-full.log 2>/dev/null || true
  echo "--- end ---"
  exit "$rc"
' || fail=1

echo
echo "== 3. AppRun --selfcheck in the same clean container =="
docker run --rm -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -v "$APP:/app:ro" "$CENTOS_IMAGE" \
  bash -lc '/app/AppRun --selfcheck 2>&1 | tail -3; echo "selfcheck_exit=${PIPESTATUS[0]}"' || fail=1

echo
sleep 1 # let the tee child flush before the assertions read the log back
echo "== assertions =="
assert_grep() { # pattern file label
  if grep -qE "$1" "$LOG"; then echo "CHECK PASS  $3"; else echo "CHECK FAIL  $3"; fail=1; fi
}
assert_grep 'CentOS Linux release 7\.9\.2009' "$LOG" "container is CentOS 7.9.2009"
assert_grep 'GNU libc\) 2\.17' "$LOG" "container glibc is 2.17"
assert_grep '^git version 2\.53\.0$' "$LOG" "bundled git --version = 2.53.0"
assert_grep '^1\.14\.5$' "$LOG" "bundled svn --version --quiet = 1.14.5"
assert_grep '^svnadmin=absent$' "$LOG" "svnadmin is not shipped"
assert_grep '^ordinary_start_exit=0$' "$LOG" "ordinary start (no --project) exit 0"
assert_grep 'VCS-TOOLCHAIN .*source=bundled' "$LOG" "VCS-TOOLCHAIN ... source=bundled"
assert_grep '^filetree_root_hits=[1-9]' "$LOG" "filetree lane received the restored root"
assert_grep '^search_root_hits=[1-9]' "$LOG" "search lane received the restored root"
assert_grep '^vcs_root_hits=[1-9]' "$LOG" "vcs lane received the restored root"
assert_grep '^module_tree_root_hits=[1-9]' "$LOG" "module tree received the restored root"
assert_grep 'APP-SHELL-SELFTEST ok=1' "$LOG" "AppRun --selfcheck ok=1 in the clean container"

echo
echo "CLEAN-CONTAINER-E2E steps_failed=$fail"
[ "$fail" -eq 0 ] && echo "CLEAN-CONTAINER-E2E PASS" || echo "CLEAN-CONTAINER-E2E FAIL"
exit "$fail"
