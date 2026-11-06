# Breadcrumb lane — Slice 2, task T19 (acceptance AC-S2.2: "面包屑随语义走").
#
# Owns the breadcrumb model (path of semantic containers around the cursor, with
# change detection), the syntax-backed source that reads the path from the
# parser's structure query through `SyntaxProvider::breadcrumbs()`, and the
# clickable bar (`breadcrumb_bar.*`, AC-7.6) that forwards a segment click to the
# application-wide jump contract (src/workspace/jump/). The lane has NO
# dependency on the deleted folding lane (T29): updating the breadcrumbs can never rebuild
# folds (AMENDMENTS A2.1).
#
#   standalone:  cmake -S src/workspace/breadcrumb -B target/ws-breadcrumb -G Ninja
#                cmake --build target/ws-breadcrumb && ctest --test-dir target/ws-breadcrumb
#
# The editor lane includes this directory in the product build and consumes the
# model/source through `FoldBreadcrumbBinding`; the clickable bar is the widget a
# pane uses in place of its text label.
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vedit_workspace_breadcrumb LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_AUTOMOC ON)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

if(NOT TARGET vedit_syntax_grammars)
  set(VEDIT_GRAMMAR_BUILD_TESTS OFF CACHE BOOL "grammar lane tests (off in this lane build)" FORCE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../syntax/grammars"
                   "${CMAKE_CURRENT_BINARY_DIR}/syn-grammars")
endif()

# The clickable bar forwards a segment click through the process-wide jump
# contract (src/workspace/jump/jump_registry.h) - the ONE jump seam of the
# application (AC-7.7), never its own file-opening code. The editor lane includes
# this lane before src/app's workspace-lane loop, so the jump lane is configured
# here when it is not a target yet; the jump CMakeLists' GLOBAL guard makes the
# later lane-list inclusion a no-op.
if(NOT TARGET vedit_workspace_jump AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../jump/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../jump" "${CMAKE_CURRENT_BINARY_DIR}/jump-lane")
endif()
if(NOT TARGET vedit_workspace_jump)
  message(FATAL_ERROR
    "vedit_workspace_breadcrumb needs the jump lane (src/workspace/jump): its clickable bar "
    "has no second jump implementation")
endif()

add_library(vedit_workspace_breadcrumb STATIC
  breadcrumb_model.cpp
  breadcrumb_bar.cpp
  syntax_breadcrumb_source.cpp
)
target_include_directories(vedit_workspace_breadcrumb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_breadcrumb PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  vedit_syntax_grammars
  vedit_workspace_jump
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_breadcrumb PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-breadcrumb-tests tests/breadcrumb_tests.cpp)
target_link_libraries(vedit-breadcrumb-tests PRIVATE vedit_workspace_breadcrumb)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-breadcrumb-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vedit-breadcrumb-tests COMMAND vedit-breadcrumb-tests)
set_tests_properties(vedit-breadcrumb-tests PROPERTIES TIMEOUT 120)

# AC-7.6: the segments are clickable and the click reaches the jump contract with
# the container's file:line. Real QTest mouse clicks over the real structure query.
add_executable(vedit-breadcrumb-click-tests tests/breadcrumb_click_tests.cpp)
target_link_libraries(vedit-breadcrumb-click-tests PRIVATE
  vedit_workspace_breadcrumb
  Qt6::Test
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-breadcrumb-click-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vedit-breadcrumb-click-tests COMMAND vedit-breadcrumb-click-tests)
set_tests_properties(vedit-breadcrumb-click-tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-breadcrumb"
  TIMEOUT 120
)
