# Editor-side FIM lane — Slice 3, task T27 (AC-S3.2 remainder).
#
# Inline-completion client for the optional FIM service: the Qt side owns only
# the prefix/suffix split and the suggestion state machine; the HTTP request is
# made by the Rust service layer through the C ABI (vd_fim_complete/poll). With
# the product default (disabled / unconfigured) the Rust side opens no socket.
#
# The lane is intentionally NOT wired into src/editor/kernel yet; README.md names
# the minimal kernel hook and the wiring is a separate task (the kernel and
# src/app are owned by other tasks).
#
#   standalone with the Rust service:
#     cmake -S src/editor/fim -B target/fim-lane -G Ninja \
#       -DVEDIT_FFI_STATICLIB=$PWD/target/release/libvd_ffi.a \
#       -DVEDIT_FFI_INCLUDE_DIR=$PWD/crates/vd-ffi/include
#     cmake --build target/fim-lane && ctest --test-dir target/fim-lane
#
# Without the two FFI variables the lane still builds and its selftest prints an
# explicit SKIP for the Rust-chain checks (never a fake PASS).
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VEditFim LANGUAGES CXX)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VEDIT_FFI_STATICLIB "" CACHE FILEPATH
  "libvd_ffi.a (release) for the in-process Rust service; empty = no Rust chain")
set(VEDIT_FFI_INCLUDE_DIR "" CACHE PATH
  "directory holding the cbindgen vd_ffi.h")

add_library(vedit_editor_fim STATIC
  fim_backend.cpp
  fim_inline_completion.cpp
)
target_include_directories(vedit_editor_fim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_editor_fim PUBLIC Qt6::Core Threads::Threads)

set(VEDIT_FIM_FFI_AVAILABLE OFF)
if(TARGET vd_ffi)
  target_link_libraries(vedit_editor_fim PUBLIC vd_ffi)
  set(VEDIT_FIM_FFI_AVAILABLE ON)
elseif(VEDIT_FFI_STATICLIB AND EXISTS "${VEDIT_FFI_STATICLIB}"
       AND VEDIT_FFI_INCLUDE_DIR AND EXISTS "${VEDIT_FFI_INCLUDE_DIR}/vd_ffi.h")
  add_library(vedit_fim_ffi STATIC IMPORTED)
  set_target_properties(vedit_fim_ffi PROPERTIES
    IMPORTED_LOCATION "${VEDIT_FFI_STATICLIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${VEDIT_FFI_INCLUDE_DIR}")
  target_link_libraries(vedit_fim_ffi INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
  target_link_libraries(vedit_editor_fim PUBLIC vedit_fim_ffi)
  set(VEDIT_FIM_FFI_AVAILABLE ON)
endif()
if(VEDIT_FIM_FFI_AVAILABLE)
  target_compile_definitions(vedit_editor_fim PUBLIC VEDIT_FIM_HAVE_FFI=1)
  message(STATUS "VEdit FIM: Rust service layer linked (the Qt side never sends HTTP)")
else()
  message(STATUS "VEdit FIM: no Rust service layer; the guard is inactive, the selftest SKIPs the chain")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_editor_fim PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-fim-selftest fim_selftest.cpp)
target_link_libraries(vedit-fim-selftest PRIVATE vedit_editor_fim)
if(VEDIT_FIM_FFI_AVAILABLE)
  target_compile_definitions(vedit-fim-selftest PRIVATE VEDIT_FIM_SELFTEST_HAVE_FFI=1)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-fim-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vedit-fim-selftest COMMAND vedit-fim-selftest)
set_tests_properties(vedit-fim-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-fim"
  TIMEOUT 120
)
