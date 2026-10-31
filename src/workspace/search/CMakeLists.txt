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

set(VEDIT_WORKSPACE_SEARCH_SOURCES
  global_search.cpp
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
target_include_directories(vedit_workspace_search PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_search PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)
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
