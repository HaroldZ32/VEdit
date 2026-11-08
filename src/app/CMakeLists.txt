# Application shell (app-shell lane, T13).
#
# The root CMakeLists.txt adds this directory automatically once this file
# exists (docs/BUILD.md §12.4), so this lane never edits the root build.
#
# Targets:
#   vedit_app_shell          static library with the shell implementation
#   vedit-app                the shell executable (layout/menu/theme/docks)
#   vedit-app-shell-selftest ctest entry running `vedit-app --selfcheck`
#
# The shell links Qt6 Widgets + KSyntaxHighlighting only. It deliberately does
# NOT link KTextEditor: the editor is a seam (src/app/editor_provider.h) owned by
# src/editor (edit-core). That keeps the shell buildable and testable before the
# editor lane lands, and keeps the editor lane free to pick its own widgets.

set(CMAKE_AUTOMOC ON)
# UX batch 2 (item 11): the built-in user manual is a Qt resource
# (resources/help/user_manual.qrc embeds docs/USER_GUIDE.md), so the manual is
# inside the executable in every packaging form and no packaging script needs a
# second install rule.
set(CMAKE_AUTORCC ON)

# UI-Mosaic T1: themed_icons.cpp recolours the SVG action set at render time, so
# the shell links QtSvg. It is part of the pinned Qt6 6.10.2 image (see
# scripts/docker/qt6-locked-packages.txt) — not a new third-party dependency.
find_package(Qt6 REQUIRED COMPONENTS Svg)

set(VEDIT_APP_SHELL_SOURCES
  desktop_shortcuts.cpp
  app_shell.cpp
  command_registry.cpp
  crash_reporter.cpp
  editor_area.cpp
  editor_provider.cpp
  help_viewer.cpp
  i18n.cpp
  panel_registry.cpp
  product_theme.cpp
  session_store.cpp
  theme_manager.cpp
  themed_icons.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/help/user_manual.qrc"
)

# UI-Mosaic T1: the product theme is DATA — the card stylesheet template and the
# window/action icons are compiled into the executable that displays them.
# They are attached to the EXECUTABLES (not to the static library) so Qt's rcc
# initialiser always runs before main() without a Q_INIT_RESOURCE call:
# main.cpp belongs to the icon lane and is frozen for this task, and a qrc that
# sits only in the archive is dropped by the linker unless something references
# its init symbol. The ui-theme selftest links the shell library and exercises
# the same rendering code, so it receives the same resources.
set(VEDIT_APP_THEME_RESOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/themes/theme.qss.qrc"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/app_icon.qrc")

# T117: the build identity a crash report carries (HEAD + build time).
set(VEDIT_BUILD_HEAD "unknown")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../.git")
  execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/../.." rev-parse HEAD
    OUTPUT_VARIABLE VEDIT_BUILD_HEAD_VALUE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(VEDIT_BUILD_HEAD_VALUE)
    set(VEDIT_BUILD_HEAD "${VEDIT_BUILD_HEAD_VALUE}")
  endif()
endif()

add_library(vedit_app_shell STATIC ${VEDIT_APP_SHELL_SOURCES})
target_compile_definitions(vedit_app_shell PUBLIC VEDIT_BUILD_HEAD="${VEDIT_BUILD_HEAD}")
# The ONE path-display rule (src/common/path_display.h) is header-only and
# included by src/app and by the workspace lanes; each target adds its source
# path so a standalone lane build finds it without depending on src/app.
target_include_directories(vedit_app_shell PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../common")
target_link_libraries(vedit_app_shell PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Svg
  KF6::SyntaxHighlighting
)
# Same C++ static-check policy as the M0 smoke target (docs/BUILD.md §12.4/12.5):
# warnings in our own code are errors.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_app_shell PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-app main.cpp ${VEDIT_APP_THEME_RESOURCES})
target_link_libraries(vedit-app PRIVATE vedit_app_shell)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # `-rdynamic` lets backtrace_symbols_fd() print function names in a crash
  # report instead of bare addresses (T117).
  target_link_options(vedit-app PRIVATE -rdynamic)
endif()

# --- T18 i18n: compile translations/*.ts and ship the .qm next to the binary ---
# The .qm is what the product installs at startup (src/app/i18n.cpp). The build
# tree and the tarball keep it in <binary dir>/translations; the AppDir installs
# it under bin/translations because AppRun starts the product through the bundled
# loader, which makes applicationDirPath() the AppDir ROOT. The runtime probes
# both layouts (src/app/i18n.cpp translationCandidates()). Every file is offline
# — no network, no system locale data beyond Qt itself.
# Guarded: a tree without Qt6 LinguistTools (or without translations/) still
# configures and builds, it just ships the English sources.
set(VEDIT_TRANSLATIONS_SOURCE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../../translations")
if(TARGET Qt6::lrelease AND EXISTS "${VEDIT_TRANSLATIONS_SOURCE_DIR}/vedit_zh_CN.ts")
  qt_add_lrelease(
    TS_FILES "${VEDIT_TRANSLATIONS_SOURCE_DIR}/vedit_zh_CN.ts"
    QM_FILES_OUTPUT_VARIABLE VEDIT_QM_FILES
    QM_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translations")
  if(TARGET release_translations)
    add_dependencies(vedit-app release_translations)
  endif()
  # T38/C-class: Qt's OWN standard-dialog strings (OK/Cancel/Open/Save in
  # QFileDialog, QMessageBox, QDialogButtonBox) are translated by Qt itself, so
  # the vendored qtbase_<tag>.qm must sit in the SAME directory the runtime
  # loads vedit_<tag>.qm from and be installed by the same rule. Source/version/
  # licence of the vendored file are recorded in evidence/i18n-l2/.
  set(VEDIT_QT_BASE_QM "${VEDIT_TRANSLATIONS_SOURCE_DIR}/qtbase_zh_CN.qm")
  if(EXISTS "${VEDIT_QT_BASE_QM}")
    set(VEDIT_QT_BASE_QM_BUILD
      "${CMAKE_CURRENT_BINARY_DIR}/translations/qtbase_zh_CN.qm")
    add_custom_command(
      OUTPUT "${VEDIT_QT_BASE_QM_BUILD}"
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "${CMAKE_CURRENT_BINARY_DIR}/translations"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${VEDIT_QT_BASE_QM}" "${VEDIT_QT_BASE_QM_BUILD}"
      DEPENDS "${VEDIT_QT_BASE_QM}"
      COMMENT "Copying the vendored qtbase_zh_CN.qm next to the VEdit .qm (T38/C)")
    add_custom_target(vedit_qtbase_translation ALL DEPENDS "${VEDIT_QT_BASE_QM_BUILD}")
    add_dependencies(vedit-app vedit_qtbase_translation)
    list(APPEND VEDIT_QM_FILES "${VEDIT_QT_BASE_QM_BUILD}")
    message(STATUS "VEdit: vendored Qt standard-dialog translation wired: ${VEDIT_QT_BASE_QM}")
  else()
    message(STATUS
      "VEdit: no vendored qtbase_zh_CN.qm; Qt standard buttons stay English (T38/C)")
  endif()
  install(FILES ${VEDIT_QM_FILES} DESTINATION bin/translations)
  message(STATUS "VEdit: translations compiled -> ${VEDIT_QM_FILES}")
else()
  message(STATUS
    "VEdit: no translation build (Qt6::lrelease or "
    "${VEDIT_TRANSLATIONS_SOURCE_DIR}/vedit_zh_CN.ts missing); UI stays English")
endif()
# Terminal panel lane (T35): the shell registers the "terminal" panel id but the
# factory and the widget live in this lane. Guarded so a tree without the lane
# still configures (same "missing lane => skip" promise as the root lane list).
if(TARGET vedit_terminal)
  target_link_libraries(vedit-app PRIVATE vedit_terminal)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-app PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# --- Edit-core lane (Slice 1 / task T10) ------------------------------------
# The editing kernel lives in src/editor and ships its own CMakeLists.txt; the
# root CMakeLists.txt is owned by the build-base lane, so the shell wires the
# lane here (the shell is its consumer). A tree without the lane still
# configures. The lane registers its own ctest entry.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../editor/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../editor"
                   "${CMAKE_CURRENT_BINARY_DIR}/editor-lane")
  if(TARGET vedit_editor_core)
    target_link_libraries(vedit-app PRIVATE vedit_editor_core)
    target_compile_definitions(vedit-app PRIVATE VEDIT_HAVE_EDIT_CORE=1)
    message(STATUS "VEdit: edit-core lane wired (vedit_editor_core); KTextEditor is the product editor")
  endif()
endif()

# --- Rust service layer (C ABI, Slice 0 / task T4) ---------------------------
# The product binary links the Rust services (AC-0.1/AC-S0.1) and the ABI smoke
# runs against a real project fixture (AC-S0.2). The fixture path is passed on
# the command line so the test reads the SOURCE tree, not a copied build tree.
if(TARGET vd_ffi)
  target_link_libraries(vedit-app PRIVATE vd_ffi)
  target_compile_definitions(vedit-app PRIVATE VEDIT_HAVE_RUST_FFI=1)
  add_test(NAME vedit-app-ffi-abi-selftest
           COMMAND vedit-app --selfcheck-ffi
                   "${CMAKE_CURRENT_SOURCE_DIR}/../../crates/vd-ffi/tests/fixtures/project")
  set_tests_properties(vedit-app-ffi-abi-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
    TIMEOUT 120
  )
  message(STATUS "VEdit: vedit-app links the Rust service layer (vd-ffi); ABI selftest registered")
endif()

add_test(NAME vedit-app-shell-selftest
         COMMAND vedit-app --selfcheck --state-dir "${CMAKE_CURRENT_BINARY_DIR}/selftest-state")
set_tests_properties(vedit-app-shell-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 120
)

# --- Workspace lanes (ws-filetree / ws-palette / ws-search) -----------------
# A workspace lane is an OPTIONAL subdirectory: it exists only once the lane has
# landed, and the unified entry point src/workspace/integration.cpp calls the
# install<Lane>() functions it can see and silently skips the missing ones.
# src/app therefore never learns which lanes are present, and landing a new
# workspace lane does not edit this file. Convention: evidence/ws-palette/SEAM.md.
# `jump` first: it is the process-wide jump contract the panel lanes consume
# (their CMakeLists may link vedit_workspace_jump at configure time), and it has
# no dependency on the shell or on another lane.
set(VEDIT_WORKSPACE_LANES jump filetree palette search project module_tree diagnostics vcs settings)
foreach(lane IN LISTS VEDIT_WORKSPACE_LANES)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/${lane}/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../workspace/${lane}"
                     "${CMAKE_CURRENT_BINARY_DIR}/workspace-lane-${lane}")
    message(STATUS "VEdit: workspace lane '${lane}' included")
  endif()
endforeach()

# Library target per lane. The convention is `vedit_workspace_<lane>`; a lane
# that landed with its own target name is mapped here (one line each).
set(VEDIT_WORKSPACE_FILETREE_TARGET vedit_file_tree)
set(VEDIT_WORKSPACE_PALETTE_TARGET vedit_workspace_palette)
set(VEDIT_WORKSPACE_SEARCH_TARGET vedit_workspace_search)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/integration.cpp")
  add_library(vedit_workspace_integration STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/integration.cpp")
  target_include_directories(vedit_workspace_integration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../workspace")
  target_link_libraries(vedit_workspace_integration PUBLIC vedit_app_shell)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit_workspace_integration PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  foreach(lane IN LISTS VEDIT_WORKSPACE_LANES)
    string(TOUPPER "${lane}" lane_upper)
    set(lane_target "${VEDIT_WORKSPACE_${lane_upper}_TARGET}")
    if(NOT lane_target)
      set(lane_target "vedit_workspace_${lane}")
    endif()
    # A lane is only wired when BOTH its library and its integration entry file
    # exist; adding <lane>/<lane>_integration.cpp is the whole step a lane takes.
    if(TARGET ${lane_target} AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/${lane}/${lane}_integration.cpp")
      target_link_libraries(vedit_workspace_integration PUBLIC ${lane_target})
      target_compile_definitions(vedit_workspace_integration PRIVATE
        "VEDIT_WORKSPACE_HAVE_${lane_upper}=1")
      message(STATUS "VEdit: workspace lane '${lane}' wired (${lane_target})")
    endif()
  endforeach()
  target_link_libraries(vedit-app PRIVATE vedit_workspace_integration)
  message(STATUS "VEdit: workspace integration enabled (src/workspace/integration.cpp)")
endif()


# --- T20/A-3: the pane's real (clickable) breadcrumb ------------------------
# EditorPane mounts vedit::breadcrumb::Bar when this lane is present: each
# segment asks the application-wide jump registry for the container's line
# (AC-7.6). The lane is optional in this build system (the editor lane includes
# it when it exists), so the shell only links it when the target is there and
# falls back to the T94 status label otherwise. `vedit_app_shell` is defined
# above; target_link_libraries() may extend it here, after the lane was added.
if(TARGET vedit_workspace_breadcrumb)
  target_link_libraries(vedit_app_shell PUBLIC vedit_workspace_breadcrumb)
  target_compile_definitions(vedit_app_shell PUBLIC VEDIT_APP_HAVE_BREADCRUMB=1)
  message(STATUS "VEdit: editor pane mounts the breadcrumb lane's clickable Bar (T20/A-3)")
else()
  message(STATUS
    "VEdit: breadcrumb lane absent; the editor pane keeps the plain breadcrumb label")
endif()

# --- T117: crash self-reporting --------------------------------------------
# The fork/raise selftest proves the handler writes a readable report; the shell
# check runs the SHIPPED binary with --crash-test=segv, which is what a user's
# next crash will exercise.
add_executable(vedit-app-crash-reporter-selftest crash_reporter_selftest.cpp)
target_link_libraries(vedit-app-crash-reporter-selftest PRIVATE vedit_app_shell)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-app-crash-reporter-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_options(vedit-app-crash-reporter-selftest PRIVATE -rdynamic)
endif()
add_test(NAME vedit-app-crash-reporter-selftest
         COMMAND vedit-app-crash-reporter-selftest "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(vedit-app-crash-reporter-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime-crash"
  TIMEOUT 120
)
add_test(NAME vedit-app-crash-report-shipped
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/crash_report_check.sh"
                 $<TARGET_FILE:vedit-app> "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(vedit-app-crash-report-shipped PROPERTIES TIMEOUT 120)

# --- Packaged-AppDir regressions (T18/T64) ---------------------------------
# The restart must select AppRun/$APPIMAGE, never the bundled loader that
# applicationFilePath() names in an AppDir, and the packaged .qm live under
# <AppDir>/bin/translations. Both resolvers are pure, so this needs no GUI.
# RM-16: the file-dialog filters are DATA. This binary takes the PRODUCT
# open-file filter table and asserts every filelist-claiming entry offers exactly
# the extension set the project parser recognises; it also asserts the
# import-filelist dialog's SELECTED filter is the filelist one, and drives a real
# QFileSystemModel (the engine Qt's file dialog uses) over a temp dir holding
# `a.sv` + `lib.f` to prove the default filter shows `lib.f`.
add_executable(vedit-file-dialog-filters-selftest file_dialog_filters_selftest.cpp)
target_link_libraries(vedit-file-dialog-filters-selftest PRIVATE vedit_app_shell)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-file-dialog-filters-selftest PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-file-dialog-filters-selftest COMMAND vedit-file-dialog-filters-selftest)
set_tests_properties(vedit-file-dialog-filters-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 120
)

add_executable(vedit-app-i18n-selftest i18n_selftest.cpp)
target_link_libraries(vedit-app-i18n-selftest PRIVATE vedit_app_shell)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-app-i18n-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-app-i18n-selftest COMMAND vedit-app-i18n-selftest)
set_tests_properties(vedit-app-i18n-selftest PROPERTIES TIMEOUT 60)

# UI-Mosaic T1: the six product themes, the cards.qss token template, the
# palette contrast floors (A3: body >= 4.5:1, disabled >= 3:1) and the embedded
# icon set are a frozen contract (.mpd/plans/ui-mosaic/CONTRACT.md §1.7). Hand-
# written main() + failure counter, like the other app selftests.
add_executable(vedit-app-ui-theme-selftest ui_theme_selftest.cpp ${VEDIT_APP_THEME_RESOURCES})
target_link_libraries(vedit-app-ui-theme-selftest PRIVATE vedit_app_shell)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-app-ui-theme-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-app-ui-theme-selftest COMMAND vedit-app-ui-theme-selftest)
set_tests_properties(vedit-app-ui-theme-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime-ui-theme"
  TIMEOUT 120
)

# --- UX batch 2 (lane A2): menus, main toolbar, i18n, built-in manual --------
# `vedit-app-ux-selftest` asserts, with real widgets and real actions: the fixed
# File/Edit/View/Project/Help order, Preferences at the bottom of Edit, the
# toolbar holding the command registry's own QActions (Build/Format really run
# the diagnostics commands), New opening a document, Undo/Redo changing the
# active document (the plain-text fallback AND — when the lane is present — the
# real KTextEditor document), the zh_CN .qm rendering the five menu titles +
# Preferences + a panel toggle in Chinese, and Help -> User Manual (F1) opening
# the embedded docs/USER_GUIDE.md.
#
# `vedit-app-ux-screenshots` runs the SAME binary under xvfb-run + xcb and writes
# real window grabs; without xvfb-run it is an explicit SKIP (exit 77).
add_executable(vedit-app-ux-selftest ux_selftest.cpp)
target_link_libraries(vedit-app-ux-selftest PRIVATE vedit_app_shell)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-app-ux-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(TARGET release_translations)
  # The i18n assertions load the .qm the build just produced (the same file the
  # product installs), never a hand-copied fixture.
  add_dependencies(vedit-app-ux-selftest release_translations)
  target_compile_definitions(vedit-app-ux-selftest PRIVATE
    VEDIT_UX_TRANSLATIONS_DIR="${CMAKE_CURRENT_BINARY_DIR}/translations")
endif()
if(TARGET vedit_editor_core)
  # The product editor: proves Undo/Redo drive KTextEditor's OWN actions on a
  # real document (not a placeholder). Optional, so a tree without the editor
  # lane still builds and its checks are reported as SKIP.
  target_link_libraries(vedit-app-ux-selftest PRIVATE vedit_editor_core)
  target_compile_definitions(vedit-app-ux-selftest PRIVATE VEDIT_UX_HAVE_EDIT_CORE=1)
endif()
# F1 vs the terminal (UX batch 2 item 11): the manual is a window-level F1
# shortcut, but a focused terminal must receive F1 as VT100 "OP". The assertion
# needs REAL key events through Qt's shortcut map + a real pty, so the Test
# module and the terminal lane are linked when both are available (optional).
find_package(Qt6 QUIET COMPONENTS Test)
if(TARGET Qt6::Test AND TARGET vedit_terminal)
  target_link_libraries(vedit-app-ux-selftest PRIVATE Qt6::Test vedit_terminal)
  target_compile_definitions(vedit-app-ux-selftest PRIVATE VEDIT_UX_HAVE_TERMINAL=1)
endif()
add_test(NAME vedit-app-ux-selftest COMMAND vedit-app-ux-selftest)
set_tests_properties(vedit-app-ux-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime-ux"
  TIMEOUT 240
)
# --- RM-7: disabled controls must LOOK disabled in every theme ---------------
# The product has no .qss resources: the shell applies a KSyntaxHighlighting
# theme as a QPalette, and `QPalette::setColor(role, colour)` fills the Disabled
# group too — so a disabled control used to render exactly like an enabled one
# (the user's "everything that should be grey is not"). This executable asserts
# the visual contract NUMERICALLY, for every theme the manager can apply:
#   * palette: the Disabled colour of each text role must differ from Active by
#     a stated channel / relative-luminance / contrast-ratio delta (no hard-coded
#     hex, so a theme change cannot break it);
#   * pixels: a real QToolButton (fed by a QAction, the Undo/Redo wiring),
#     QPushButton, QComboBox, QLineEdit and QPlainTextEdit are rendered offscreen
#     in the enabled and disabled state and the rendered colours are compared.
# A regression to Disabled == Active fails on every theme and every widget.
add_executable(vedit-app-disabled-look-selftest disabled_look_selftest.cpp)
target_link_libraries(vedit-app-disabled-look-selftest PRIVATE vedit_app_shell)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-app-disabled-look-selftest PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-app-disabled-look-selftest COMMAND vedit-app-disabled-look-selftest)
set_tests_properties(vedit-app-disabled-look-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime-disabled-look"
  TIMEOUT 240
)
# --- RM-12: destructive layout commands must confirm unsaved documents ------
# View -> Reset Layout and File -> New Empty Layout (and the mixed-split preset,
# which reaches the same EditorArea::resetLayout()) used to delete every pane and
# the document that dies with it (KTextEditor bounds the document to its view)
# with no confirmation. This executable drives the REAL command actions (the
# QAction objects the menu, the toolbar and the shortcut are bound to) with the
# shell's unsaved-prompt test seam: Cancel must keep every dirty buffer and the
# layout byte-for-byte; Discard/Save must let the reset happen; a clean layout
# must reset with NO prompt at all.
add_executable(vedit-app-reset-layout-guard-selftest reset_layout_guard_selftest.cpp)
target_link_libraries(vedit-app-reset-layout-guard-selftest PRIVATE vedit_app_shell)
if(TARGET vedit_editor_core)
  # The product editor: proves the real KTextEditor document (whose lifetime is
  # its view) survives a cancelled reset, not only the demo placeholder.
  target_link_libraries(vedit-app-reset-layout-guard-selftest PRIVATE vedit_editor_core)
  target_compile_definitions(vedit-app-reset-layout-guard-selftest PRIVATE
    VEDIT_RESET_HAVE_EDIT_CORE=1)
  target_include_directories(vedit-app-reset-layout-guard-selftest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../editor/kernel")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-app-reset-layout-guard-selftest PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-app-reset-layout-guard-selftest
         COMMAND vedit-app-reset-layout-guard-selftest)
set_tests_properties(vedit-app-reset-layout-guard-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime-reset-guard"
  TIMEOUT 240
)
# --- UX batch 2: split-pane activation --------------------------------------
# A press (or focus-in) that lands anywhere inside a pane's editor must make that
# pane the ACTIVE pane of the EditorArea, whichever widget of the editor tree
# receives it. The product editor is KTextEditor, whose real mouse target is
# KatePart's inner widget (a CHILD of the public View), so this OFFSCREEN test
# drives real QTest events to the deepest child under the click point of a
# two-pane EditorArea. It needs the edit-core lane (real provider + real
# KTextEditor::View); without it the entry is an explicit SKIP (exit 77), never a
# silent pass.
if(TARGET vedit_editor_core AND TARGET Qt6::Test)
  add_executable(vedit-editor-pane-activation-tests editor_area_activation_test.cpp)
  target_link_libraries(vedit-editor-pane-activation-tests PRIVATE
    vedit_app_shell
    vedit_editor_core
    Qt6::Test
  )
  # ktexteditor_provider.h lives in the kernel lane; the test installs the real
  # provider through its public factory, exactly like the product shell does.
  target_include_directories(vedit-editor-pane-activation-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../editor/kernel")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-editor-pane-activation-tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-editor-pane-activation-tests
           COMMAND vedit-editor-pane-activation-tests)
  set_tests_properties(vedit-editor-pane-activation-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime-pane-activation"
    TIMEOUT 120
    SKIP_RETURN_CODE 77
  )
  message(STATUS "VEdit: split-pane activation test enabled (two panes, real KTextEditor)")
else()
  add_test(NAME vedit-editor-pane-activation-tests
           COMMAND bash -c "echo 'SKIP: edit-core lane or Qt6::Test missing'; exit 77")
  set_tests_properties(vedit-editor-pane-activation-tests PROPERTIES SKIP_RETURN_CODE 77)
  message(STATUS "VEdit: split-pane activation test NOT built (edit-core lane or Qt6::Test missing)")
endif()
# --- RM-5b: a user-changed shortcut must FIRE through the real key path -----
# The Shortcuts page could edit a binding, but the product had no proof the new
# sequence reached the command. This test drives the shell's own registry, then
# delivers the key through the QWindow (the same path a physical keystroke
# takes, including Qt's shortcut map) and asserts the command fired; it also
# restarts a shell on the same state dir for the persistence half.
if(TARGET Qt6::Test)
  add_executable(vedit-shortcut-key-path-tests shortcut_key_path_test.cpp)
  target_link_libraries(vedit-shortcut-key-path-tests PRIVATE
    vedit_app_shell
    vedit_workspace_settings
    vedit_editor_core
    Qt6::Test
  )
  target_include_directories(vedit-shortcut-key-path-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../editor/kernel"
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-shortcut-key-path-tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-shortcut-key-path-tests
           COMMAND vedit-shortcut-key-path-tests)
  set_tests_properties(vedit-shortcut-key-path-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime-shortcut-key"
    TIMEOUT 120
  )
  message(STATUS "VEdit: shortcut key-path regression enabled (RM-5b)")
else()
  message(STATUS "VEdit: Qt6 Test missing -> shortcut key-path regression NOT built")
endif()

find_program(VEDIT_UX_XVFB_RUN xvfb-run)
if(VEDIT_UX_XVFB_RUN)
  set(VEDIT_UX_SCREENSHOT_DIR "${CMAKE_CURRENT_BINARY_DIR}/ux-screenshots")
  add_test(NAME vedit-app-ux-screenshots
           COMMAND ${VEDIT_UX_XVFB_RUN} -a -s "-screen 0 1600x1000x24"
                   $<TARGET_FILE:vedit-app-ux-selftest> --screenshots
                   "${VEDIT_UX_SCREENSHOT_DIR}")
  set_tests_properties(vedit-app-ux-screenshots PROPERTIES
    ENVIRONMENT
      "QT_QPA_PLATFORM=xcb;XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime-ux-shot;VEDIT_UX_SCREENSHOT_DIR=${VEDIT_UX_SCREENSHOT_DIR}"
    SKIP_RETURN_CODE 77
    TIMEOUT 300
  )
else()
  add_test(NAME vedit-app-ux-screenshots
           COMMAND bash -c "echo 'SKIP: xvfb-run not available'; exit 77")
  set_tests_properties(vedit-app-ux-screenshots PROPERTIES SKIP_RETURN_CODE 77)
endif()

# --- RM-7b/M2: the file-status dot is a hand-painted widget -------------------
# The disabled-look selftest also proves the file-tree's dot dims on a disabled
# row. Both targets exist by the end of this file (the selftest above, the lane
# in the workspace loop), so the seam is wired last. A tree without the lane
# still builds the selftest; its dot check is reported as an explicit SKIP.
if(TARGET vedit_file_tree AND TARGET vedit-app-disabled-look-selftest)
  target_link_libraries(vedit-app-disabled-look-selftest PRIVATE vedit_file_tree)
  target_compile_definitions(vedit-app-disabled-look-selftest PRIVATE
    VEDIT_DISABLED_LOOK_HAVE_FILETREE=1)
  message(STATUS "VEdit: disabled-look selftest covers the file-status dot (RM-7b/M2)")
endif()
