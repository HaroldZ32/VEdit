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
# The edit-core lane (src/editor) links KTextEditor; the real root build finds it
# in the root CMakeLists.txt, so this mini-project has to ask for it itself.
find_package(KF6TextEditor REQUIRED)

# The workspace lanes wired by src/app include `project`, which requires the
# Rust service layer (vd_ffi) exactly like the root build does. Import the same
# module before src/app; the artifacts are produced by `bash scripts/build-qt.sh`
# (or an explicit -DVEDIT_RUST_FFI_LIB / -DVEDIT_RUST_FFI_INCLUDE).
if(NOT TARGET vd_ffi)
  if(NOT VEDIT_RUST_FFI_LIB)
    set(VEDIT_RUST_FFI_LIB "${VEDIT_REPO_ROOT}/target/release/libvd_ffi.a"
        CACHE FILEPATH "libvd_ffi.a produced by scripts/build-qt.sh")
  endif()
  if(NOT VEDIT_RUST_FFI_INCLUDE)
    set(VEDIT_RUST_FFI_INCLUDE "${VEDIT_REPO_ROOT}/crates/vd-ffi/include"
        CACHE PATH "Directory containing the cbindgen-generated vd_ffi.h")
  endif()
  include("${VEDIT_REPO_ROOT}/cmake/VEditRustFfi.cmake")
endif()

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
# Regression T60: the PRODUCT installs the KTextEditor provider, so the seam
# check must run the double-click assertions against it, not only against the
# demo QPlainTextEdit. The lane itself stays provider-agnostic (it only calls
# EditorPane::openFile); the harness is what pins the real provider.
if(TARGET vedit_editor_core)
  target_link_libraries(vedit-filetree-seamcheck PRIVATE vedit_editor_core)
  target_compile_definitions(vedit-filetree-seamcheck PRIVATE VEDIT_ROOTCHECK_HAVE_EDIT_CORE=1)
  message(STATUS "VEdit file tree rootcheck: edit-core lane present -> KTextEditor provider assertions enabled")
else()
  message(STATUS "VEdit file tree rootcheck: no edit-core lane -> demo provider assertions only")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-filetree-seamcheck PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

message(STATUS "VEdit file tree rootcheck: Qt ${Qt6_VERSION}, shell=${VEDIT_REPO_ROOT}/src/app")
