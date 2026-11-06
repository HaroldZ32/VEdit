#!/usr/bin/env bash
# Read-only review probe build + run (review-b4-r2).
# Nothing under src/, tests/ or gates is written; the binary goes to /tmp.
#
# Run inside the pinned image from the repository root, e.g.:
#   docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
#     -e LANG=C.UTF-8 -e QT_QPA_PLATFORM=offscreen \
#     -e XDG_RUNTIME_DIR=/tmp/xdg-probe vedit-qt6-build:local \
#     bash /vedit-build/evidence/ux-batch-2/review-b4-r2/build-probe.sh
set -euo pipefail

R="${REPO_ROOT:-/vedit-build}"

INCLUDES="-I${R}/src/app -I${R}/src/editor/kernel -I${R}/src/editor -I${R}/src/common -I${R}/src/workspace/breadcrumb -I${R}/src/syntax/grammars -I${R}/target/qt6-build/src/syntax/grammars/generated -I${R}/src/syntax/provider -I${R}/target/qt6-build/src/syntax/provider/generated -I${R}/third_party/tree-sitter/verilog/src -I${R}/src/workspace/jump/.. -I${R}/src/editor/fim -isystem /usr/include/x86_64-linux-gnu/qt6/QtCore -isystem /usr/include/x86_64-linux-gnu/qt6 -isystem /usr/lib/x86_64-linux-gnu/qt6/mkspecs/linux-g++ -isystem /usr/include/x86_64-linux-gnu/qt6/QtGui -isystem /usr/include/x86_64-linux-gnu/qt6/QtWidgets -isystem /usr/include/KF6/KSyntaxHighlighting -isystem /usr/include/KF6/KTextEditor -isystem /usr/include/KF6/KParts -isystem /usr/include/KF6/KIOCore -isystem /usr/include/KF6/KIO -isystem /usr/include/KF6/KCoreAddons -isystem /usr/include/x86_64-linux-gnu/qt6/QtNetwork -isystem /usr/include/x86_64-linux-gnu/qt6/QtDBus -isystem /usr/include/KF6/KXmlGui -isystem /usr/include/x86_64-linux-gnu/qt6/QtXml -isystem /usr/include/KF6/KConfig -isystem /usr/include/KF6/KConfigCore -isystem /usr/include/KF6/KConfigWidgets -isystem /usr/include/KF6/KWidgetsAddons -isystem /usr/include/KF6/KConfigGui -isystem /usr/include/KF6/KColorScheme -isystem /usr/include/KF6/KGuiAddons -isystem /usr/include/KF6/KCompletion -isystem ${R}/crates/vd-ffi/include -isystem /usr/include/x86_64-linux-gnu/qt6/QtTest"

DEFINES="-DKCOREADDONS_LIB -DKGUIADDONS_LIB -DQT_CORE_LIB -DQT_DBUS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_NO_DEBUG -DQT_TESTLIB_LIB -DQT_WIDGETS_LIB -DQT_XML_LIB -DVEDIT_APP_HAVE_BREADCRUMB=1 -DVEDIT_EDIT_CORE_HAVE_APP_SEAM=1 -DVEDIT_EDIT_CORE_HAVE_FIM=1 -DVEDIT_EDIT_CORE_HAVE_FOLDING=1 -DVEDIT_FIM_HAVE_FFI=1"

STATICS="${R}/target/qt6-build/src/app/libvedit_app_shell.a ${R}/target/qt6-build/src/app/editor-lane/libvedit_editor_core.a ${R}/target/qt6-build/src/app/editor-lane/breadcrumb-lane/libvedit_workspace_breadcrumb.a ${R}/target/qt6-build/src/syntax/grammars/libvedit_syntax_grammars.a ${R}/target/qt6-build/src/syntax/provider/libvedit_syntax_provider.a ${R}/target/qt6-build/src/syntax/grammars/libvedit_tree_sitter_verilog.a ${R}/target/qt6-build/src/app/editor-lane/breadcrumb-lane/jump-lane/libvedit_workspace_jump.a ${R}/target/qt6-build/src/app/editor-lane/fim-lane/libvedit_editor_fim.a ${R}/target/release/libvd_ffi.a"

SHARED="-lQt6Test -lQt6Widgets -lKF6TextEditor -lKF6SyntaxHighlighting -lKF6Parts -lKF6XmlGui -lKF6ConfigWidgets -lKF6Completion -lKF6ColorScheme -lKF6GuiAddons -lKF6ConfigGui -lKF6WidgetsAddons -lKF6KIOCore -lQt6Network -lKF6CoreAddons -lKF6ConfigCore -lQt6Xml -lQt6Gui -lQt6DBus -lQt6Core -ltree-sitter -lGLX -lOpenGL"

g++ -std=c++20 -fPIC -O1 -Wall -Wextra $DEFINES $INCLUDES \
    "${R}/evidence/ux-batch-2/review-b4-r2/probe_pane_order.cpp" \
    -o /tmp/probe_pane_order -Wl,--start-group $STATICS -Wl,--end-group $SHARED

QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-probe /tmp/probe_pane_order
