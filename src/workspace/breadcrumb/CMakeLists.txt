# Breadcrumb lane — Slice 2, task T19 (acceptance AC-S2.2: "面包屑随语义走").
#
# Owns the breadcrumb model (path of semantic containers around the cursor, with
# change detection) and the syntax-backed source that reads the path from the
# parser's structure query through `SyntaxProvider::breadcrumbs()`. The lane has
# NO dependency on src/syntax/folding: updating the breadcrumbs can never rebuild
# folds (AMENDMENTS A2.1).
#
#   standalone:  cmake -S src/workspace/breadcrumb -B target/ws-breadcrumb -G Ninja
#                cmake --build target/ws-breadcrumb && ctest --test-dir target/ws-breadcrumb
#
# Not linked into vedit-app yet: the live cursor/view wiring belongs to the
# editor lane, outside this task's write scope (frozen contract folding-breadcrumbs).
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vedit_workspace_breadcrumb LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_C_STANDARD 11)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)

if(NOT TARGET vedit_syntax_grammars)
  set(VEDIT_GRAMMAR_BUILD_TESTS OFF CACHE BOOL "grammar lane tests (off in this lane build)" FORCE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../syntax/grammars"
                   "${CMAKE_CURRENT_BINARY_DIR}/syn-grammars")
endif()

add_library(vedit_workspace_breadcrumb STATIC
  breadcrumb_model.cpp
  syntax_breadcrumb_source.cpp
)
target_include_directories(vedit_workspace_breadcrumb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_breadcrumb PUBLIC Qt6::Core vedit_syntax_grammars)
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
