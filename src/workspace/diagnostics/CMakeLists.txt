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
  diagnostics_controller.cpp
  diagnostics_outline_model.cpp
  ffi_backends.cpp
  toolchain.cpp
)
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)
  list(APPEND VEDIT_DIAGNOSTICS_SOURCES
    diagnostics_panel.cpp
    diagnostics_integration.cpp
  )
endif()

add_library(vedit_workspace_diagnostics STATIC ${VEDIT_DIAGNOSTICS_SOURCES})
target_include_directories(vedit_workspace_diagnostics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_diagnostics PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)
if(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)
  target_link_libraries(vedit_workspace_diagnostics PUBLIC vedit_app_shell)
  target_compile_definitions(vedit_workspace_diagnostics PRIVATE
    VEDIT_DIAGNOSTICS_HAVE_APP_SHELL=1)
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
add_test(NAME vedit-workspace-diagnostics-selftest
         COMMAND vedit-workspace-diagnostics-selftest --selfcheck)

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
