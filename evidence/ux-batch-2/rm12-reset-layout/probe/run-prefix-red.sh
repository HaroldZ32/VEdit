#!/usr/bin/env bash
# RM-12 RED run: execute the FINAL selftest against the CURRENT tree with ONLY
# the four RM-12 guards removed (the pre-fix handler shapes), without touching
# the shared working tree.
#
#   bash evidence/ux-batch-2/rm12-reset-layout/probe/run-prefix-red.sh
#
# Runs inside the pinned Qt6/KF6 image, mounted at /vedit-build (the workspace
# root). It (1) copies src/app/app_shell.cpp, (2) deletes exactly the four
# confirmUnsavedDocuments() guards added by RM-12, (3) compiles that one source
# with the product's own flags from ninja, (4) builds a scratch copy of
# libvedit_app_shell.a whose app_shell.cpp.o is the pre-fix object, (5) links the
# selftest against it and (6) runs it. The RED result is the selftest's own
# failure output; every other source file is exactly the tree's current one.
set -euo pipefail
ROOT=/vedit-build
BUILD="$ROOT/target/qt6-build"
OUT="$ROOT/target/rm12-prefix"
SRC="$OUT/app_shell_prefix.cpp"
OBJ="$OUT/app_shell_prefix.o"
LIB="$OUT/libvedit_app_shell_prefix.a"
BIN="$OUT/vedit-app-reset-layout-guard-selftest-prefix"
mkdir -p "$OUT"

python3 - "$ROOT/src/app/app_shell.cpp" "$SRC" <<'PY'
import sys

source, target = sys.argv[1], sys.argv[2]
text = open(source, encoding="utf-8").read()

guards = {
    "file.new-layout": '''        // RM-12: resetLayout() deletes every pane AND the document that dies
        // with it (KTextEditor bounds the document to its view). The user must
        // answer the SAME unsaved-document question the window close asks; a
        // cancelled answer leaves every buffer and the layout untouched.
        if (!confirmUnsavedDocuments()) {
            return;
        }
''',
    "view.layout.mixed": '''        // RM-12: the mixed preset starts from resetLayout(), i.e. it destroys
        // every pane and document exactly like Reset Layout. Same confirmation;
        // a cancelled answer must not even add panes to the current layout.
        if (!confirmUnsavedDocuments()) {
            return;
        }
''',
    "view.layout.reset": '''        // RM-12: see file.new-layout above. Cancel keeps every dirty document
        // and the layout byte-for-byte (the check asserts the layout JSON).
        if (!confirmUnsavedDocuments()) {
            return;
        }
''',
}
for name, guard in guards.items():
    if guard not in text:
        sys.exit(f"RM-12 RED: guard not found for {name}; the tree changed")
    text = text.replace(guard, "", 1)

quit_fixed = '''    commands_->setHandler(QStringLiteral("file.quit"), []() { QApplication::quit(); });'''
quit_guarded = '''    commands_->setHandler(QStringLiteral("file.quit"), [this]() {
        // RM-12 family audit: QApplication::quit() ends the process WITHOUT
        // running closeEvent(), so File > Quit / Ctrl+Q was a second silent
        // data-loss path (every dirty buffer died unasked). Ask the SAME
        // unsaved-document question the window close asks before quitting.
        if (!confirmUnsavedDocuments()) {
            return;
        }
        QApplication::quit();
    });'''
if quit_guarded not in text:
    sys.exit("RM-12 RED: file.quit guard not found; the tree changed")
text = text.replace(quit_guarded, quit_fixed, 1)

if "confirmUnsavedDocuments()) {\n            return;" in text:
    sys.exit("RM-12 RED: a guard survived the strip")
open(target, "w", encoding="utf-8").write(text)
print("PREFIX guards_removed=4")
PY
sha256sum "$ROOT/src/app/app_shell.cpp" "$SRC"

commands=$(ninja -C "$BUILD" -t commands src/app/CMakeFiles/vedit_app_shell.dir/app_shell.cpp.o)
compile=$(printf '%s\n' "$commands" | grep -F -- '-c /vedit-build/src/app/app_shell.cpp' | tail -n 1)
if [ -z "$compile" ]; then
  echo "FATAL: could not extract the app_shell.cpp compile command" >&2
  exit 3
fi
flags=${compile%% -MD *}
eval "$flags -o '$OBJ' -c '$SRC'"

cp "$BUILD/src/app/libvedit_app_shell.a" "$LIB"
ar d "$LIB" app_shell.cpp.o
ar r "$LIB" "$OBJ"

link_commands=$(ninja -C "$BUILD" -t commands src/app/vedit-app-reset-layout-guard-selftest)
link=$(printf '%s\n' "$link_commands" | grep -F -- '-o src/app/vedit-app-reset-layout-guard-selftest ' | tail -n 1)
if [ -z "$link" ]; then
  echo "FATAL: could not extract the selftest link command" >&2
  exit 3
fi
link=${link#: && }
link=${link% && :}
link=${link//src\/app\/libvedit_app_shell.a/$LIB}
link=${link//-Wl,--dependency-file=src\/app\/CMakeFiles\/vedit-app-reset-layout-guard-selftest.dir\/link.d/}
link=${link//-o src\/app\/vedit-app-reset-layout-guard-selftest /-o $BIN }
( cd "$BUILD" && eval "$link" )

export XDG_RUNTIME_DIR=/tmp/xdg-rm12-prefix
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
set +e
env QT_QPA_PLATFORM=offscreen "$BIN"
status=$?
set -e
echo "RM12-PREFIX-RED exit=$status"
exit "$status"
