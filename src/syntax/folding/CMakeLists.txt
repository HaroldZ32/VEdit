# Folding lane — Slice 2, task T19 (acceptance AC-S2.2 / AMENDMENTS A1-A3).
#
# Owns the application-side fold injector over KTextEditor's public command
# interface (`fold` / `unfold`), the A1 range self-validation, the A2 rebuild
# policy/counters and the A3 observation helper. The ranges come from the syntax
# stack's `SyntaxProvider::foldRanges()` (tree-sitter, resources/queries/*/folds.scm),
# so this lane links the grammar lane rather than parsing anything itself.
#
#   standalone:  cmake -S src/syntax/folding -B target/syn-folding -G Ninja
#                cmake --build target/syn-folding && ctest --test-dir target/syn-folding
#
# The lane is NOT linked into vedit-app yet: wiring it to the live editor view
# belongs to the editor lane (`src/editor`), which is outside this task's write
# scope (frozen contract folding-breadcrumbs, allowed scopes). The header
# fold_injector.h documents the seam (caller owns the KTextEditor::View).
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vedit_syntax_folding LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_C_STANDARD 11)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6TextEditor REQUIRED)

# The syntax stack owns the fold ranges (provider + vendored grammar + queries).
# Standalone builds pull the grammar lane in; the root build already has it.
if(NOT TARGET vedit_syntax_grammars)
  set(VEDIT_GRAMMAR_BUILD_TESTS OFF CACHE BOOL "grammar lane tests (off in this lane build)" FORCE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../grammars"
                   "${CMAKE_CURRENT_BINARY_DIR}/syn-grammars")
endif()

add_library(vedit_syntax_folding STATIC
  fold_range.cpp
  fold_injector.cpp
  fold_source.cpp
  syntax_fold_source.cpp
  ksyntax_fold_source.cpp
)
target_include_directories(vedit_syntax_folding PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_syntax_folding PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  KF6::TextEditor
  vedit_syntax_grammars
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_syntax_folding PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-folding-tests tests/folding_tests.cpp)
target_link_libraries(vedit-folding-tests PRIVATE vedit_syntax_folding Qt6::Gui)
# T29: the closed-state source reads OUR definition file, never the repository.
target_compile_definitions(vedit-folding-tests PRIVATE
  VEDIT_A4_SYSTEMVERILOG_XML="${CMAKE_CURRENT_SOURCE_DIR}/../../../resources/syntax/vedit-systemverilog.xml")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-folding-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vedit-folding-tests COMMAND vedit-folding-tests)
set_tests_properties(vedit-folding-tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-fold"
  TIMEOUT 300
)
