# Edit-core kernel lane (Slice 1, task T10): KTextEditor as the product's
# editing kernel, with no Rust/C-ABI involvement on the edit path.
#
# Two ways to build it, like every lane in this repository:
#   * standalone  -- cmake -S src/editor -B <build> ; ctest --test-dir <build>
#   * product     -- src/app/CMakeLists.txt add_subdirectory()s this directory
#                    and links vedit_editor_core into vedit-app (the root
#                    CMakeLists.txt is owned by the build-base lane and is not
#                    edited by this lane).
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # C is enabled because the folding lane this directory wires in (T94) builds
  # the vendored tree-sitter grammar from a .c source. A CXX-only standalone
  # configure silently left libvedit_tree_sitter_verilog.a without parser.c.o,
  # which then failed to link every lane test (`undefined reference to
  # tree_sitter_verilog`). The product build already enables C.
  project(VEditEditCore LANGUAGES C CXX)
  set(VEDIT_EDIT_CORE_STANDALONE ON)
  enable_testing()
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
  find_package(KF6TextEditor REQUIRED)
  find_package(KF6SyntaxHighlighting REQUIRED)
else()
  set(VEDIT_EDIT_CORE_STANDALONE OFF)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(VEDIT_EDIT_CORE_SOURCES
  kernel/atomic_save.cpp
  kernel/cross_boundary.cpp
  kernel/diagnostics_annotations.cpp
  kernel/document_io.cpp
  kernel/editor_advanced.cpp
  kernel/editor_context_menu.cpp
  kernel/edit_core_selftest.cpp
  kernel/find_replace.cpp
  kernel/goto_line.cpp
  kernel/highlight_diagnosis.cpp
  kernel/highlight_mode.cpp
  kernel/lsp_actions.cpp
  kernel/multi_cursor.cpp
  kernel/signature_help.cpp
  kernel/undo_ops.cpp
)

# The EditorProvider seam lives in src/app (the shell owns the interface); the
# kernel only needs the header, never the shell implementation. When src/app is
# absent (a bare kernel checkout) the provider adapter is left out.
set(VEDIT_EDIT_CORE_HAVE_APP_SEAM OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../app/editor_provider.h")
  set(VEDIT_EDIT_CORE_HAVE_APP_SEAM ON)
  list(APPEND VEDIT_EDIT_CORE_SOURCES kernel/ktexteditor_provider.cpp)
endif()

# --- T94: the live consumer of the breadcrumb lane --------------------------
# The editor is the consumer of the breadcrumb lane, so it wires it (the root
# CMakeLists.txt is owned by the build-base lane). Guarded: a tree without the
# lane still configures and builds, just without the wiring.
#
# T29 deleted the folding lane (`src/syntax/folding`, its static library):
# KDE 6 computes folding from its own KSyntaxHighlighting definitions and VEdit no longer injects fold commands, so
# no product code linked the library any more (AGENT.md §10/§11). The macro keeps
# its historical name on purpose — `ktexteditor_provider.cpp` uses it to guard the
# BREADCRUMB binding, and renaming it would touch the kernel (outside T29's write
# scope); it now means "the breadcrumb lane is wired".
set(VEDIT_EDIT_CORE_HAVE_FOLDING OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/breadcrumb/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../workspace/breadcrumb"
                   "${CMAKE_CURRENT_BINARY_DIR}/breadcrumb-lane")
  list(APPEND VEDIT_EDIT_CORE_SOURCES kernel/fold_breadcrumb.cpp)
  set(VEDIT_EDIT_CORE_HAVE_FOLDING ON)
  message(STATUS "VEdit edit-core: breadcrumb lane wired into the live editor (T94; T29 dropped the folding lib)")
endif()

# --- T104: FIM inline completion (the editor is the lane's consumer) --------
# The lane (src/editor/fim, T27) owns the state machine and the Rust C-ABI
# backend; the kernel only needs the per-view binding. Guarded: a tree without
# the lane still configures and builds.
set(VEDIT_EDIT_CORE_HAVE_FIM OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fim/CMakeLists.txt")
  # The FIM lane names its FFI inputs VEDIT_FFI_STATICLIB / VEDIT_FFI_INCLUDE_DIR.
  # Map the checkout build (the same libvd_ffi.a the other lanes link) into them;
  # without this the lane would build without the Rust chain and the product
  # could never reach the service (the wiring would only ever report "disabled").
  set(VEDIT_FFI_CHECKOUT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../target/release/libvd_ffi.a")
  set(VEDIT_FFI_CHECKOUT_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../crates/vd-ffi/include")
  if(NOT VEDIT_FFI_STATICLIB AND EXISTS "${VEDIT_FFI_CHECKOUT_LIB}")
    set(VEDIT_FFI_STATICLIB "${VEDIT_FFI_CHECKOUT_LIB}" CACHE FILEPATH
        "libvd_ffi.a (release) for the in-process FIM service" FORCE)
  endif()
  if(NOT VEDIT_FFI_INCLUDE_DIR AND EXISTS "${VEDIT_FFI_CHECKOUT_INCLUDE}/vd_ffi.h")
    set(VEDIT_FFI_INCLUDE_DIR "${VEDIT_FFI_CHECKOUT_INCLUDE}" CACHE PATH
        "directory holding the cbindgen vd_ffi.h" FORCE)
  endif()
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/fim"
                   "${CMAKE_CURRENT_BINARY_DIR}/fim-lane")
  list(APPEND VEDIT_EDIT_CORE_SOURCES kernel/fim_wiring.cpp)
  set(VEDIT_EDIT_CORE_HAVE_FIM ON)
  message(STATUS "VEdit edit-core: FIM inline-completion lane wired into the live editor (T104)")
endif()

add_library(vedit_editor_core STATIC ${VEDIT_EDIT_CORE_SOURCES})
target_include_directories(vedit_editor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_editor_core PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  KF6::TextEditor
  # The highlighting diagnosis (T81) counts format ranges with the same engine
  # KTextEditor::View uses; the editor target already depends on it transitively,
  # but the dependency is real and stated here.
  KF6::SyntaxHighlighting
)
if(VEDIT_EDIT_CORE_HAVE_FOLDING)
  # T29: the folding lane library is gone; only the breadcrumb lane remains.
  target_link_libraries(vedit_editor_core PUBLIC
    vedit_workspace_breadcrumb
  )
  target_compile_definitions(vedit_editor_core PUBLIC VEDIT_EDIT_CORE_HAVE_FOLDING=1)
endif()
if(VEDIT_EDIT_CORE_HAVE_FIM)
  target_link_libraries(vedit_editor_core PUBLIC vedit_editor_fim)
  target_compile_definitions(vedit_editor_core PUBLIC VEDIT_EDIT_CORE_HAVE_FIM=1)
endif()

if(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
  target_include_directories(vedit_editor_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../app")
  target_compile_definitions(vedit_editor_core PUBLIC VEDIT_EDIT_CORE_HAVE_APP_SEAM=1)
  message(STATUS "VEdit edit-core: src/app EditorProvider seam found -> provider adapter built")
else()
  message(STATUS "VEdit edit-core: no src/app seam -> provider adapter not built")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_editor_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-edit-core-selftest kernel/edit_core_selftest_main.cpp)
target_link_libraries(vedit-edit-core-selftest PRIVATE vedit_editor_core)
# The delivery-like highlighting diagnosis needs the shipped XML files to build
# its private XDG share directory (the same two files packaging installs).
target_compile_definitions(vedit-edit-core-selftest PRIVATE
  VEDIT_SYNTAX_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../resources")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-edit-core-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-native-fold-tests kernel/native_fold_tests.cpp)
target_link_libraries(vedit-native-fold-tests PRIVATE vedit_editor_core)
if(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
  target_include_directories(vedit-native-fold-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../app")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-native-fold-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-native-fold-tests COMMAND vedit-native-fold-tests)
set_tests_properties(vedit-native-fold-tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 120
)

# --- T29: native KDE folding by REAL GUTTER CLICK (moved out of the deleted
# folding lane) --------------------------------------------------------------
# `vedit-fold-gutter-click` is the CURRENT native-folding acceptance criterion
# (AGENT.md §10): under Xvfb+xcb a real mouse click on KDE's own gutter marker
# must fold a module and a second click must unfold it, with a non-region line as
# the negative control. The probe lived in `src/syntax/folding/tests/` only
# because that lane owned the experiment; it uses KTextEditor + Qt alone (it never
# called a symbol of the deleted folding library), so T29 moved the source into the editor
# kernel and re-registered it here — the check id is unchanged on purpose, so the
# documented criterion stays findable.
add_executable(fold-gutter-click-probe kernel/fold_gutter_click_probe.cpp)
target_link_libraries(fold-gutter-click-probe PRIVATE Qt6::Widgets KF6::TextEditor)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(fold-gutter-click-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
find_program(VEDIT_XVFB_RUN xvfb-run)
if(VEDIT_XVFB_RUN)
  add_test(NAME vedit-fold-gutter-click
           COMMAND ${VEDIT_XVFB_RUN} -a -s "-screen 0 1280x1024x24"
                   $<TARGET_FILE:fold-gutter-click-probe>
                   "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/syntax"
                   "${CMAKE_CURRENT_BINARY_DIR}/fold-gutter.png")
  set_tests_properties(vedit-fold-gutter-click PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=xcb;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-fold"
    SKIP_RETURN_CODE 77
    TIMEOUT 120
  )
else()
  add_test(NAME vedit-fold-gutter-click
           COMMAND bash -c "echo 'SKIP: xvfb-run not available'; exit 77")
  set_tests_properties(vedit-fold-gutter-click PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_test(NAME vedit-edit-core-selftest COMMAND vedit-edit-core-selftest)
set_tests_properties(vedit-edit-core-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 300
)

# --- A3: extension -> definition mapping (VHDL / Markdown / Tcl + SDC/XDC) ---
# Asserts the explicit table wins over the editor repository (`.v` stays
# "Verilog"; a decoy definition claiming `*.sdc` does not steal it), that the
# six contract extensions resolve to their definition names, and that an unknown
# extension still falls back to ANY valid repository definition (the former
# `author == "VEdit"` gate is gone). The probe share is built from
# resources/syntax at run time, exactly the files packaging installs.
add_executable(vedit-highlight-mode-selftest kernel/highlight_mode_selftest.cpp)
target_link_libraries(vedit-highlight-mode-selftest PRIVATE vedit_editor_core)
# R4(d): drift guard against the syntax-data table. KsyntaxData owns the
# extension -> language mapping used by the breadcrumb/syntax-data path; linking
# it here lets the test assert both tables agree (it is added by the lane loop
# before src/app, so the target exists; the guard keeps a standalone build of
# this lane working).
if(TARGET vedit_syntax_kde)
  target_link_libraries(vedit-highlight-mode-selftest PRIVATE vedit_syntax_kde)
  target_compile_definitions(vedit-highlight-mode-selftest PRIVATE VEDIT_HAVE_SYNTAX_KDE=1)
endif()
target_compile_definitions(vedit-highlight-mode-selftest PRIVATE
  VEDIT_SYNTAX_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../resources")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-highlight-mode-selftest PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-highlight-mode-selftest COMMAND vedit-highlight-mode-selftest)
set_tests_properties(vedit-highlight-mode-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-highlight-mode"
  TIMEOUT 180
)

# --- T13 (AC-4.3): find / replace API + the in-view bar ---------------------
# ADDENDUM A-7 (T13) grants kernel/find_replace_selftest.cpp. Qt6Test is needed
# by this lane test only; the product never links Qt6::Test. A build image
# without the Test component skips the target loudly instead of failing.
find_package(Qt6 QUIET COMPONENTS Test)
if(TARGET Qt6::Test)
  add_executable(vedit-editor-find-replace-selftest kernel/find_replace_selftest.cpp)
  target_link_libraries(vedit-editor-find-replace-selftest PRIVATE
    vedit_editor_core
    Qt6::Test
  )
  if(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
    target_include_directories(vedit-editor-find-replace-selftest PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/../app")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-editor-find-replace-selftest PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-editor-find-replace-selftest
           COMMAND vedit-editor-find-replace-selftest)
  set_tests_properties(vedit-editor-find-replace-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-find"
    TIMEOUT 180
    SKIP_RETURN_CODE 77
  )
  message(STATUS "VEdit edit-core: find/replace bar selftest enabled (T13)")
else()
  message(STATUS "VEdit edit-core: Qt6 Test missing -> find/replace bar selftest NOT built")
endif()

# --- T14 (AC-4.2/AC-4.4): block selection, multi-cursor, editing actions ---
# ADDENDUM A-10 (T14) grants kernel/multi_cursor_selftest.cpp. Same Qt6::Test
# rule as the T13 lane test.
if(TARGET Qt6::Test)
  add_executable(vedit-editor-multi-cursor-selftest kernel/multi_cursor_selftest.cpp)
  target_link_libraries(vedit-editor-multi-cursor-selftest PRIVATE
    vedit_editor_core
    Qt6::Test
  )
  if(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
    target_include_directories(vedit-editor-multi-cursor-selftest PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/../app")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-editor-multi-cursor-selftest PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-editor-multi-cursor-selftest
           COMMAND vedit-editor-multi-cursor-selftest)
  set_tests_properties(vedit-editor-multi-cursor-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-edit"
    TIMEOUT 180
    SKIP_RETURN_CODE 77
  )
  message(STATUS "VEdit edit-core: multi-cursor/editing selftest enabled (T14)")
else()
  message(STATUS "VEdit edit-core: Qt6 Test missing -> multi-cursor/editing selftest NOT built")
endif()

# --- T16 (AC-3.3 / A-8): the local signatureHelp synthesiser ----------------
# ADDENDUM A-8 grants kernel/signature_help_selftest.cpp. The synthesiser needs
# the tree-sitter grammar the folding lane links; a tree without that lane still
# builds and the popup simply never appears (the selftest then reports it).
if(TARGET Qt6::Test)
  add_executable(vedit-signature-help-selftest kernel/signature_help_selftest.cpp)
  target_link_libraries(vedit-signature-help-selftest PRIVATE
    vedit_editor_core
    Qt6::Test
  )
  if(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
    target_include_directories(vedit-signature-help-selftest PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/../app")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-signature-help-selftest PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-signature-help-selftest
           COMMAND vedit-signature-help-selftest)
  set_tests_properties(vedit-signature-help-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-sig"
    TIMEOUT 180
    SKIP_RETURN_CODE 77
  )
  message(STATUS "VEdit edit-core: signatureHelp selftest enabled (T16)")
else()
  message(STATUS "VEdit edit-core: Qt6 Test missing -> signatureHelp selftest NOT built")
endif()

# T104: the FIM wiring has its own headless entry (real provider + real lane).
if(VEDIT_EDIT_CORE_HAVE_FIM)
  add_executable(vedit-fim-wiring-selftest kernel/fim_wiring_selftest.cpp)
  target_link_libraries(vedit-fim-wiring-selftest PRIVATE vedit_editor_core)
  if(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
    target_include_directories(vedit-fim-wiring-selftest PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/../app")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-fim-wiring-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-fim-wiring-selftest COMMAND vedit-fim-wiring-selftest --selfcheck)
  set_tests_properties(vedit-fim-wiring-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-fim"
    TIMEOUT 120
  )
endif()
