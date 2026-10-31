# Root-context integration check for the file tree lane (task T21).
#
# The repository's root CMakeLists.txt does not add src/workspace yet (the
# captain-approved workspace seam task adds it). This tiny project reproduces
# that context WITHOUT touching src/app/: it adds the real application shell and
# this lane exactly as the seam will, then runs `vedit-filetree-seamcheck`, which
# installs the panel through `installFileTree()` and asserts the shell's
# `file-tree` dock really holds a FileTreeWidget.
#
#   cmake -S src/workspace/filetree/tools/rootcheck -B target/filetree-rootcheck
#   cmake --build target/filetree-rootcheck
#   target/filetree-rootcheck/vedit-filetree-seamcheck --project <dir> --state <dir> \
#       --screenshot <png>
#
# It is evidence tooling for this lane, not a product build: the pins are
# enforced by the real root build / the pinned image (docs/BUILD.md §12.3).
cmake_minimum_required(VERSION 3.25)
project(VEditFileTreeRootCheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VEDIT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
get_filename_component(VEDIT_REPO_ROOT "${VEDIT_REPO_ROOT}" ABSOLUTE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)
# The application shell links KSyntaxHighlighting (theme catalogue).
find_package(KF6SyntaxHighlighting REQUIRED)

# src/app's CMakeLists already add_subdirectory()s every workspace lane that has
# a CMakeLists.txt (SEAM.md §3), so the lane is included there - adding it here
# as well would define the same target twice.
add_subdirectory("${VEDIT_REPO_ROOT}/src/app" app)

if(NOT TARGET vedit_workspace_integration)
  message(FATAL_ERROR
    "VEdit file tree rootcheck: vedit_workspace_integration was not created; "
    "src/workspace/integration.cpp or the lane wiring in src/app/CMakeLists.txt is missing")
endif()

add_executable(vedit-filetree-seamcheck seamcheck.cpp)
target_link_libraries(vedit-filetree-seamcheck PRIVATE
  vedit_workspace_integration
  vedit_app_shell
  vedit_file_tree
  Qt6::Test
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-filetree-seamcheck PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

message(STATUS "VEdit file tree rootcheck: Qt ${Qt6_VERSION}, shell=${VEDIT_REPO_ROOT}/src/app")
