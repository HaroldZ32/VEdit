#!/usr/bin/env bash
# AC-3.0 functional set against the BUNDLED git/svn, inside the clean CentOS 7
# container where the AppDir is mounted at /app. All fixtures live in mktemp -d
# and the probe refuses to run from the VEdit working tree.
set -uo pipefail

APPDIR=${APPDIR:-/app}
GIT=${GIT:-$APPDIR/bin/git}
SVN=${SVN:-$APPDIR/bin/svn}
WORKSPACE=${WORKSPACE:-/vedit-build}

if [ "$(pwd -P)" = "$WORKSPACE" ]; then
  echo "FATAL: the probe must not run inside the VEdit working tree" >&2
  exit 1
fi

fail=0
check() { # <name> <ok: 0|1> <detail>
  if [ "$2" -eq 0 ]; then printf 'CHECK %-34s PASS %s\n' "$1" "${3:-}"; else
    printf 'CHECK %-34s FAIL %s\n' "$1" "${3:-}"; fail=1; fi
}

FIXTURE=$(mktemp -d /tmp/vedit-vcs-toolchain-XXXXXX)
echo "fixture=$FIXTURE"
echo "cwd=$(pwd -P)"
echo "appdir=$APPDIR"
echo "git=$GIT"
echo "svn=$SVN"

echo "== versions =="
git_raw=$("$GIT" --version 2>&1)
git_version=$(printf '%s\n' "$git_raw" | awk '{print $3}')
svn_version=$("$SVN" --version --quiet 2>&1)
echo "git_version=$git_version raw=$git_raw"
echo "svn_version=$svn_version"
printf '%s\n%s\n' "2.43" "$git_version" | sort -V | head -1 | grep -qx "2.43"
check "git>=2.43" $? "$git_version"
printf '%s\n%s\n' "1.14" "$svn_version" | sort -V | head -1 | grep -qx "1.14"
check "svn>=1.14" $? "$svn_version"

echo "== fixture repository =="
cd "$FIXTURE" || { echo "FATAL: cannot enter the fixture" >&2; exit 1; }
"$GIT" init -q repo || { echo "FATAL: git init failed" >&2; exit 1; }
cd repo || exit 1
"$GIT" config user.email toolchain@example.com
"$GIT" config user.name "VEdit Toolchain"
mkdir -p rtl
printf 'module top;\nendmodule\n' > rtl/top.sv
"$GIT" add rtl/top.sv
"$GIT" commit -q -m "fixture commit" || { echo "FATAL: commit failed" >&2; exit 1; }
printf 'module top;\n// changed\nendmodule\n' > rtl/top.sv

echo "== status =="
status=$("$GIT" status --porcelain --branch 2>&1)
echo "$status"
printf '%s\n' "$status" | grep -q '^## ' && printf '%s\n' "$status" | grep -q 'rtl/top.sv'
check "status-branch-and-change" $? "$(printf '%s' "$status" | head -2 | tr '\n' '|')"

echo "== info =="
branch=$("$GIT" rev-parse --abbrev-ref HEAD 2>&1)
revision=$("$GIT" rev-parse --short HEAD 2>&1)
echo "branch=$branch revision=$revision"
[ -n "$branch" ] && [ -n "$revision" ]
check "info-branch-and-revision" $? "$branch @ $revision"

echo "== diff =="
diff_text=$("$GIT" diff --no-color -- rtl/top.sv 2>&1)
printf '%s\n' "$diff_text" | grep -q '+// changed'
check "diff-shows-change" $? "$(printf '%s' "$diff_text" | head -1)"

echo "== refs =="
refs=$("$GIT" for-each-ref --format='%(refname)' 2>&1)
printf '%s\n' "$refs" | grep -qx 'refs/heads/master'
check "refs-list" $? "$(printf '%s' "$refs" | tr '\n' ' ')"

echo "== history =="
history=$("$GIT" log --oneline 2>&1)
printf '%s\n' "$history" | grep -q 'fixture commit'
check "history-log" $? "$(printf '%s' "$history" | head -1)"

echo "== workspace =="
toplevel=$("$GIT" rev-parse --show-toplevel 2>&1)
[ "$toplevel" = "$(pwd -P)" ]
check "workspace-toplevel" $? "$toplevel"

echo "== worktree add/list/remove =="
"$GIT" worktree add -q ../wt HEAD
add_ok=$?
echo "worktree_add_exit=$add_ok"
"$GIT" worktree list > worktree-list.txt 2>&1
cat worktree-list.txt
grep -q '/wt ' worktree-list.txt
check "worktree-add-list" $?
"$GIT" worktree remove ../wt
remove_ok=$?
"$GIT" worktree list > worktree-after.txt 2>&1
! grep -q '/wt ' worktree-after.txt
check "worktree-remove" $? "add=$add_ok remove=$remove_ok"

echo "== svn =="
svn_full=$("$SVN" --version 2>&1 | head -1)
echo "$svn_full"
printf '%s' "$svn_full" | grep -q '1\.14\.5'
check "svn-binary-runs" $? "$svn_full"

echo "== svnadmin must not be shipped =="
if [ -n "$(find "$APPDIR" -name 'svnadmin' -print -quit 2>/dev/null)" ]; then
  check "svnadmin-absent" 1 "found in the AppDir"
else
  check "svnadmin-absent" 0 "build-only"
fi

echo "fixture_kept=$FIXTURE"
echo "ac30_fail=$fail"
exit "$fail"
