# Global search (workspace lane, task T23 / acceptance A2c)
#
# This directory is self-contained: configured on its own it builds the library,
# the self-check harness and the ctest entry, and pulls the app shell in from
# ../../app so the PanelRegistry factory seam is exercised for real:
#
#   cmake -S src/workspace/search -B target/search-build -G Ninja
#   cmake --build target/search-build
#   ctest --test-dir target/search-build --output-on-failure
#
# Inside the root project the directory is expected to be added AFTER src/app
# (the app shell contributes vedit_app_shell); the generic lane seam is being
# provided by the app-shell lane (evidence/ws-palette/SEAM.md), see README.md.
cmake_minimum_required(VERSION 3.25)

# src/app adds this directory as a workspace lane. When this lane is the
# top-level project (standalone build below) that would recurse through
# ../../app, so a second inclusion must be a no-op. The property is a GLOBAL
# sentinel, not a per-directory variable: both inclusions share the same source
# directory.
get_property(VEDIT_WORKSPACE_SEARCH_ADDED GLOBAL PROPERTY VEDIT_WORKSPACE_SEARCH_ADDED)
if(VEDIT_WORKSPACE_SEARCH_ADDED)
  return()
endif()
set_property(GLOBAL PROPERTY VEDIT_WORKSPACE_SEARCH_ADDED TRUE)

set(VEDIT_WORKSPACE_SEARCH_HAVE_SHELL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vedit_workspace_search LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_AUTOMOC ON)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)
  # Standalone builds the core lane only. The seam test and the screenshots need
  # vedit_app_shell; adding src/app from here would make src/app both a parent
  # and a child directory of this lane (CMake directory cycle), so the shell
  # path is exercised by the root build, which is what the repo gate runs.
else()
  if(NOT TARGET vedit_app_shell)
    message(FATAL_ERROR
      "vedit_workspace_search needs the app-shell target (vedit_app_shell): "
      "add src/app before src/workspace/search in the root build")
  endif()
  set(VEDIT_WORKSPACE_SEARCH_HAVE_SHELL ON)
endif()

# In the root build the parent already found Qt6/KF6 and enabled testing; only
# the Qt6 Test module is new here.
find_package(Qt6 REQUIRED COMPONENTS Test)

set(CMAKE_AUTOMOC ON)

# The Rust search service (T21): the index and the matcher are `vd-search`; this
# lane only consumes the C ABI. Standalone builds may point at the archive like
# the diagnostics lane does; a build without the Rust layer still compiles but
# GlobalSearch reports a typed failure instead of growing a Qt implementation.
set(VEDIT_RUST_FFI_LIB "" CACHE FILEPATH "libvd_ffi.a (standalone builds)")
set(VEDIT_RUST_FFI_INCLUDE "" CACHE PATH "directory containing vd_ffi.h")
if(TARGET vd_ffi)
  set(VEDIT_WORKSPACE_SEARCH_HAVE_FFI ON)
elseif(VEDIT_RUST_FFI_LIB AND EXISTS "${VEDIT_RUST_FFI_LIB}"
       AND VEDIT_RUST_FFI_INCLUDE AND EXISTS "${VEDIT_RUST_FFI_INCLUDE}/vd_ffi.h")
  add_library(vd_ffi STATIC IMPORTED)
  set_target_properties(vd_ffi PROPERTIES
    IMPORTED_LOCATION "${VEDIT_RUST_FFI_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${VEDIT_RUST_FFI_INCLUDE}")
  include(CMakeFindDependencyMacro)
  find_dependency(Threads)
  target_link_libraries(vd_ffi INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
  set(VEDIT_WORKSPACE_SEARCH_HAVE_FFI ON)
else()
  set(VEDIT_WORKSPACE_SEARCH_HAVE_FFI OFF)
endif()

set(VEDIT_WORKSPACE_SEARCH_SOURCES
  document_sync.cpp
  global_search.cpp
  project_replace.cpp
  search_panel.cpp
  search_types.cpp
)
if(VEDIT_WORKSPACE_SEARCH_HAVE_SHELL)
  # The lane entry point needs src/app headers; only the root build has them.
  # search_jump_target.cpp is the product consumer of jumpRequested (verdict H1)
  # and lives on the same terms: it opens through the shell's editor pane.
  list(APPEND VEDIT_WORKSPACE_SEARCH_SOURCES search_integration.cpp search_jump_target.cpp)
endif()

add_library(vedit_workspace_search STATIC ${VEDIT_WORKSPACE_SEARCH_SOURCES})
# The shared path-display rule (src/common/path_display.h), header-only.
target_include_directories(vedit_workspace_search PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../common")
# A-1/T20: the panel routes every result click through the application-wide jump
# seam (src/workspace/jump) instead of its own IJumpTarget. The jump lane is
# configured early when it is not a target yet (same pattern as the breadcrumb
# lane); its GLOBAL guard makes the later lane-list inclusion a no-op.
if(NOT TARGET vedit_workspace_jump AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../jump/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../jump" "${CMAKE_CURRENT_BINARY_DIR}/jump-lane")
endif()
if(NOT TARGET vedit_workspace_jump)
  message(FATAL_ERROR
    "vedit_workspace_search needs the jump lane (src/workspace/jump): the panel has no second "
    "jump implementation")
endif()
target_link_libraries(vedit_workspace_search PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  vedit_workspace_jump
)
if(VEDIT_WORKSPACE_SEARCH_HAVE_FFI)
  target_link_libraries(vedit_workspace_search PUBLIC vd_ffi)
  target_compile_definitions(vedit_workspace_search PRIVATE VEDIT_SEARCH_HAVE_FFI=1)
endif()
if(VEDIT_WORKSPACE_SEARCH_HAVE_SHELL)
  target_link_libraries(vedit_workspace_search PUBLIC vedit_app_shell)
  # The jump target opens through the ONE shell-level seam
  # (EditorPane::openFile(), src/app/editor_area.h) and then positions the
  # cursor in the product's editing kernel. Regression T69: it must NOT cast the
  # pane widget to the demo QPlainTextEdit - the product installs KTextEditor.
  if(TARGET vedit_editor_core)
    target_link_libraries(vedit_workspace_search PUBLIC vedit_editor_core)
    target_compile_definitions(vedit_workspace_search PRIVATE
      VEDIT_SEARCH_HAVE_EDITOR_CORE=1)
  endif()
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_search PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-workspace-search-selftest search_selftest.cpp)
target_link_libraries(vedit-workspace-search-selftest PRIVATE
  vedit_workspace_search
  Qt6::Test
)
if(VEDIT_WORKSPACE_SEARCH_HAVE_SHELL)
  target_link_libraries(vedit-workspace-search-selftest PRIVATE vedit_app_shell)
  target_compile_definitions(vedit-workspace-search-selftest PRIVATE
    VEDIT_WORKSPACE_SEARCH_HAVE_SHELL=1)
  # Regression T69: the product-jump checks run against the real KTextEditor
  # provider, so the harness needs the kernel's document/view accessors.
  if(TARGET vedit_editor_core)
    target_link_libraries(vedit-workspace-search-selftest PRIVATE vedit_editor_core)
    target_compile_definitions(vedit-workspace-search-selftest PRIVATE
      VEDIT_SEARCH_HAVE_EDITOR_CORE=1)
  endif()
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-workspace-search-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Test-only stall shim (tools/slowio): simulates slow storage for files whose
# name ends with .slowio, so the GlobalSearch destructor lifetime test (T64) can
# make the scanner block inside read() on purpose. It is lane test tooling: it is
# never linked into vedit-app nor into vedit_workspace_search - the self-check
# re-executes itself with this library in LD_PRELOAD.
add_library(vedit_search_slowio SHARED tools/slowio/slowio.cpp)
target_link_libraries(vedit_search_slowio PRIVATE ${CMAKE_DL_LIBS})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_search_slowio PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
target_compile_definitions(vedit-workspace-search-selftest PRIVATE
  VEDIT_SEARCH_SLOWIO_LIB="$<TARGET_FILE:vedit_search_slowio>")
# The definition above only creates a file-level dependency; building just the
# self-check must still produce the shim it re-executes with LD_PRELOAD.
add_dependencies(vedit-workspace-search-selftest vedit_search_slowio)

add_test(NAME vedit-workspace-search-selftest
         COMMAND vedit-workspace-search-selftest --selfcheck)
set_tests_properties(vedit-workspace-search-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-search"
  TIMEOUT 180
)

# --- ux-batch-2 B2: shared option model, hand-off, project-wide replace ------
# One offscreen harness for the six acceptance points of the lane: option-set
# parity of the two surfaces, replace-one, replace-all (counts + one write per
# file), regex/whole-word/case, the Ctrl+F whole-project hand-off, and the
# unsaved-buffer rule. It links the real KTextEditor when the kernel provides it
# (the bar and the unsaved-buffer check need a real document/view), otherwise
# those checks report SKIP.
add_executable(vedit-search-replace-tests search_replace_selftest.cpp)
target_link_libraries(vedit-search-replace-tests PRIVATE
  vedit_workspace_search
  Qt6::Test
)
if(TARGET vedit_editor_core)
  target_link_libraries(vedit-search-replace-tests PRIVATE vedit_editor_core)
  target_compile_definitions(vedit-search-replace-tests PRIVATE
    VEDIT_SEARCH_HAVE_EDITOR_CORE=1)
endif()
if(VEDIT_WORKSPACE_SEARCH_HAVE_SHELL)
  # The product wiring check: the Ctrl+F hand-off reaches the dock through
  # installSearch()'s handler (the dock is created on demand by the shell).
  target_link_libraries(vedit-search-replace-tests PRIVATE vedit_app_shell)
  target_compile_definitions(vedit-search-replace-tests PRIVATE
    VEDIT_SEARCH_HAVE_SHELL=1)
endif()
if(VEDIT_WORKSPACE_SEARCH_HAVE_FFI)
  target_compile_definitions(vedit-search-replace-tests PRIVATE
    VEDIT_SEARCH_HAVE_FFI=1)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-search-replace-tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(VEDIT_WORKSPACE_SEARCH_HAVE_FFI)
  add_test(NAME vedit-search-replace-tests
           COMMAND vedit-search-replace-tests --selfcheck
                   --evidence "${CMAKE_CURRENT_BINARY_DIR}/search-replace-evidence")
  set_tests_properties(vedit-search-replace-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-search"
    TIMEOUT 180
  )
else()
  message(STATUS
    "VEdit: vedit-search-replace-tests built without the Rust search service; ctest entry skipped")
endif()
