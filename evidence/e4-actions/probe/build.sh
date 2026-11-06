#!/usr/bin/env bash
# Build the T15 editor-actions probe against the CURRENT tree, inside the pinned
# image (run from /vedit-build, i.e. the workspace root mounted there):
#
#   docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
#     bash -lc 'bash evidence/e4-actions/probe/build.sh'
#
# The probe is an out-of-tree binary by the repository's probe convention (AGENT.md
# §4 / docs/handover CELL-20261102-STATUS §6.1): the compile and link flags are
# taken from ninja's own commands for `src/app/vedit-app`, so the probe links the
# same product static libraries and the same Qt6/KF6 set the product does. The only
# edits are the source/object substitution and the extra Qt6Test link (QTest key
# events); -Wl,--dependency-file is dropped so the product's own link dependency
# file is not rewritten by a probe link.
set -euo pipefail

ROOT=/vedit-build
BUILD="$ROOT/target/qt6-build"
OUT="$ROOT/target/e4-actions-probe"
PROBE="$ROOT/evidence/e4-actions/probe/editor_actions_probe.cpp"
OBJECT="$OUT/editor_actions_probe.o"
BINARY="$OUT/editor-actions-probe"
QT6TEST=$(ls /usr/lib/x86_64-linux-gnu/libQt6Test.so.* 2>/dev/null | head -1)

mkdir -p "$OUT"
command -v /usr/bin/c++ >/dev/null
if [ -z "$QT6TEST" ]; then
  echo "FATAL: libQt6Test is missing from the build image" >&2
  exit 3
fi

commands=$(ninja -C "$BUILD" -t commands src/app/vedit-app)
compile=$(printf '%s\n' "$commands" | grep -F -- '-c /vedit-build/src/app/main.cpp' | tail -n 1)
link=$(printf '%s\n' "$commands" | grep -F -- '-o src/app/vedit-app ' | tail -n 1)

if [ -z "$compile" ] || [ -z "$link" ]; then
  echo "FATAL: could not extract the vedit-app compile/link commands from ninja" >&2
  exit 3
fi

# Compile: keep the product's own flags, drop the dependency-file bookkeeping
# (it names the product's object), substitute our source and object.
flags=${compile%% -MD *}
eval "$flags -isystem /usr/include/x86_64-linux-gnu/qt6/QtTest -o '$OBJECT' -c '$PROBE'"

# Link: same link line, our object in place of main.cpp.o, Qt6Test appended.
link=${link#: && }
link=${link% && :}
link=${link//src\/app\/CMakeFiles\/vedit-app.dir\/main.cpp.o/$OBJECT}
link=${link//-Wl,--dependency-file=src\/app\/CMakeFiles\/vedit-app.dir\/link.d/}
link=${link//-o src\/app\/vedit-app /-o $BINARY }
( cd "$BUILD" && eval "$link '$QT6TEST'" )

echo "PROBE_BUILD_OK binary=$BINARY"
stat -c 'binary %y %s %n' "$BINARY"
sha256sum "$BINARY"
