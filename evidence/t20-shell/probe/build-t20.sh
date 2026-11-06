#!/usr/bin/env bash
# Build the T20 shell-integration probe against the CURRENT tree, inside the
# pinned image (run from /vedit-build, i.e. the workspace root mounted there):
#
#   docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
#     bash -lc 'bash evidence/t20-shell/probe/build-t20.sh'
#
# The probe is an out-of-tree binary by the repository's probe convention
# (AGENT.md §4 / docs/handover CELL-20261102-STATUS §6.1): the compile and link
# flags are taken from ninja's own commands for `src/app/vedit-app`, so the probe
# links exactly the product's static libraries (app shell + all workspace lanes +
# editor core + the Rust C ABI) and its Qt6/KF6 set. The only edits are the
# source/object substitution, the Qt6Test include/link (QTest mouse events) and
# dropping -Wl,--dependency-file so the product's link dependency file is not
# rewritten by a probe link.
set -euo pipefail

ROOT=/vedit-build
BUILD="$ROOT/target/qt6-build"
OUT="$ROOT/target/t20-probe"
PROBE="$ROOT/evidence/t20-shell/probe/t20_shell_probe.cpp"
OBJECT="$OUT/t20_shell_probe.o"
BINARY="$OUT/t20-shell-probe"
QT6TEST_INCLUDE=/usr/include/x86_64-linux-gnu/qt6/QtTest
QT6TEST_LIB=/usr/lib/x86_64-linux-gnu/libQt6Test.so.6.10.2

mkdir -p "$OUT"
command -v /usr/bin/c++ >/dev/null

commands=$(ninja -C "$BUILD" -t commands src/app/vedit-app)
compile=$(printf '%s\n' "$commands" | grep -F -- '-c /vedit-build/src/app/main.cpp' | tail -n 1)
link=$(printf '%s\n' "$commands" | grep -F -- '-o src/app/vedit-app ' | tail -n 1)

if [ -z "$compile" ] || [ -z "$link" ]; then
  echo "FATAL: could not extract the vedit-app compile/link commands from ninja" >&2
  exit 3
fi

# Compile: the product's own flags, minus the dependency-file bookkeeping.
flags=${compile%% -MD *}
eval "$flags -isystem '$QT6TEST_INCLUDE' -o '$OBJECT' -c '$PROBE'"

# Link: same link line, our object in place of main.cpp.o, Qt6Test appended.
link=${link#: && }
link=${link% && :}
link=${link//src\/app\/CMakeFiles\/vedit-app.dir\/main.cpp.o/$OBJECT}
link=${link//-Wl,--dependency-file=src\/app\/CMakeFiles\/vedit-app.dir\/link.d/}
link=${link//-o src\/app\/vedit-app /-o $BINARY }
( cd "$BUILD" && eval "$link '$QT6TEST_LIB'" )

echo "PROBE_BUILD_OK binary=$BINARY"
stat -c 'binary %y %s %n' "$BINARY"
sha256sum "$BINARY"
