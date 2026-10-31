# Workspace palette lane (ws-palette, task T22 / acceptance A2b).
#
# Two ways in:
#
#   * root build: src/app/CMakeLists.txt includes this directory through its
#     `VEDIT_WORKSPACE_LANES` loop once the CMakeLists.txt exists, links
#     `vedit_workspace_palette` into vedit-app and the app-facing adapter
#     (palette_integration.cpp) is compiled because `vedit_app_shell` exists;
#   * standalone: `cmake -S src/workspace/palette -B <dir>` configures the core
#     library + selftest + benchmark only, which is how the lane evidence is
#     produced inside the pinned Qt6 image (scripts/build-qt.sh context).
#
# Warnings are errors, matching the M0 build base policy (docs/BUILD.md §12.5).
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VEditWorkspacePalette LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  find_package(Qt6 6.10.2 EXACT REQUIRED COMPONENTS Core Gui Widgets)
endif()

set(CMAKE_AUTOMOC ON)

set(VEDIT_PALETTE_WARNING_FLAGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(VEDIT_PALETTE_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(vedit_workspace_palette STATIC
  command_palette.cpp
  file_index.cpp
  fuzzy_matcher.cpp
)
target_include_directories(vedit_workspace_palette PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_palette PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_options(vedit_workspace_palette PRIVATE ${VEDIT_PALETTE_WARNING_FLAGS})

add_executable(vedit-workspace-palette-selftest palette_selftest.cpp)
target_link_libraries(vedit-workspace-palette-selftest PRIVATE vedit_workspace_palette)
target_compile_options(vedit-workspace-palette-selftest PRIVATE ${VEDIT_PALETTE_WARNING_FLAGS})

add_executable(vedit-workspace-palette-bench palette_bench.cpp)
target_link_libraries(vedit-workspace-palette-bench PRIVATE vedit_workspace_palette)
target_compile_options(vedit-workspace-palette-bench PRIVATE ${VEDIT_PALETTE_WARNING_FLAGS})

add_test(NAME vedit-workspace-palette-selftest
         COMMAND vedit-workspace-palette-selftest
                 --screenshots "${CMAKE_CURRENT_BINARY_DIR}/screenshots")
set_tests_properties(vedit-workspace-palette-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 120
)

# App-facing adapter: only meaningful when the shell library is in this build.
if(TARGET vedit_app_shell)
  target_sources(vedit_workspace_palette PRIVATE palette_integration.cpp)
  target_link_libraries(vedit_workspace_palette PUBLIC vedit_app_shell)

  add_executable(vedit-workspace-palette-integration-selftest
                 palette_integration_selftest.cpp)
  target_link_libraries(vedit-workspace-palette-integration-selftest
                        PRIVATE vedit_workspace_palette)
  target_compile_options(vedit-workspace-palette-integration-selftest
                         PRIVATE ${VEDIT_PALETTE_WARNING_FLAGS})

  add_test(NAME vedit-workspace-palette-integration-selftest
           COMMAND vedit-workspace-palette-integration-selftest
                   --screenshots "${CMAKE_CURRENT_BINARY_DIR}/integration-screenshots")
  set_tests_properties(vedit-workspace-palette-integration-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
    TIMEOUT 120
  )
endif()
