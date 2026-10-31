# Product wiring of the syntax stack — Slice 1, task T12 (G2 + G3).
#
# What this lane owns: the ONE place where the syntax stack becomes part of the
# product binary. It adds nothing to the highlighting implementation; it links
# the three syntax libraries + libtree-sitter + KSyntaxHighlighting and proves the
# editing kernel (KF6TextEditor) is present, then registers the stack with Qt's
# application startup hook so no src/app change is required.
#
# The grammar id (G3) is owned by src/syntax/grammars and read by the provider
# lane at the END of the top-level configure (see src/syntax/provider/CMakeLists).
# This file therefore must not be the place that decides it.
#
# Standalone (lane evidence):
#   cmake -S src/syntax/product -B target/syn-product -G Ninja && ctest
# Root build: pulled in by src/syntax/grammars/CMakeLists.txt (the root lane list
# does not name this directory), and its deferred function links vedit-app.
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vedit_syntax_product LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_C_STANDARD 11)
  enable_testing()
endif()

# G2: these packages are product dependencies, so a missing one fails configure
# instead of producing a binary that silently has no syntax stack.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)
find_package(KF6TextEditor REQUIRED)

# The stack itself: the grammar registry (which owns the provider + the vendored
# tree-sitter-verilog grammar) and the syntax-data loader.
if(NOT TARGET vedit_syntax_grammars)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../grammars" "${CMAKE_CURRENT_BINARY_DIR}/grammars")
endif()
if(NOT TARGET vedit_syntax_kde)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../kde" "${CMAKE_CURRENT_BINARY_DIR}/kde")
endif()

add_library(vedit_syntax_product STATIC syntax_product.cpp)
target_include_directories(vedit_syntax_product PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# The object under test references all of these on purpose: the grammar registry
# (tree-sitter), the data loader (KSyntaxHighlighting) and the editing kernel.
target_link_libraries(vedit_syntax_product PUBLIC
  Qt6::Core
  Qt6::Gui
  vedit_syntax_grammars
  vedit_syntax_kde
  KF6::TextEditor
  KF6::SyntaxHighlighting
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_syntax_product PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(VEDIT_SYNTAX_PRODUCT_BUILD_TESTS "Build the product-wiring acceptance test" ON)
if(VEDIT_SYNTAX_PRODUCT_BUILD_TESTS)
  add_executable(vedit_syntax_product_tests tests/test_syntax_product.cpp)
  target_link_libraries(vedit_syntax_product_tests PRIVATE vedit_syntax_product)
  # The repository's own LGPL-2.1+ definitions: exactly what packaging ships.
  target_compile_definitions(vedit_syntax_product_tests PRIVATE
    VEDIT_SYNTAX_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../resources")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit_syntax_product_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-syntax-product-tests COMMAND vedit_syntax_product_tests)
  set_tests_properties(vedit-syntax-product-tests PROPERTIES
    ENVIRONMENT
      "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime;VEDIT_SYNTAX_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../../resources"
    TIMEOUT 120
  )
endif()

# --- G2: make the syntax stack part of the product binary -------------------
# Deferred to the end of the top-level configure because the root build adds
# src/app (and therefore the vedit-app target) AFTER the lane directories.
#
# WHOLE_ARCHIVE is required: the startup registration object is not referenced by
# any other object, so a normal static-library link would drop it and the product
# would silently not initialize the syntax stack.
function(vedit_syntax_link_product)
  if(NOT TARGET vedit-app)
    message(STATUS
      "syn-product: this build has no vedit-app target (lane-only build); the product "
      "link is skipped, the stack itself is still built and tested")
    return()
  endif()
  foreach(required_target
      vedit_syntax_product vedit_syntax_grammars vedit_syntax_kde vedit_syntax_provider
      KF6::TextEditor KF6::SyntaxHighlighting)
    if(NOT TARGET ${required_target})
      message(FATAL_ERROR
        "syn-product: required target '${required_target}' is missing. The product must not "
        "link a partial syntax stack (G2) - fix the lane or the package, never skip the link.")
    endif()
  endforeach()
  target_link_libraries(vedit-app PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,vedit_syntax_product>"
    KF6::TextEditor)
  # The product binary itself must log the initialised stack: this ctest asserts
  # the startup hook ran inside vedit-app, not just inside a unit test.
  add_test(NAME vedit-app-syntax-startup COMMAND vedit-app --selfcheck)
  set_tests_properties(vedit-app-syntax-startup PROPERTIES
    ENVIRONMENT
      "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime;VEDIT_SYNTAX_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../../resources"
    PASS_REGULAR_EXPRESSION "syntax-product: grammar=verilog"
    TIMEOUT 120
  )
  message(STATUS
    "syn-product: vedit-app links the syntax stack (provider+kde+grammars, tree-sitter, "
    "KTextEditor, KSyntaxHighlighting) and its startup hook is asserted by ctest")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL vedit_syntax_link_product)
