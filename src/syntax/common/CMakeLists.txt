# Neutral shared utilities of the syntax stack (T110).
#
# `syntax_directory` is the ONE normalisation of `VEDIT_SYNTAX_DIR` (T84 data root
# vs. the legacy `syntax/` subdirectory spelling). It lives here, below both the
# KDE data loader (`src/syntax/kde`) — and, until T29 deleted it, the folding lane,
# so neither has to depend on the other and the three call sites cannot drift
# apart again (the T109 root cause).
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vedit_syntax_common LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(vedit_syntax_common STATIC syntax_directory.cpp)
target_include_directories(vedit_syntax_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_syntax_common PUBLIC Qt6::Core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_syntax_common PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
