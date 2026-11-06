#!/usr/bin/env bash
# Build the reviewer's independent probe inside the pinned image, against the
# reviewed tree's static lane library. Reviewer-side only: no product file is
# read from this script's output or modified by it.
set -euo pipefail
cd /vedit-build

INCLUDES=(
  -I/vedit-build/target/qt6-build/src/app/workspace-lane-search/vedit-search-replace-tests_autogen/include
  -I/vedit-build/src/workspace/search
  -I/vedit-build/src/workspace/search/../../common
  -I/vedit-build/src/workspace/jump/..
  -I/vedit-build/src/app
  -I/vedit-build/src/app/../common
  -I/vedit-build/src/workspace/breadcrumb
  -I/vedit-build/src/syntax/grammars
  -I/vedit-build/target/qt6-build/src/syntax/grammars/generated
  -I/vedit-build/src/syntax/provider
  -I/vedit-build/target/qt6-build/src/syntax/provider/generated
  -I/vedit-build/src/syntax/grammars/../../../third_party/tree-sitter/verilog/src
  -I/vedit-build/src/editor
  -I/vedit-build/src/editor/fim
  -isystem /usr/include/x86_64-linux-gnu/qt6/QtCore
  -isystem /usr/include/x86_64-linux-gnu/qt6
  -isystem /usr/lib/x86_64-linux-gnu/qt6/mkspecs/linux-g++
  -isystem /usr/include/x86_64-linux-gnu/qt6/QtGui
  -isystem /usr/include/x86_64-linux-gnu/qt6/QtWidgets
  -isystem /vedit-build/crates/vd-ffi/include
  -isystem /usr/include/KF6/KSyntaxHighlighting
  -isystem /usr/include/KF6/KTextEditor
  -isystem /usr/include/KF6/KParts
  -isystem /usr/include/KF6/KIOCore
  -isystem /usr/include/KF6/KIO
  -isystem /usr/include/KF6/KCoreAddons
  -isystem /usr/include/x86_64-linux-gnu/qt6/QtNetwork
  -isystem /usr/include/x86_64-linux-gnu/qt6/QtDBus
  -isystem /usr/include/KF6/KXmlGui
  -isystem /usr/include/x86_64-linux-gnu/qt6/QtXml
  -isystem /usr/include/KF6/KConfig
  -isystem /usr/include/KF6/KConfigCore
  -isystem /usr/include/KF6/KConfigWidgets
  -isystem /usr/include/KF6/KWidgetsAddons
  -isystem /usr/include/KF6/KConfigGui
  -isystem /usr/include/KF6/KColorScheme
  -isystem /usr/include/KF6/KGuiAddons
  -isystem /usr/include/KF6/KCompletion
  -isystem /usr/include/x86_64-linux-gnu/qt6/QtTest
)

DEFINES=(
  -DKCOREADDONS_LIB -DKGUIADDONS_LIB -DQT_CORE_LIB -DQT_DBUS_LIB -DQT_GUI_LIB
  -DQT_NETWORK_LIB -DQT_NO_DEBUG -DQT_WIDGETS_LIB -DQT_XML_LIB
  -DVEDIT_APP_HAVE_BREADCRUMB=1
  -DVEDIT_BUILD_HEAD=\"4e2d0a57ca50db9568db5a5b040cc66f04c9a05f\"
  -DVEDIT_EDIT_CORE_HAVE_APP_SEAM=1 -DVEDIT_EDIT_CORE_HAVE_FIM=1
  -DVEDIT_EDIT_CORE_HAVE_FOLDING=1 -DVEDIT_FIM_HAVE_FFI=1
  -DVEDIT_SEARCH_HAVE_EDITOR_CORE=1 -DVEDIT_SEARCH_HAVE_FFI=1
  -DVEDIT_SEARCH_HAVE_SHELL=1
)

/usr/bin/c++ "${DEFINES[@]}" "${INCLUDES[@]}" -Wall -Wextra -O2 -g -DNDEBUG \
  -std=c++20 -Wpedantic \
  -c evidence/ux-batch-2/review-b2-r5/probes/probe_cap_ui.cpp -o /tmp/b2-r5-cap-ui-probe.o

/usr/bin/c++ -Wall -Wextra -O2 -g -DNDEBUG /tmp/b2-r5-cap-ui-probe.o \
  -o evidence/ux-batch-2/review-b2-r5/probes/b2-r5-cap-ui-probe \
  target/qt6-build/src/app/workspace-lane-search/libvedit_workspace_search.a \
  /usr/lib/x86_64-linux-gnu/libQt6Test.so.6.10.2 \
  target/qt6-build/src/app/editor-lane/libvedit_editor_core.a \
  target/qt6-build/src/app/libvedit_app_shell.a \
  /usr/lib/x86_64-linux-gnu/libKF6TextEditor.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6Parts.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6XmlGui.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6ConfigWidgets.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6Completion.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6ColorScheme.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6GuiAddons.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6ConfigGui.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6WidgetsAddons.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6KIOCore.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6CoreAddons.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libKF6ConfigCore.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libQt6Xml.so.6.10.2 \
  target/qt6-build/src/app/editor-lane/fim-lane/libvedit_editor_fim.a \
  /usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0 \
  /usr/lib/x86_64-linux-gnu/libQt6Network.so.6.10.2 \
  target/qt6-build/src/app/editor-lane/breadcrumb-lane/libvedit_workspace_breadcrumb.a \
  target/qt6-build/src/syntax/grammars/libvedit_syntax_grammars.a \
  target/qt6-build/src/syntax/provider/libvedit_syntax_provider.a \
  /usr/lib/x86_64-linux-gnu/libtree-sitter.so \
  target/qt6-build/src/syntax/grammars/libvedit_tree_sitter_verilog.a \
  /usr/lib/x86_64-linux-gnu/libQt6Widgets.so.6.10.2 \
  /usr/lib/x86_64-linux-gnu/libQt6Gui.so.6.10.2 \
  /usr/lib/x86_64-linux-gnu/libGLX.so \
  /usr/lib/x86_64-linux-gnu/libOpenGL.so \
  /usr/lib/x86_64-linux-gnu/libQt6DBus.so.6.10.2 \
  target/qt6-build/src/app/editor-lane/breadcrumb-lane/jump-lane/libvedit_workspace_jump.a \
  /vedit-build/target/release/libvd_ffi.a \
  -ldl -lm /usr/lib/x86_64-linux-gnu/libQt6Core.so.6.10.2

echo "built: evidence/ux-batch-2/review-b2-r5/probes/b2-r5-cap-ui-probe"
