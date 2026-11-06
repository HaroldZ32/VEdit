#!/usr/bin/env bash
# Build the RM-4 jump probe against the CURRENT tree, inside the pinned Qt6/KF6
# image (run from /vedit-build, i.e. the workspace root mounted there):
#
#   docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
#     vedit-qt6-build:local bash -lc 'bash evidence/ux-batch-2/rm4-jump/probe/build.sh'
#
# The probe is an out-of-tree binary by the repository's probe convention: the
# compile and link flags are taken from ninja's own commands for
# `src/app/vedit-app`, so the probe links the same product static libraries and
# Qt6/KF6 set the product does. The only two edits are the source/object
# substitution and the extra Qt6Test link (QTest mouse events);
# -Wl,--dependency-file is dropped so the probe link does not rewrite the
# product's own link dependency file.
set -euo pipefail

ROOT=/vedit-build
BUILD="$ROOT/target/qt6-build"
OUT="$ROOT/target/rm4-jump-probe"
PROBE="$ROOT/evidence/ux-batch-2/rm4-jump/probe/rm4_probe.cpp"
OBJECT="$OUT/rm4_probe.o"
BINARY="$OUT/rm4-jump-probe"
QT6TEST=$(ls /usr/lib/x86_64-linux-gnu/libQt6Test.so* 2>/dev/null | head -n 1)

if [ -z "$QT6TEST" ]; then
  echo "FATAL: libQt6Test not found in the pinned image" >&2
  exit 3
fi

mkdir -p "$OUT"

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
