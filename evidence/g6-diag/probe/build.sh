#!/usr/bin/env bash
# Build the AC-8.1 probe against the CURRENT tree. Run this INSIDE the pinned
# image (the host wrapper run.sh does that for you):
#
#   docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/vedit-build -w /vedit-build \
#     vedit-qt6-build:local bash -lc 'bash evidence/g6-diag/probe/build.sh'
#
# Out-of-tree binary by the repository's probe convention (AGENT.md §4): the
# compile/link flags are taken verbatim from ninja's own commands for
# `src/app/vedit-app`, so the probe links the same product static libraries and
# the same Qt6/KF6 set. Only the source/object substitution is made, and
# `-Wl,--dependency-file=` is dropped so the product's own link dependency file
# is not rewritten by a probe link.
set -euo pipefail

ROOT=/vedit-build
BUILD="$ROOT/target/qt6-build"
OUT="$ROOT/target/g6-diag-probe"
PROBE="$ROOT/evidence/g6-diag/probe/g6_diag_probe.cpp"
OBJECT="$OUT/g6_diag_probe.o"
BINARY="$OUT/g6-diag-probe"

mkdir -p "$OUT"

# 1. The product libraries the probe links must be the tree's current ones.
ninja -C "$BUILD" src/app/vedit-app

commands=$(ninja -C "$BUILD" -t commands src/app/vedit-app)
compile=$(printf '%s\n' "$commands" | grep -F -- '-c /vedit-build/src/app/main.cpp' | tail -n 1)
link=$(printf '%s\n' "$commands" | grep -F -- '-o src/app/vedit-app ' | tail -n 1)
if [ -z "$compile" ] || [ -z "$link" ]; then
  echo "FATAL: could not extract the vedit-app compile/link commands from ninja" >&2
  exit 3
fi

flags=${compile%% -MD *}
eval "$flags -o '$OBJECT' -c '$PROBE'"

link=${link#: && }
link=${link% && :}
link=${link//src\/app\/CMakeFiles\/vedit-app.dir\/main.cpp.o/$OBJECT}
link=${link//-Wl,--dependency-file=src\/app\/CMakeFiles\/vedit-app.dir\/link.d/}
link=${link//-o src\/app\/vedit-app /-o $BINARY }
( cd "$BUILD" && eval "$link" )

echo "PROBE_BUILD_OK binary=$BINARY"
stat -c 'binary %y %s %n' "$BINARY"
sha256sum "$BINARY"
