# VEdit private syntax-data loader + data validator (syn-kde-data lane, T16).
#
#   standalone:  cmake -S src/syntax/kde -B target/syn-kde -G Ninja && ctest
#   root build (once wired like src/app): add_subdirectory(src/syntax/kde)
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vedit_syntax_kde LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(KF6SyntaxHighlighting REQUIRED)

# T110: the shared VEDIT_SYNTAX_DIR normalisation (neutral layer below this one).
if(NOT TARGET vedit_syntax_common)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../common"
                   "${CMAKE_CURRENT_BINARY_DIR}/syn-common")
endif()

add_library(vedit_syntax_kde STATIC ksyntax_data.cpp)
target_include_directories(vedit_syntax_kde PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_syntax_kde PUBLIC Qt6::Core KF6::SyntaxHighlighting vedit_syntax_common)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_syntax_kde PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(VEDIT_SYNTAX_KDE_BUILD_TESTS "Build the syntax-data validator" ON)
if(VEDIT_SYNTAX_KDE_BUILD_TESTS)
  add_executable(vedit_syntax_kde_selftest tests/validate_syntax_data.cpp)
  target_link_libraries(vedit_syntax_kde_selftest PRIVATE vedit_syntax_kde)
  # resources/ holds the "syntax/" data directory (repo layout mirrors the AppDir).
  target_compile_definitions(vedit_syntax_kde_selftest PRIVATE
    VEDIT_SYNTAX_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../resources")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit_syntax_kde_selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-syntax-kde-data COMMAND vedit_syntax_kde_selftest)
  set_tests_properties(vedit-syntax-kde-data PROPERTIES TIMEOUT 120)

  # T110: equivalence check — the loader now calls the shared resolver, so its
  # effective search path must equal the common function's answer for BOTH the
  # data-root and the legacy `syntax/` spelling.
  add_executable(vedit-syntax-directory-equivalence tests/syntax_directory_equivalence.cpp)
  target_link_libraries(vedit-syntax-directory-equivalence PRIVATE vedit_syntax_kde)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-syntax-directory-equivalence PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-syntax-directory-equivalence COMMAND vedit-syntax-directory-equivalence)
  set_tests_properties(vedit-syntax-directory-equivalence PROPERTIES TIMEOUT 60)
endif()
