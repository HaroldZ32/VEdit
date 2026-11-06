# Jump lane - the application-wide jump contract (acceptance AC-7.7).
#
# ONE interface (`IJumpTarget`) and ONE process-wide registry, consumed by every
# panel that can send the editor to file:line:column. The lane has no widget and
# no file-opening code: the shell/editor lane registers the real target.
#
# Two ways to build it, like every lane in this repository:
#   standalone  cmake -S src/workspace/jump -B target/ws-jump -G Ninja
#               cmake --build target/ws-jump && ctest --test-dir target/ws-jump
#   product     src/app/CMakeLists.txt lists `jump` in VEDIT_WORKSPACE_LANES and
#               src/workspace/integration.cpp calls installJump(); the lane is
#               also configured early when the breadcrumb lane (included by the
#               editor lane) needs it first.
#
# The GLOBAL guard below mirrors the other lanes: a second add_subdirectory()
# of this source directory is a no-op, so the early inclusion from
# src/workspace/breadcrumb and the later lane-list inclusion cannot fight.
cmake_minimum_required(VERSION 3.25)

get_property(VEDIT_WORKSPACE_JUMP_ADDED GLOBAL PROPERTY VEDIT_WORKSPACE_JUMP_ADDED)
if(VEDIT_WORKSPACE_JUMP_ADDED)
  return()
endif()
set_property(GLOBAL PROPERTY VEDIT_WORKSPACE_JUMP_ADDED TRUE)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VEditWorkspaceJump LANGUAGES CXX)
  enable_testing()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(vedit_workspace_jump STATIC
  jump_registry.cpp
  jump_integration.cpp
)
# Consumers include `jump/jump_registry.h` (the same spelling integration.cpp
# uses), so the workspace directory is the public include root.
target_include_directories(vedit_workspace_jump PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_workspace_jump PUBLIC Qt6::Core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_jump PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-jump-tests tests/jump_tests.cpp)
target_link_libraries(vedit-jump-tests PRIVATE vedit_workspace_jump)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-jump-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vedit-jump-tests COMMAND vedit-jump-tests)
set_tests_properties(vedit-jump-tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-jump"
  TIMEOUT 120
)
