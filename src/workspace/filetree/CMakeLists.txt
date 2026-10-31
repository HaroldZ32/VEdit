# File tree panel -- workspace lane, task T21 (acceptance A2a).
#
# This directory is self-contained on purpose:
#   * `cmake -S src/workspace/filetree -B <dir>` configures it on its own
#     (that is how the lane produces its build + ctest + screenshot evidence
#     before the shell's panel seam is wired), and
#   * the same file is usable through `add_subdirectory()` from the root build
#     once the integration seam exists (docs/BUILD.md §12.4 pattern: the
#     including build only has to add the directory).
#
# The library never links KTextEditor/KSyntaxHighlighting: the tree classifies
# files itself (see file_kind.h for the "*.v is Verilog, not V/vlang" reason).
#
# This file follows the workspace-lane seam contract (evidence/ws-palette/SEAM.md):
# target `vedit_file_tree` (the mapping in src/app/CMakeLists.txt names it) and
# entry `installFileTree(...)` declared in filetree_integration.h. The app-shell
# adapter is compiled only when `vedit_app_shell` is part of the build, so an
# independent `cmake -S src/workspace/filetree ...` never needs src/app.

cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VEditFileTree LANGUAGES CXX)
  set(VEDIT_FILETREE_STANDALONE ON)
  enable_testing()
else()
  set(VEDIT_FILETREE_STANDALONE OFF)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
message(STATUS "VEdit file tree: Qt ${Qt6_VERSION} (standalone=${VEDIT_FILETREE_STANDALONE})")

set(VEDIT_FILETREE_SOURCES
  file_kind.cpp
  file_ops.cpp
  file_tree_model.cpp
  file_tree_widget.cpp
  filetree_panel.cpp
)

add_library(vedit_file_tree STATIC ${VEDIT_FILETREE_SOURCES})
target_include_directories(vedit_file_tree PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_file_tree PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_file_tree PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# The shell adapter needs src/app; the core library must not (SEAM.md §2.4).
# `add_subdirectory()` therefore has to happen after `add_library(vedit_app_shell
# ...)` for the seam to be live; a configure-time line says which branch ran.
if(TARGET vedit_app_shell)
  target_sources(vedit_file_tree PRIVATE filetree_integration.cpp)
  target_link_libraries(vedit_file_tree PUBLIC vedit_app_shell)
  target_compile_definitions(vedit_file_tree PRIVATE VEDIT_FILETREE_HAVE_APP_SHELL=1)
  message(STATUS "VEdit file tree: vedit_app_shell present -> installFileTree() is active")
else()
  message(STATUS "VEdit file tree: independent build (no vedit_app_shell) -> filetree_integration.cpp not compiled")
endif()

# The demo and the ctest suite are lane tooling, not product code: a product
# build that only wants the panel can switch them off.
option(VEDIT_BUILD_FILETREE_TESTS
  "Build the file tree demo and its ctest suite (lane evidence tooling)"
  ${VEDIT_FILETREE_STANDALONE})

if(VEDIT_BUILD_FILETREE_TESTS)
  find_package(Qt6 REQUIRED COMPONENTS Test)

  add_executable(vedit-filetree-demo demo/filetree_demo.cpp)
  target_link_libraries(vedit-filetree-demo PRIVATE vedit_file_tree Qt6::Core Qt6::Gui Qt6::Widgets)

  add_executable(vedit-filetree-tests tests/filetree_tests.cpp)
  target_link_libraries(vedit-filetree-tests PRIVATE vedit_file_tree Qt6::Test)

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-filetree-demo PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(vedit-filetree-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()

  add_test(NAME vedit-filetree-tests COMMAND vedit-filetree-tests)
  set_tests_properties(vedit-filetree-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    TIMEOUT 300
  )
endif()
