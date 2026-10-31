# VEdit — root build description for the Qt6 / KF6 / KTextEditor C++ product.
#
# Scope of this file (M0 build base, task T10): the CMake skeleton that every
# later lane (app-shell, editor core, syntax, workspace, project, tools, VCS,
# settings) plugs into. The business code itself belongs to those lanes; the
# only target defined here is the M0 smoke target under cmake/smoke/, which
# proves the pinned toolchain can link and run Qt6 Widgets + KTextEditor +
# KSyntaxHighlighting + tree-sitter.
#
# Every dependency version is pinned in scripts/versions-qt.env and requested
# EXACT via cmake/VEditVersions.cmake; the toolchain itself is the pinned image
# `scripts/docker/Dockerfile.build` stage `qt6-build` (Ubuntu 26.04.1 LTS digest
# fixed). Build entry point: `bash scripts/build-qt.sh`.
cmake_minimum_required(VERSION 3.25)
# C is enabled because a lane library can legitimately contain generated C
# (e.g. src/syntax/provider's test fixture grammar parser.c). A CXX-only root
# project cannot link such a target: CMake fails with "cannot determine linker
# language for target". T63 found this when the provider lane was first wired in.
project(VEdit VERSION 0.2.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VEditVersions)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

# --- Dependencies (exact pins; see the messages VEditVersions prints) --------
# Qml/TextToSpeech/PrintSupport are not used directly here, but KTextEditor's
# imported targets reference them, so they must be requested by the consumer.
find_package(Qt6 ${VEDIT_QT6_VERSION} EXACT REQUIRED
  COMPONENTS Core Gui Widgets Network PrintSupport Qml TextToSpeech)
find_package(KF6CoreAddons ${VEDIT_KF6_VERSION} EXACT REQUIRED)
find_package(KF6Config ${VEDIT_KF6_VERSION} EXACT REQUIRED)
find_package(KF6SyntaxHighlighting ${VEDIT_KSYNTAXHIGHLIGHTING_VERSION} EXACT REQUIRED)
find_package(KF6TextEditor ${VEDIT_KTEXTEDITOR_VERSION} EXACT REQUIRED)

# tree-sitter ships no CMake package, so it is pinned through pkg-config with
# the exact runtime version (C18: the runtime is part of the private bundle).
find_package(PkgConfig REQUIRED)
pkg_check_modules(TREE_SITTER REQUIRED IMPORTED_TARGET "tree-sitter=${VEDIT_TREE_SITTER_VERSION}")
message(STATUS "  tree-sitter:       ${TREE_SITTER_VERSION} at ${TREE_SITTER_LIBDIR}")

# --- M0 build-base smoke target ---------------------------------------------
option(VEDIT_BUILD_M0_SMOKE
  "Build the M0 Qt6/KTextEditor/KSyntaxHighlighting/tree-sitter smoke target (T10)" ON)
if(VEDIT_BUILD_M0_SMOKE)
  add_subdirectory(cmake/smoke)
endif()

# --- Lane directories outside src/app (T63) ---------------------------------
# A lane library that exists on disk but is never add_subdirectory()'d is the
# worst kind of false green: `bash scripts/build-qt.sh` passes without compiling
# a single line of it. Every top-level lane directory is therefore listed HERE
# (explicit list, NOT file(GLOB): the build must not depend on whatever happens
# to be on disk). src/app/CMakeLists.txt keeps owning the shell's own
# subdirectories (src/workspace/* is wired there), so those are not repeated.
#
# When a lane lands, add its directory to this list in the same PR that lands
# its CMakeLists.txt; the if(EXISTS) guard keeps the build green meanwhile.
set(VEDIT_LANE_DIRECTORIES
  src/syntax/provider   # syn-provider lane (T15)
  src/syntax/kde        # syn-kde-data lane (T16), added once it lands
  src/syntax/grammars   # syn-grammars lane (T17)
  src/terminal          # term-panel lane (T35)
  src/syntax/overlay    # syn-overlay lane (T18), added once it lands
)
foreach(lane_dir IN LISTS VEDIT_LANE_DIRECTORIES)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${lane_dir}/CMakeLists.txt")
    add_subdirectory("${lane_dir}")
    message(STATUS "VEdit: building lane directory ${lane_dir}")
  else()
    message(STATUS "VEdit: lane directory ${lane_dir} not present yet; skipped")
  endif()
endforeach()

# --- Application shell (owned by the app-shell lane, T13) -------------------
# The build base must not contain src/app, but must pick it up automatically
# once that lane lands, so the app lane never has to edit this file.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/CMakeLists.txt")
  add_subdirectory(src/app)
  message(STATUS "VEdit: building src/app (application shell)")
else()
  message(STATUS "VEdit: src/app absent (app-shell lane T13 not landed yet); building only the M0 smoke target")
endif()
