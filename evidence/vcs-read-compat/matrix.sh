#!/usr/bin/env bash
# One compatibility matrix: every read command the product issues, run against
# the git on PATH, plus the two legacy spellings as a NEGATIVE CONTROL (they are
# the only places in this evidence directory that name them on purpose).
#
# Usage: matrix.sh <workspace> <label>
# Must run with cwd NOT the workspace (asserted below); all work happens in a
# fresh mktemp fixture.
set -uo pipefail

WORKSPACE=${1:?workspace path}
LABEL=${2:?label}

if [ "$(pwd -P)" = "$WORKSPACE" ]; then
  echo "FATAL: run the matrix outside the VEdit working tree ($WORKSPACE)" >&2
  exit 1
fi

FIXTURE=$(mktemp -d /tmp/vedit-vcs-matrix-XXXXXX) || exit 1
cd "$FIXTURE" || exit 1
echo "label=$LABEL"
echo "cwd=$(pwd -P)"
echo "fixture=$FIXTURE"
echo "git_binary=$(command -v git)"
echo "git_version=$(git --version)"

git init -q repo || { echo "FATAL: git init failed"; exit 1; }
cd repo || exit 1
git config user.email compat@example.com
git config user.name "VEdit Compat"
mkdir -p rtl
printf 'module a;\nendmodule\n' > rtl/a.sv
git add rtl/a.sv
git commit -q -m "matrix fixture"
printf 'module a;\n// changed\nendmodule\n' > rtl/a.sv
printf 'module b;\nendmodule\n' > rtl/b.sv

# run <id> <command...> : prints exit code and the first two output lines.
run() {
  local id=$1
  shift
  local out=""
  local rc=0
  out=$("$@" 2>&1) || rc=$?
  printf '%-26s exit=%-3d | %s\n' "$id" "$rc" "$(printf '%s' "$out" | head -2 | tr '\n' '|')"
}

echo "# read commands the product issues (status / identity / diff)"
run status             git status --porcelain --branch
run rev-parse-abbrev   git rev-parse --abbrev-ref HEAD
run rev-parse-short    git rev-parse --short HEAD
run diff-worktree      git diff --no-color -- rtl/a.sv
run diff-cached        git diff --cached --no-color -- rtl/a.sv
run diff-no-index      git diff --no-index --no-color -- /dev/null rtl/b.sv

echo "# negative control: the legacy spellings the product used to emit"
run legacy-path-option git -C "$PWD" status --porcelain --branch
run legacy-porcelain-v1 git status --porcelain=v1 --branch
echo "# note: diff-no-index exits 1 when the files differ (expected); the product"
echo "#       reads the output and ignores the exit status for that one query."
echo "matrix_done=yes"
