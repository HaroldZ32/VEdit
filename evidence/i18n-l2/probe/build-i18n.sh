#!/usr/bin/env bash
# Build the T38 i18n L2 acceptance probe against the CURRENT tree, inside the
# pinned image (run from /vedit-build, i.e. the workspace root mounted there):
#
#   docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
#     bash -lc 'bash evidence/i18n-l2/probe/build-i18n.sh'
#
# The compile/link flags are ninja's own commands for `src/app/vedit-app`, so the
# probe links exactly the product's static libraries and Qt6/KF6 set; only the
# source/object substitution and the dropped dependency-file bookkeeping differ.
set -euo pipefail

ROOT=/vedit-build
BUILD="$ROOT/target/qt6-build"
OUT="$ROOT/target/i18n-probe"
PROBE="$ROOT/evidence/i18n-l2/probe/i18n_probe.cpp"
OBJECT="$OUT/i18n_probe.o"
BINARY="$OUT/i18n-probe"

mkdir -p "$OUT"
command -v /usr/bin/c++ >/dev/null

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
