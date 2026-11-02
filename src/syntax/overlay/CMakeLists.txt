# VEdit syn-overlay lane (T18): the tree-sitter colouring overlay on top of the
# KSyntaxHighlighting base highlighter.
#
# Responsibilities (frozen contract .mpd/cellwork/.../contracts/ts-overlay.md):
#   A1g/C16  every range has ONE colouring owner (OwnerArbiter in the provider
#            lane); paint.h is the clipping plan the editor paints from;
#   A1h/C15  the runtime tree-sitter switch falls back to the base highlighter
#            and never crashes, at any point;
#   C12      the overlay injects a visible byte window (scrolling replaces the
#            claim set) and one edit re-injects only that window.
#
# Build: the ROOT build adds this directory (CMakeLists.txt lists
# src/syntax/overlay in VEDIT_LANE_DIRECTORIES when it exists). A standalone
# build is refused on purpose: the provider target brings the tree-sitter
# version/package discovery with it, and duplicating that here would let the two
# copies disagree.
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR
    "vedit_syntax_overlay is built by the ROOT build (it needs the vedit_syntax_provider target);\n"
    "run: cmake -S <repo> -B <build> -G Ninja && cmake --build <build> --target vedit-syntax-overlay-selftest")
endif()

add_library(vedit_syntax_overlay STATIC
  overlay.cpp
  paint.cpp
)
target_include_directories(vedit_syntax_overlay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_syntax_overlay PUBLIC vedit_syntax_provider)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_syntax_overlay PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-syntax-overlay-selftest overlay_selftest.cpp)
target_link_libraries(vedit-syntax-overlay-selftest PRIVATE vedit_syntax_overlay)
# The real grammar registration makes the validator exercise the real
# tree-sitter provider; without it (a product build that drops the grammars lane)
# the validator reports SKIP for the provider-dependent checks instead of lying.
if(TARGET vedit_syntax_grammars)
  target_link_libraries(vedit-syntax-overlay-selftest PRIVATE vedit_syntax_grammars)
  target_compile_definitions(vedit-syntax-overlay-selftest PRIVATE VEDIT_OVERLAY_HAVE_GRAMMARS=1)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-syntax-overlay-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vedit-syntax-overlay-selftest COMMAND vedit-syntax-overlay-selftest --selfcheck)
# The validator runs 50 rebuilds + 200 injections on a ~1k-line document and a
# toggle-stress loop; 120 s leaves room on a loaded machine without hiding a hang.
set_tests_properties(vedit-syntax-overlay-selftest PROPERTIES TIMEOUT 120)
