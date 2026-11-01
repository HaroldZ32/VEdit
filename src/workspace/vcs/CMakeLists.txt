# VCS panel lane (T23): read-only Git/SVN view + the stub seam the tests drive.
#
# Standalone build (this directory) or as a lane of the product build. The FFI
# link is optional: without crates/vd-ffi's static library the lane still builds
# and the selftest marks the C ABI checks SKIP instead of pretending they ran.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(VEditWorkspaceVcs LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
  enable_testing()
endif()

set(CMAKE_AUTOMOC ON)

set(VEDIT_WORKSPACE_VCS_SOURCES
  vcs_source.cpp
  vcs_panel.cpp
)
if(TARGET vedit_app_shell)
  # Only the product build has the shell; the standalone lane keeps the core.
  list(APPEND VEDIT_WORKSPACE_VCS_SOURCES vcs_integration.cpp)
endif()

add_library(vedit_workspace_vcs STATIC ${VEDIT_WORKSPACE_VCS_SOURCES})
target_include_directories(vedit_workspace_vcs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_vcs PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
if(TARGET vedit_app_shell)
  target_link_libraries(vedit_workspace_vcs PUBLIC vedit_app_shell)
  target_compile_definitions(vedit_workspace_vcs PRIVATE VEDIT_VCS_HAVE_SHELL=1)
endif()

# Optional C ABI: the panel's production source calls vd_vcs_info/vd_vcs_diff.
# Resolution order: the root build's cache variables, then the checkout defaults.
set(VEDIT_VCS_FFI_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../../../crates/vd-ffi/include/vd_ffi.h")
set(VEDIT_VCS_FFI_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../../target/release/libvd_ffi.a")
if(DEFINED VEDIT_RUST_FFI_INCLUDE AND VEDIT_RUST_FFI_INCLUDE)
  set(VEDIT_VCS_FFI_HEADER "${VEDIT_RUST_FFI_INCLUDE}/vd_ffi.h")
endif()
if(DEFINED VEDIT_RUST_FFI_LIB AND VEDIT_RUST_FFI_LIB)
  set(VEDIT_VCS_FFI_LIB "${VEDIT_RUST_FFI_LIB}")
endif()
if(EXISTS "${VEDIT_VCS_FFI_HEADER}" AND EXISTS "${VEDIT_VCS_FFI_LIB}")
  find_package(Threads REQUIRED)
  get_filename_component(VEDIT_VCS_FFI_INCLUDE_DIR "${VEDIT_VCS_FFI_HEADER}" DIRECTORY)
  target_compile_definitions(vedit_workspace_vcs PUBLIC VEDIT_VCS_HAVE_FFI=1)
  target_include_directories(vedit_workspace_vcs PUBLIC "${VEDIT_VCS_FFI_INCLUDE_DIR}")
  target_link_libraries(vedit_workspace_vcs PUBLIC "${VEDIT_VCS_FFI_LIB}" Threads::Threads ${CMAKE_DL_LIBS} m)
  message(STATUS "workspace-vcs: linking the vd-ffi VCS ABI (${VEDIT_VCS_FFI_LIB})")
else()
  message(STATUS "workspace-vcs: no vd-ffi library found; building stub-only (FFI checks SKIP)")
endif()

add_executable(vedit-workspace-vcs-selftest vcs_selftest.cpp)
target_link_libraries(vedit-workspace-vcs-selftest PRIVATE vedit_workspace_vcs)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-workspace-vcs-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(vedit_workspace_vcs PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-workspace-vcs-selftest COMMAND vedit-workspace-vcs-selftest --selfcheck)
set_tests_properties(vedit-workspace-vcs-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-vcs"
  TIMEOUT 120
)
