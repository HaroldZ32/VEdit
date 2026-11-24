# Rename lane (batch 20261110c): the engineering-wide (slang AST) rename — the
# `Project -> Rename Symbol in Project…` entry (AC-T13.1) plus the per-item
# preview / atomic apply / cancel dialog (AC-T13.2–13.5).
#
# src/app/CMakeLists.txt already lists `rename` in VEDIT_WORKSPACE_LANES and
# src/workspace/integration.cpp already calls installRename() behind
# VEDIT_WORKSPACE_HAVE_RENAME, so this directory existing is the whole wiring
# step. Neither file is edited by this task, and the directory is also the
# build's own discovery key (`<lane>/<lane>_integration.cpp`).
#
# The directory also builds stand-alone (`cmake -S src/workspace/rename -B …`)
# to compile and assert the PURE gate function; the shell half is compiled only
# when the product's `vedit_app_shell` target exists.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(VEditWorkspaceRename LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
  enable_testing()
endif()

# The preview dialog is a QObject subclass (Q_OBJECT); the product build already
# sets AUTOMOC for the lanes, the stand-alone build needs it here.
set(CMAKE_AUTOMOC ON)

add_library(vedit_workspace_rename STATIC
  rename_integration.cpp rename_integration.h
  rename_preview.cpp rename_preview.h)
target_include_directories(vedit_workspace_rename PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_rename PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)

if(TARGET vedit_app_shell)
  # The product build only: the command registry, the editor area and the status
  # bar live in the shell. A stand-alone build keeps the pure gate function.
  target_link_libraries(vedit_workspace_rename PUBLIC vedit_app_shell)
  target_compile_definitions(vedit_workspace_rename PRIVATE VEDIT_RENAME_HAVE_APP_SHELL=1)
  # The symbol under the cursor is read from the REAL product editor. The demo
  # fallback is a QPlainTextEdit, so both paths exist (the KTextEditor one is
  # compiled only when KF6::TextEditor is available, exactly like the
  # diagnostics lane's active-file watcher).
  if(TARGET KF6::TextEditor)
    target_link_libraries(vedit_workspace_rename PUBLIC KF6::TextEditor)
    target_compile_definitions(vedit_workspace_rename PRIVATE
      VEDIT_RENAME_HAVE_KTEXTEDITOR=1)
  endif()
endif()

if(TARGET vedit_workspace_diagnostics)
  # T12 already owns the ONE toolchain resolution order (bundled > local > PATH
  # > env); the gate reuses it instead of growing a second resolver. The link is
  # optional: without the diagnostics lane the gate answers with its own reason
  # (the compile definition below switches the message).
  target_link_libraries(vedit_workspace_rename PUBLIC vedit_workspace_diagnostics)
  target_compile_definitions(vedit_workspace_rename PRIVATE VEDIT_RENAME_HAVE_TOOLCHAIN=1)
endif()

if(TARGET vd_ffi)
  # The frozen rename C ABI: vd_rename_index_start/poll/cancel/release. The
  # product build creates the imported target in cmake/VEditRustFfi.cmake before
  # the lanes are added; a build without it keeps the command disabled with a
  # named reason instead of pretending to index.
  target_link_libraries(vedit_workspace_rename PUBLIC vd_ffi)
  target_compile_definitions(vedit_workspace_rename PRIVATE VEDIT_RENAME_HAVE_FFI=1)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_rename PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# --- the Xvfb+xcb UI probe (AC-T13.2–13.5 evidence) --------------------------
# The probe drives the REAL product path: the shell, the real editor, the rename
# command's handler, the real Rust job and the rendered dialog. It is NOT the
# LSP rename e2e (`vedit-rename-e2e` covers `diagnostics.rename` only) and never
# reads implementation state: every assertion is on a rendered widget text.
#
# Under `xvfb-run` with QT_QPA_PLATFORM=xcb (AGENT.md §9/§10's xcb rule for real
# GUI probes); without xvfb the test SKIPs (exit 77) with the reason printed.
if(TARGET vedit_app_shell AND TARGET vd_ffi AND TARGET vedit_workspace_diagnostics
   AND TARGET vedit_editor_core)
  add_executable(vedit-rename-ui-probe selftests/rename_ui_probe.cpp)
  # The header's Q_OBJECT is moc'd ONCE, in vedit_workspace_rename; the probe
  # only consumes the metaobject it links. Without this, AUTOMOC would generate
  # a second moc_rename_preview.cpp for the probe and the link would carry two
  # definitions of the same staticMetaObject.
  set_target_properties(vedit-rename-ui-probe PROPERTIES AUTOMOC OFF)
  target_link_libraries(vedit-rename-ui-probe PRIVATE
    vedit_workspace_rename
    vedit_editor_core
  )
  target_include_directories(vedit-rename-ui-probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../editor/kernel"
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-rename-ui-probe PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()

  find_program(VEDIT_RENAME_XVFB_RUN xvfb-run)
  if(VEDIT_RENAME_XVFB_RUN)
    add_test(NAME vedit-rename-ui-probe
             COMMAND ${VEDIT_RENAME_XVFB_RUN} -a -s "-screen 0 1280x1024x24"
                     $<TARGET_FILE:vedit-rename-ui-probe>
                     --fixture "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/fixtures/rename"
                     --mode preview)
    set_tests_properties(vedit-rename-ui-probe PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=xcb;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-rename;VEDIT_QUIET=1"
      SKIP_RETURN_CODE 77
      TIMEOUT 300
    )
  else()
    add_test(NAME vedit-rename-ui-probe
             COMMAND bash -c "echo 'SKIP: xvfb-run not available'; exit 77")
    set_tests_properties(vedit-rename-ui-probe PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()
