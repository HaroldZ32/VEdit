# Scala one-click run lane (batch 20261110, AC-7.5 / AC-7.10).
#
# A production lane of the product build: src/app/CMakeLists.txt already lists
# `scala` in VEDIT_WORKSPACE_LANES and src/workspace/integration.cpp already calls
# installScala() behind VEDIT_WORKSPACE_HAVE_SCALA, so this directory existing is
# the whole wiring step (that file and the lane list are NOT edited by this task).
# It also builds stand-alone (`cmake -S src/workspace/scala -B <build>`) for the
# lane's own verification, the same convention as the other workspace lanes.
#
# Deliberately NO add_test() here: the frozen write scope of this task names the
# five lane files, not a test source, so the lane's behavioural probe is recorded
# in evidence/batch-20261110c/scala/01-run.log instead of a second code path that
# nobody else owns.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(VEditWorkspaceScala LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
  enable_testing()
endif()

set(CMAKE_AUTOMOC ON)

set(VEDIT_WORKSPACE_SCALA_SOURCES
  scala_run.cpp
)
if(TARGET vedit_app_shell)
  # Only the product build has the shell; the stand-alone lane keeps the core.
  list(APPEND VEDIT_WORKSPACE_SCALA_SOURCES scala_integration.cpp)
endif()

add_library(vedit_workspace_scala STATIC ${VEDIT_WORKSPACE_SCALA_SOURCES})
target_include_directories(vedit_workspace_scala PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_scala PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
if(TARGET vedit_app_shell)
  target_link_libraries(vedit_workspace_scala PUBLIC vedit_app_shell)
  target_compile_definitions(vedit_workspace_scala PRIVATE VEDIT_SCALA_HAVE_SHELL=1)
endif()
if(TARGET vedit_terminal)
  # AC-7.5: the output goes into the EXISTING Terminal panel, so the lane needs
  # that panel's widget (and must not grow a second console).
  target_link_libraries(vedit_workspace_scala PUBLIC vedit_terminal)
  target_compile_definitions(vedit_workspace_scala PRIVATE VEDIT_SCALA_HAVE_TERMINAL=1)
endif()
if(TARGET vedit_workspace_diagnostics)
  # T12 already resolves java / scala-cli / sbt with the product-wide order; the
  # run lane reuses that resolver instead of copying it.
  target_link_libraries(vedit_workspace_scala PUBLIC vedit_workspace_diagnostics)
  target_compile_definitions(vedit_workspace_scala PRIVATE VEDIT_SCALA_HAVE_TOOLCHAIN=1)
endif()

# --- the Rust engine (frozen C ABI) -----------------------------------------
# vd_scala_run_start/poll/cancel/release own the child, the streamed output, the
# exit code and the process-tree kill. In the product build the root CMakeLists
# has already created the imported `vd_ffi` target (cmake/VEditRustFfi.cmake), so
# linking it here is one line; a stand-alone build resolves the same library from
# the checkout (or the -DVEDIT_RUST_FFI_LIB / -DVEDIT_RUST_FFI_INCLUDE cache vars).
if(TARGET vd_ffi)
  target_link_libraries(vedit_workspace_scala PUBLIC vd_ffi)
  target_compile_definitions(vedit_workspace_scala PRIVATE VEDIT_SCALA_HAVE_FFI=1)
  message(STATUS "workspace-scala: linking the Rust service layer (vd_ffi)")
else()
  set(VEDIT_SCALA_FFI_HEADER
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../crates/vd-ffi/include/vd_ffi.h")
  set(VEDIT_SCALA_FFI_LIB
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../target/release/libvd_ffi.a")
  if(DEFINED VEDIT_RUST_FFI_INCLUDE AND VEDIT_RUST_FFI_INCLUDE)
    set(VEDIT_SCALA_FFI_HEADER "${VEDIT_RUST_FFI_INCLUDE}/vd_ffi.h")
  endif()
  if(DEFINED VEDIT_RUST_FFI_LIB AND VEDIT_RUST_FFI_LIB)
    set(VEDIT_SCALA_FFI_LIB "${VEDIT_RUST_FFI_LIB}")
  endif()
  if(EXISTS "${VEDIT_SCALA_FFI_HEADER}" AND EXISTS "${VEDIT_SCALA_FFI_LIB}")
    find_package(Threads REQUIRED)
    get_filename_component(VEDIT_SCALA_FFI_INCLUDE_DIR "${VEDIT_SCALA_FFI_HEADER}" DIRECTORY)
    target_include_directories(vedit_workspace_scala PUBLIC "${VEDIT_SCALA_FFI_INCLUDE_DIR}")
    target_link_libraries(vedit_workspace_scala PUBLIC
      "${VEDIT_SCALA_FFI_LIB}" Threads::Threads ${CMAKE_DL_LIBS} m)
    target_compile_definitions(vedit_workspace_scala PRIVATE VEDIT_SCALA_HAVE_FFI=1)
    message(STATUS "workspace-scala: linking the vd-ffi Scala run ABI (${VEDIT_SCALA_FFI_LIB})")
  else()
    message(STATUS
      "workspace-scala: no vd-ffi library found; the lane reports the missing service layer")
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_scala PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
