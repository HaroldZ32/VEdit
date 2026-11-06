#!/usr/bin/env bash
# Build the T16 advanced-features probe against the CURRENT tree, inside the
# pinned image (run from /vedit-build, i.e. the workspace root mounted there):
#
#   docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
#     bash -lc 'bash evidence/e5-advanced/probe/build.sh'
#
# Same probe convention as evidence/e4-actions/probe/build.sh: the compile and
# link flags are ninja's own commands for `src/app/vedit-app`, so the probe links
# the same product static libraries and the same Qt6/KF6 set; only the
# source/object substitution and the extra Qt6Test link differ.
set -euo pipefail

ROOT=/vedit-build
BUILD="$ROOT/target/qt6-build"
OUT="$ROOT/target/e5-advanced-probe"
PROBE="$ROOT/evidence/e5-advanced/probe/advanced_probe.cpp"
OBJECT="$OUT/advanced_probe.o"
BINARY="$OUT/advanced-probe"
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

flags=${compile%% -MD *}
eval "$flags -isystem /usr/include/x86_64-linux-gnu/qt6/QtTest -o '$OBJECT' -c '$PROBE'"

link=${link#: && }
link=${link% && :}
link=${link//src\/app\/CMakeFiles\/vedit-app.dir\/main.cpp.o/$OBJECT}
link=${link//-Wl,--dependency-file=src\/app\/CMakeFiles\/vedit-app.dir\/link.d/}
link=${link//-o src\/app\/vedit-app /-o $BINARY }
( cd "$BUILD" && eval "$link '$QT6TEST'" )

echo "PROBE_BUILD_OK binary=$BINARY"
stat -c 'binary %y %s %n' "$BINARY"
sha256sum "$BINARY"
