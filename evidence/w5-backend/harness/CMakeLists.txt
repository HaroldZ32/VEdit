# T11 evidence harness (backend switch + capability gating + format + engines).
#
# It compiles the SAME product sources (FFI backends, toolchain, controller) and
# drives the real verible-verilog-ls / slang-server / verible-verilog-format /
# verible-verilog-lint binaries resolved from the checkout. Nothing here is
# product code; `root-grab` captures the product window from an Xvfb display.
cmake_minimum_required(VERSION 3.25)
project(vedit_w5_backend_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(VEDIT_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." CACHE PATH "workspace root")
get_filename_component(VEDIT_SOURCE_ROOT "${VEDIT_SOURCE_ROOT}" ABSOLUTE)
set(VEDIT_RUST_FFI_LIB "${VEDIT_SOURCE_ROOT}/target/release/libvd_ffi.a" CACHE FILEPATH "libvd_ffi.a")
set(VEDIT_RUST_FFI_INCLUDE "${VEDIT_SOURCE_ROOT}/crates/vd-ffi/include" CACHE PATH "vd_ffi.h dir")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Threads REQUIRED)

add_library(w5_core STATIC
  "${VEDIT_SOURCE_ROOT}/src/workspace/diagnostics/ffi_backends.cpp"
  "${VEDIT_SOURCE_ROOT}/src/workspace/diagnostics/toolchain.cpp"
  "${VEDIT_SOURCE_ROOT}/src/workspace/diagnostics/diagnostics_controller.cpp"
)
target_compile_definitions(w5_core PUBLIC VEDIT_DIAGNOSTICS_HAVE_FFI=1)
target_include_directories(w5_core PUBLIC
  "${VEDIT_SOURCE_ROOT}/src/workspace/diagnostics"
  "${VEDIT_SOURCE_ROOT}/src/workspace"
  "${VEDIT_RUST_FFI_INCLUDE}"
)
target_link_libraries(w5_core PUBLIC Qt6::Core "${VEDIT_RUST_FFI_LIB}" Threads::Threads ${CMAKE_DL_LIBS} m)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(w5_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(w5-probe w5_probe.cpp)
target_link_libraries(w5-probe PRIVATE w5_core Qt6::Widgets)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(w5-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Grabs the root window of an Xvfb display (no xwd/import in the pinned image).
add_executable(root-grab root_grab.cpp)
target_link_libraries(root-grab PRIVATE Qt6::Core Qt6::Gui)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(root-grab PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
