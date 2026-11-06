# VEdit diagnostics lane (workspace lane, task T8 / acceptance AC-S1.4)
#
# This directory is self-contained: configured on its own it builds the lane
# library and the headless self-test (fakes only), and when a Rust FFI static
# library is given it also builds the real-server self-test.
#
#   cmake -S src/workspace/diagnostics -B target/diagnostics-build -G Ninja
#   cmake --build target/diagnostics-build
#   ctest --test-dir target/diagnostics-build --output-on-failure
#
#   # with the Rust C ABI (real Verible server + SLANG service):
#   cmake -S src/workspace/diagnostics -B target/diagnostics-build -G Ninja \
#         -DVEDIT_RUST_FFI_LIB=target/release/libvd_ffi.a \
#         -DVEDIT_RUST_FFI_INCLUDE=crates/vd-ffi/include
#
# Inside the root project src/app adds this directory as a workspace lane; the
# app-shell target contributes `vedit_app_shell` and the `vd_ffi` target is
# already imported by VEditRustFfi. The GLOBAL sentinel makes a second inclusion
# a no-op (same convention as src/workspace/search).
cmake_minimum_required(VERSION 3.25)

get_property(VEDIT_WORKSPACE_DIAGNOSTICS_ADDED GLOBAL PROPERTY VEDIT_WORKSPACE_DIAGNOSTICS_ADDED)
if(VEDIT_WORKSPACE_DIAGNOSTICS_ADDED)
  return()
endif()
set_property(GLOBAL PROPERTY VEDIT_WORKSPACE_DIAGNOSTICS_ADDED TRUE)

set(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vedit_workspace_diagnostics LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_AUTOMOC ON)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
else()
  if(NOT TARGET vedit_app_shell)
    message(FATAL_ERROR
      "vedit_workspace_diagnostics needs the app-shell target (vedit_app_shell): "
      "add src/app before src/workspace/diagnostics in the root build")
  endif()
  set(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL ON)
endif()

set(CMAKE_AUTOMOC ON)

# --- Rust C ABI (optional in a standalone build, present in the product) -----
set(VEDIT_DIAGNOSTICS_FFI OFF)
set(VEDIT_RUST_FFI_LIB "" CACHE FILEPATH "libvd_ffi.a (standalone builds)")
set(VEDIT_RUST_FFI_INCLUDE "" CACHE PATH "directory containing vd_ffi.h")
if(TARGET vd_ffi)
  set(VEDIT_DIAGNOSTICS_FFI ON)
elseif(VEDIT_RUST_FFI_LIB AND EXISTS "${VEDIT_RUST_FFI_LIB}"
       AND VEDIT_RUST_FFI_INCLUDE AND EXISTS "${VEDIT_RUST_FFI_INCLUDE}/vd_ffi.h")
  add_library(vd_ffi STATIC IMPORTED)
  set_target_properties(vd_ffi PROPERTIES
    IMPORTED_LOCATION "${VEDIT_RUST_FFI_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${VEDIT_RUST_FFI_INCLUDE}")
  find_package(Threads REQUIRED)
  target_link_libraries(vd_ffi INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
  set(VEDIT_DIAGNOSTICS_FFI ON)
endif()

set(VEDIT_DIAGNOSTICS_SOURCES
  autofix.cpp
  diagnostic_hover.cpp
  diagnostics_controller.cpp
  diagnostics_outline_model.cpp
  ffi_backends.cpp
  lint_progress.cpp
  project_lint_trigger.cpp
  symbol_scan.cpp
  toolchain.cpp
)
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)
  list(APPEND VEDIT_DIAGNOSTICS_SOURCES
    autofix_document.cpp
    diagnostics_panel.cpp
    diagnostics_integration.cpp
    project_lint_notice.cpp
  )
endif()

add_library(vedit_workspace_diagnostics STATIC ${VEDIT_DIAGNOSTICS_SOURCES})
# RM-9: the ONE file-status vocabulary (src/common/file_status.h) is header-only,
# like src/common/path_display.h; this lane is its consumer alongside the file
# tree and the Project panel (both already carry this include path).
target_include_directories(vedit_workspace_diagnostics PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../common")
target_link_libraries(vedit_workspace_diagnostics PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)
  target_link_libraries(vedit_workspace_diagnostics PUBLIC vedit_app_shell)
  target_compile_definitions(vedit_workspace_diagnostics PRIVATE
    VEDIT_DIAGNOSTICS_HAVE_APP_SHELL=1)
  # T118: the active-file watcher reads the editor buffer. The product editor is
  # a KTextEditor::View; the demo fallback is a QPlainTextEdit.
  find_package(KF6TextEditor REQUIRED)
  target_link_libraries(vedit_workspace_diagnostics PUBLIC KF6::TextEditor)
  # T131: the inline annotation model lives in the editor kernel (it installs
  # itself per view); the diagnostics lane only pushes entries into it.
  if(TARGET vedit_editor_core)
    target_link_libraries(vedit_workspace_diagnostics PUBLIC vedit_editor_core)
    target_include_directories(vedit_workspace_diagnostics PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/../../editor/kernel")
    target_compile_definitions(vedit_workspace_diagnostics PRIVATE
      VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS=1)
  endif()
  target_compile_definitions(vedit_workspace_diagnostics PRIVATE
    VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR=1)
endif()
if(VEDIT_DIAGNOSTICS_FFI)
  target_link_libraries(vedit_workspace_diagnostics PUBLIC vd_ffi)
  target_compile_definitions(vedit_workspace_diagnostics PRIVATE
    VEDIT_DIAGNOSTICS_HAVE_FFI=1)
  message(STATUS "VEdit: diagnostics lane links the Rust service layer (vd-ffi)")
else()
  message(STATUS "VEdit: diagnostics lane built WITHOUT the Rust service layer (fakes only)")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_diagnostics PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

# --- Headless self-test (fakes; always built) --------------------------------
add_executable(vedit-workspace-diagnostics-selftest diagnostics_selftest.cpp)
target_link_libraries(vedit-workspace-diagnostics-selftest PRIVATE
  vedit_workspace_diagnostics
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-workspace-diagnostics-selftest PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
# T8: the project-pass probe (cancellation, dedup, 1k-file interactivity) uses a
# fake backend, so it needs no toolchain and no Rust service. U8b: it lives in
# selftests/ beside the lane (its output is recorded under evidence/); a LIVING
# test must not be edited as if it were a closed evidence artefact.
set(VEDIT_LINT_PROBE_SOURCE
  "${CMAKE_CURRENT_SOURCE_DIR}/selftests/lint_orchestration_probe.cpp")
if(EXISTS "${VEDIT_LINT_PROBE_SOURCE}")
  add_executable(vedit-lint-orchestration-probe ${VEDIT_LINT_PROBE_SOURCE})
  target_link_libraries(vedit-lint-orchestration-probe PRIVATE vedit_workspace_diagnostics)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-lint-orchestration-probe PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-lint-orchestration-probe COMMAND vedit-lint-orchestration-probe)
  set_tests_properties(vedit-lint-orchestration-probe PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-lint"
    TIMEOUT 180
  )
endif()

add_test(NAME vedit-workspace-diagnostics-selftest
         COMMAND vedit-workspace-diagnostics-selftest --selfcheck)

# --- RM-9 (T20): per-file verdict retention (fakes; standalone-buildable) ----
# The user-visible defect: a dot existed only for the document being displayed.
# This target builds WITHOUT the app shell/panel (it needs only the controller),
# so the acceptance property is reproducible on its own:
#   * a verdict is retained per file across a tab switch,
#   * every file of the project set gets a status (opened or not),
#   * "not analysed yet / excluded" is a distinct, visible state (4),
#   * an edit voids the verdict (never a green dot on a voided verdict), and a
#     fresh analysis of the dirty buffer wins.
add_executable(vedit-verdict-retention-selftest selftests/verdict_retention_selftest.cpp)
target_link_libraries(vedit-verdict-retention-selftest PRIVATE vedit_workspace_diagnostics)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-verdict-retention-selftest PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-verdict-retention-selftest COMMAND vedit-verdict-retention-selftest)
set_tests_properties(vedit-verdict-retention-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
  TIMEOUT 120
)

# --- Toolchain observability self-test (T82) --------------------------------
# The "missing" case is deterministic (an empty staged AppDir), so it belongs in
# ctest. The "bundled" case needs the assembled AppDir and is run by
# evidence/toolchain-bundling/ on the delivery payload.
add_executable(vedit-toolchain-selftest toolchain_selftest.cpp)
target_link_libraries(vedit-toolchain-selftest PRIVATE vedit_workspace_diagnostics)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-toolchain-selftest PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/toolchain-missing-appdir")
add_test(NAME vedit-toolchain-missing-selftest
         COMMAND vedit-toolchain-selftest
                 --appdir "${CMAKE_CURRENT_BINARY_DIR}/toolchain-missing-appdir"
                 --expect missing)
set_tests_properties(vedit-toolchain-missing-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics"
  TIMEOUT 60
)
set_tests_properties(vedit-workspace-diagnostics-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics"
  TIMEOUT 120
)

# --- Real-server self-test (only when the Rust service layer is linked) ------
if(VEDIT_DIAGNOSTICS_FFI)
  add_executable(vedit-diagnostics-ffi-selftest diagnostics_ffi_selftest.cpp)
  target_link_libraries(vedit-diagnostics-ffi-selftest PRIVATE
    vedit_workspace_diagnostics
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-diagnostics-ffi-selftest PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  set(VEDIT_DIAGNOSTICS_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/fixtures/verilog-project")
  set(VEDIT_DIAGNOSTICS_VERIBLE "${CMAKE_CURRENT_SOURCE_DIR}/../../../.mpd/recon/verible/bin/verible-verilog-ls")
  add_test(NAME vedit-diagnostics-ffi-selftest
           COMMAND vedit-diagnostics-ffi-selftest
                   "${VEDIT_DIAGNOSTICS_FIXTURE}" "${VEDIT_DIAGNOSTICS_VERIBLE}")
  set_tests_properties(vedit-diagnostics-ffi-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics"
    TIMEOUT 240
  )
endif()

# --- Product-seam self-test (T118): open a file, edit the BUFFER, see it ----
# Builds the real ApplicationShell, calls installDiagnostics() exactly like
# vedit-app (no --project), opens a file through the document seam and edits the
# buffer only. SKIPs when the real verible-verilog-ls is not resolvable.
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL AND VEDIT_DIAGNOSTICS_FFI)
  add_executable(vedit-diagnostics-product-seam selftests/diagnostics_product_seam.cpp)
  target_link_libraries(vedit-diagnostics-product-seam PRIVATE
    vedit_workspace_diagnostics
    vedit_app_shell
  )
  target_include_directories(vedit-diagnostics-product-seam PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../editor/kernel"
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-diagnostics-product-seam PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-diagnostics-product-seam
           COMMAND vedit-diagnostics-product-seam)
  set_tests_properties(vedit-diagnostics-product-seam PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
    TIMEOUT 180
  )
endif()

# --- UX batch 2 / B3: the diagnostics experience (R2/R3 + items 9/10/12) -----
# Offscreen; the two backends and the project pass are fakes. The engine-driven
# autofix legs use the REAL verible-verilog-lint and SKIP explicitly when it is
# not resolvable, so the suite is honest in every environment.
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL AND TARGET vedit_file_tree)
  set(VEDIT_DIAGNOSTICS_VERIBLE_LINT
      "${CMAKE_CURRENT_SOURCE_DIR}/../../../.mpd/recon/verible/bin/verible-verilog-lint")
  add_executable(vedit-lint-experience-selftest
                 selftests/lint_experience_selftest.cpp)
  target_link_libraries(vedit-lint-experience-selftest PRIVATE
    vedit_workspace_diagnostics
    vedit_file_tree
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-lint-experience-selftest PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-lint-experience-selftest
           COMMAND vedit-lint-experience-selftest
                   --verible-lint "${VEDIT_DIAGNOSTICS_VERIBLE_LINT}")
  set_tests_properties(vedit-lint-experience-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
    TIMEOUT 180
  )
endif()

# --- UX batch 2 / B5: the Rename end-to-end proof ----------------------------
# The REAL menu action, the REAL Rust LSP session and the REAL verible server;
# the name dialog is filled from the event loop. SKIPs (exit 0) when the server
# binary is absent, so the suite stays honest on a bare machine.
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL AND VEDIT_DIAGNOSTICS_FFI)
  set(VEDIT_DIAGNOSTICS_VERIBLE_LS
      "${CMAKE_CURRENT_SOURCE_DIR}/../../../.mpd/recon/verible/bin/verible-verilog-ls")
  add_executable(vedit-rename-e2e selftests/rename_e2e.cpp)
  target_link_libraries(vedit-rename-e2e PRIVATE
    vedit_workspace_diagnostics
  )
  target_include_directories(vedit-rename-e2e PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../editor/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app"
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-rename-e2e PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-rename-e2e
           COMMAND vedit-rename-e2e --verible-ls "${VEDIT_DIAGNOSTICS_VERIBLE_LS}")
  set_tests_properties(vedit-rename-e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
    TIMEOUT 240
  )
endif()

# --- UX batch 2 / item 12: the presentation, end to end in the real shell ----
# Explicit `project.lint` -> modal jumpable list; a save-triggered pass -> the
# clickable status-bar notice and NO dialog. SKIPs (exit 0) when the toolchain
# or the project file set never arrives.
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL AND VEDIT_DIAGNOSTICS_FFI)
  add_executable(vedit-project-lint-e2e selftests/project_lint_e2e.cpp)
  target_link_libraries(vedit-project-lint-e2e PRIVATE
    vedit_workspace_diagnostics
  )
  target_include_directories(vedit-project-lint-e2e PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../editor/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app"
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-project-lint-e2e PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-project-lint-e2e COMMAND vedit-project-lint-e2e)
  set_tests_properties(vedit-project-lint-e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
    TIMEOUT 300
  )
endif()

# --- UX batch 2 / B3 repair: the PRODUCT wiring, in the real shell ----------
# The independent review (evidence/ux-batch-2/review-b3/REPORT.md) failed the
# lane because the failures were in the product wiring, not in the controller.
# This binary drives the real shell + installDiagnostics with a fake project
# pass: hover-popup lifetime (tab switch + view destruction), R2 over EVERY open
# document, the C8 content rule, projectRootChanged and the modal counter.
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL AND VEDIT_DIAGNOSTICS_FFI)
  add_executable(vedit-b3-wiring-e2e selftests/b3_wiring_e2e.cpp)
  target_link_libraries(vedit-b3-wiring-e2e PRIVATE
    vedit_workspace_diagnostics
  )
  if(TARGET vedit_file_tree)
    target_link_libraries(vedit-b3-wiring-e2e PRIVATE vedit_file_tree)
  endif()
  target_include_directories(vedit-b3-wiring-e2e PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../editor/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app"
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-b3-wiring-e2e PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-b3-wiring-e2e COMMAND vedit-b3-wiring-e2e)
  set_tests_properties(vedit-b3-wiring-e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
    TIMEOUT 300
  )
endif()

# --- UX batch 2 / RM-2: switching projects in ONE window --------------------
# The real-machine defect: after switching folders in the same window the file
# set and the backends' -I/-D still described the previous project. This binary
# drives the real shell + project + file-tree + diagnostics lanes, switches
# A -> B -> A -> B -> A through the new top-level entry, and asserts the NEW
# root's set and environment on every switch, exactly one expansion per switch,
# and that an unsaved tab survives. Headless.
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL AND VEDIT_DIAGNOSTICS_FFI
   AND TARGET vedit_file_tree AND TARGET vedit_workspace_project)
  add_executable(vedit-rm2-project-switch-e2e selftests/rm2_project_switch_e2e.cpp)
  target_link_libraries(vedit-rm2-project-switch-e2e PRIVATE
    vedit_workspace_diagnostics
    vedit_file_tree
    vedit_workspace_project
  )
  target_include_directories(vedit-rm2-project-switch-e2e PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../editor/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app"
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-rm2-project-switch-e2e PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-rm2-project-switch-e2e COMMAND vedit-rm2-project-switch-e2e)
  set_tests_properties(vedit-rm2-project-switch-e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
    TIMEOUT 300
  )
endif()

# --- UX batch 2 / RM-9: the status dots over the WHOLE project ---------------
# The real shell + project + file-tree + diagnostics lanes, with a deterministic
# fake pass installed BEFORE the first event-loop turn (a project open starts the
# full pass at adoption). Asserts the coverage policy, the never-opened files'
# verdicts, the filelist-only file in the tree group AND the panel list, and the
# dirty-buffer exclusion. Offscreen; `--shot` saves the window as evidence.
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL AND VEDIT_DIAGNOSTICS_FFI
   AND TARGET vedit_file_tree AND TARGET vedit_workspace_project)
  add_executable(vedit-rm9-status-dots-e2e selftests/rm9_status_dots_e2e.cpp)
  target_link_libraries(vedit-rm9-status-dots-e2e PRIVATE
    vedit_workspace_diagnostics
    vedit_file_tree
    vedit_workspace_project
  )
  target_include_directories(vedit-rm9-status-dots-e2e PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../editor/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app"
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-rm9-status-dots-e2e PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-rm9-status-dots-e2e COMMAND vedit-rm9-status-dots-e2e)
  set_tests_properties(vedit-rm9-status-dots-e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
    TIMEOUT 300
  )
endif()

# --- UX batch 2 / RM-17b: the top scope, end to end -------------------------
# The real shell + project + diagnostics lanes, a deterministic pass backend and
# the fixture top A (B reachable, C not). The scope is published through the REAL
# seam (src/workspace/top_scope.h, exactly what the module tree calls), and the
# lint-request boundary is observed on the backend: the pass must not contain the
# excluded file, that file must stay hollow "not analysed" (never green), the
# aggregate line must name the top, the project list must keep it, and Auto must
# restore it. A second scenario runs the REALISTIC order (project-open pass
# covers everything, THEN the top is named) - the order the app actually has.
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL AND VEDIT_DIAGNOSTICS_FFI
   AND TARGET vedit_workspace_project)
  add_executable(vedit-rm17b-top-scope-e2e selftests/top_scope_e2e.cpp)
  target_link_libraries(vedit-rm17b-top-scope-e2e PRIVATE
    vedit_workspace_diagnostics
    vedit_workspace_project
  )
  target_include_directories(vedit-rm17b-top-scope-e2e PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../editor/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app"
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-rm17b-top-scope-e2e PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-rm17b-top-scope-e2e COMMAND vedit-rm17b-top-scope-e2e)
  set_tests_properties(vedit-rm17b-top-scope-e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
    TIMEOUT 300
  )
endif()

# --- UX batch 2 / B3 repair: the new pure APIs (rename validation, async probe)
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)
  set(VEDIT_B3_REPAIR_VERIBLE_LINT
      "${CMAKE_CURRENT_SOURCE_DIR}/../../../.mpd/recon/verible/bin/verible-verilog-lint")
  add_executable(vedit-b3-repair-api-selftest selftests/b3_repair_api_selftest.cpp)
  target_link_libraries(vedit-b3-repair-api-selftest PRIVATE vedit_workspace_diagnostics)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-b3-repair-api-selftest PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-b3-repair-api-selftest
           COMMAND vedit-b3-repair-api-selftest
                   --verible-lint "${VEDIT_B3_REPAIR_VERIBLE_LINT}")
  set_tests_properties(vedit-b3-repair-api-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics;VEDIT_QUIET=1"
    TIMEOUT 180
  )
endif()

# --- Toolchain UX self-test (T85): ONE panel-level status, not a wall --------
# Needs the panel widget (built with the app-shell seam) and, for the real
# outline-symbol leg, the Rust service layer. It is offscreen and fast.
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)
  add_executable(vedit-toolchain-ux-selftest toolchain_ux_selftest.cpp)
  target_link_libraries(vedit-toolchain-ux-selftest PRIVATE
    vedit_workspace_diagnostics
  )
  if(VEDIT_DIAGNOSTICS_FFI)
    target_compile_definitions(vedit-toolchain-ux-selftest PRIVATE
      VEDIT_DIAGNOSTICS_HAVE_FFI=1)
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-toolchain-ux-selftest PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-toolchain-ux-selftest
           COMMAND vedit-toolchain-ux-selftest
                   --fixture "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/fixtures/verilog-project")
  set_tests_properties(vedit-toolchain-ux-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-diagnostics"
    TIMEOUT 240
  )
endif()
