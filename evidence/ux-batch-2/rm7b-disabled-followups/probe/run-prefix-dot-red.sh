#!/usr/bin/env bash
# RM-7b M2 RED run: execute the FINAL disabled-look selftest against the CURRENT
# tree with ONLY the M2 dot change removed (the pre-RM-7b delegate), without
# touching the shared working tree.
#
#   bash evidence/ux-batch-2/rm7b-disabled-followups/probe/run-prefix-dot-red.sh
#
# Runs inside the pinned Qt6/KF6 image, workspace mounted at /vedit-build. The
# pre-fix delegate is the current file with the two M2 blocks (anonymous
# blendToward helper + State_Enabled gating) removed. It is compiled with the
# product's own flags, swapped into a scratch copy of libvedit_file_tree.a, and
# the selftest is linked against that archive; everything else is the current
# tree.
set -euo pipefail
ROOT=/vedit-build
BUILD="$ROOT/target/qt6-build"
OUT="$ROOT/target/rm7b-prefix"
SRC="$OUT/file_status_delegate_prefix.cpp"
OBJ="$OUT/file_status_delegate_prefix.o"
LIB="$OUT/libvedit_file_tree_prefix.a"
BIN="$OUT/vedit-app-disabled-look-selftest-prefix-dot"
mkdir -p "$OUT"

python3 - "$ROOT/src/workspace/filetree/file_status_delegate.cpp" "$SRC" <<'PY'
import sys

source, target = sys.argv[1], sys.argv[2]
text = open(source, encoding="utf-8").read()

helper = '''namespace {

// RM-7b: a disabled row's dot must dim exactly like the rest of the row. The
// same rule the theme palette uses for its Disabled group: move the semantic
// colour 55% of the way toward the surface it is painted on. Keeping the hue
// (red/amber/green/grey) means the status stays readable, only muted.
constexpr int kDisabledDotBlendPercent = 55;

QColor blendToward(const QColor &color, const QColor &surface, int percent)
{
    const qreal weight = qBound(0, percent, 100) / 100.0;
    return QColor(qRound(color.red() * (1.0 - weight) + surface.red() * weight),
                  qRound(color.green() * (1.0 - weight) + surface.green() * weight),
                  qRound(color.blue() * (1.0 - weight) + surface.blue() * weight));
}

} // namespace

'''
if helper not in text:
    sys.exit("RM-7b RED: M2 blend helper not found; the tree changed")
text = text.replace(helper, "", 1)

dimmed = '''    // RM-7b: honour the row's enabled state for the hand-painted dot. Before
    // this, a disabled row kept a full-strength dot while its icon and text
    // dimmed (the RM-9 states are unchanged: the ring shape below still uses
    // this colour, only dimmed when the row is disabled).
    QColor paintedDot = dotColor(status);
    if ((opt.state & QStyle::State_Enabled) == 0) {
        const QColor surface = (opt.state & QStyle::State_Selected)
                                   ? opt.palette.color(QPalette::Highlight)
                                   : opt.palette.color(QPalette::Base);
        paintedDot = blendToward(paintedDot, surface, kDisabledDotBlendPercent);
    }
    lastDotColor_ = paintedDot;
'''
prefix = '''    lastDotColor_ = dotColor(status);
'''
if dimmed not in text:
    sys.exit("RM-7b RED: M2 State_Enabled block not found; the tree changed")
text = text.replace(dimmed, prefix, 1)
open(target, "w", encoding="utf-8").write(text)
print("PREFIX-DELEGATE m2_removed=2")
PY
sha256sum "$ROOT/src/workspace/filetree/file_status_delegate.cpp" "$SRC"

commands=$(ninja -C "$BUILD" -t commands src/app/workspace-lane-filetree/CMakeFiles/vedit_file_tree.dir/file_status_delegate.cpp.o)
compile=$(printf '%s\n' "$commands" | grep -F -- '-c /vedit-build/src/workspace/filetree/file_status_delegate.cpp' | tail -n 1)
if [ -z "$compile" ]; then
  echo "FATAL: could not extract the delegate compile command" >&2
  exit 3
fi
flags=${compile%% -MD *}
eval "$flags -o '$OBJ' -c '$SRC'"

cp "$BUILD/src/app/workspace-lane-filetree/libvedit_file_tree.a" "$LIB"
ar d "$LIB" file_status_delegate.cpp.o
ar r "$LIB" "$OBJ"

link_commands=$(ninja -C "$BUILD" -t commands src/app/vedit-app-disabled-look-selftest)
link=$(printf '%s\n' "$link_commands" | grep -F -- '-o src/app/vedit-app-disabled-look-selftest ' | tail -n 1)
if [ -z "$link" ]; then
  echo "FATAL: could not extract the selftest link command" >&2
  exit 3
fi
link=${link#: && }
link=${link% && :}
link=${link//src\/app\/workspace-lane-filetree\/libvedit_file_tree.a/$LIB}
link=${link//-Wl,--dependency-file=src\/app\/CMakeFiles\/vedit-app-disabled-look-selftest.dir\/link.d/}
link=${link//-o src\/app\/vedit-app-disabled-look-selftest /-o $BIN }
( cd "$BUILD" && eval "$link" )

export XDG_RUNTIME_DIR=/tmp/xdg-rm7b-prefix
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
set +e
env QT_QPA_PLATFORM=offscreen "$BIN"
status=$?
set -e
echo "RM7B-PREFIX-DOT-RED exit=$status"
exit "$status"
