# Project panel lane (Slice 1, task T6 / acceptance AC-S1.3)
#
# The expanded project model: `crates/vd-config` resolves the project root, the
# `.f` filelists (`+incdir+`, `+define+`, nested `-f`, comments, bad-entry
# diagnostics) and `crates/vd-ffi/src/project.rs` exports that over the C ABI.
# This directory is the Qt side: one dock that shows the expanded file count and
# list, the diagnostics, and — through `project-model.json` in the state
# directory — the same project and members after a restart.
#
# Self-contained on purpose (same pattern as src/workspace/filetree and
# src/workspace/search):
#
#   cmake -S src/workspace/project -B target/project-build -G Ninja \
#     -DVEDIT_RUST_FFI_LIB="$PWD/target/release/libvd_ffi.a" \
#     -DVEDIT_RUST_FFI_INCLUDE="$PWD/crates/vd-ffi/include"
#   cmake --build target/project-build
#   ctest --test-dir target/project-build --output-on-failure
#
# Inside the root build `src/app/CMakeLists.txt` adds this directory as a
# workspace lane (target `vedit_workspace_project`, entry `installProject`), and
# `src/workspace/integration.cpp` calls the entry under the
# `VEDIT_WORKSPACE_HAVE_PROJECT` compile definition.
#
# There is no reduced mode without the Rust service: the lane has no fallback
# data path (C1/C4 forbid a second implementation in C++), so configure fails
# when libvd_ffi.a or vd_ffi.h is missing instead of silently building a panel
# that can never show anything.
cmake_minimum_required(VERSION 3.25)

# src/app adds this directory as a workspace lane; a standalone configure makes
# it the top-level project. The GLOBAL sentinel keeps a second inclusion a no-op.
get_property(VEDIT_WORKSPACE_PROJECT_ADDED GLOBAL PROPERTY VEDIT_WORKSPACE_PROJECT_ADDED)
if(VEDIT_WORKSPACE_PROJECT_ADDED)
  return()
endif()
set_property(GLOBAL PROPERTY VEDIT_WORKSPACE_PROJECT_ADDED TRUE)

set(VEDIT_WORKSPACE_PROJECT_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(VEDIT_WORKSPACE_PROJECT_STANDALONE ON)
  project(VEditProjectPanel LANGUAGES CXX)
  enable_testing()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

if(NOT TARGET vd_ffi)
  if(VEDIT_WORKSPACE_PROJECT_STANDALONE)
    # Default to the artifacts `scripts/build-qt.sh` produces.
    if(NOT VEDIT_RUST_FFI_LIB)
      set(VEDIT_RUST_FFI_LIB
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../target/release/libvd_ffi.a"
        CACHE FILEPATH "libvd_ffi.a produced by scripts/build-qt.sh or cargo build -p vd-ffi --release")
    endif()
    if(NOT VEDIT_RUST_FFI_INCLUDE)
      set(VEDIT_RUST_FFI_INCLUDE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../crates/vd-ffi/include"
        CACHE PATH "Directory containing the cbindgen-generated vd_ffi.h")
    endif()
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/VEditRustFfi.cmake")
  else()
    message(FATAL_ERROR
      "vedit_workspace_project needs the `vd_ffi` target: the root build links the Rust "
      "service layer (cmake/VEditRustFfi.cmake) before src/app.")
  endif()
endif()

set(VEDIT_WORKSPACE_PROJECT_SOURCES
  project_model.cpp
  project_panel.cpp
  project_service.cpp
  project_state.cpp
)
if(NOT VEDIT_WORKSPACE_PROJECT_STANDALONE)
  if(NOT TARGET vedit_app_shell)
    message(FATAL_ERROR
      "vedit_workspace_project needs vedit_app_shell: add src/app before the workspace lanes")
  endif()
  # The shell adapter is the only file that includes src/app; a standalone
  # configure (tests + widget checks) does not need it.
  list(APPEND VEDIT_WORKSPACE_PROJECT_SOURCES project_integration.cpp)
endif()

add_library(vedit_workspace_project STATIC ${VEDIT_WORKSPACE_PROJECT_SOURCES})
target_include_directories(vedit_workspace_project PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_project PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  vd_ffi
)
if(NOT VEDIT_WORKSPACE_PROJECT_STANDALONE)
  target_link_libraries(vedit_workspace_project PUBLIC vedit_app_shell)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_project PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-workspace-project-selftest project_selftest.cpp)
target_link_libraries(vedit-workspace-project-selftest PRIVATE vedit_workspace_project)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-workspace-project-selftest PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

# The repository-owned Verilog fixture (task T14) is the acceptance input.
set(VEDIT_PROJECT_FIXTURE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/fixtures/verilog-project")
add_test(NAME vedit-workspace-project-selftest
         COMMAND vedit-workspace-project-selftest --selfcheck "${VEDIT_PROJECT_FIXTURE}")
set_tests_properties(vedit-workspace-project-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-project"
  TIMEOUT 180
)

message(STATUS "VEdit project panel: Qt ${Qt6_VERSION} (standalone=${VEDIT_WORKSPACE_PROJECT_STANDALONE})")
