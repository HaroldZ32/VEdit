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
project(VEdit VERSION 0.2.1 LANGUAGES C CXX)

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
# T18 i18n: Qt6 LinguistTools turns translations/*.ts into the shipped .qm.
# Optional on purpose: a host without the tools still configures and builds; the
# pinned image ships them (verified: /usr/lib/qt6/bin/lrelease, 6.10.2).
find_package(Qt6 ${VEDIT_QT6_VERSION} QUIET COMPONENTS LinguistTools)
if(NOT TARGET Qt6::lrelease)
  message(STATUS "  Qt6 LinguistTools: absent (no .qm generated; UI stays English)")
endif()

# tree-sitter ships no CMake package, so it is pinned through pkg-config with
# the exact runtime version (C18: the runtime is part of the private bundle).
find_package(PkgConfig REQUIRED)
pkg_check_modules(TREE_SITTER REQUIRED IMPORTED_TARGET "tree-sitter=${VEDIT_TREE_SITTER_VERSION}")
message(STATUS "  tree-sitter:       ${TREE_SITTER_VERSION} at ${TREE_SITTER_LIBDIR}")

# --- Rust service layer (vd-ffi, Slice 0 / task T4) -------------------------
# Imported target + the hard requirement that the product binary carries the
# C ABI. The library is built by scripts/build-qt.sh before configure.
include(VEditRustFfi)

# --- A-11: Qt/ABI compile-only guard (T20) ----------------------------------
# `qobjectdefs.h` defines `signals`/`slots`/`emit` as macros, so a field or
# parameter named after one of them in the generated C ABI header breaks every
# Qt translation unit that includes vd_ffi.h (the 2026-11-02 `signals` collision
# took the editor lane down). This target COMPILES
# crates/vd-ffi/tests/qt_abi_guard.cpp together with a REAL Qt header and the
# header AS IT EXISTS IN THE TREE (crates/vd-ffi/include/vd_ffi.h, regenerated
# by build.rs on every Rust build) - never a copy. Compile-only: the TU has
# main() and is never linked or run. The host-side wrapper is the Rust test
# crates/vd-ffi/tests/qt_abi_guard.rs; without Qt6 development files that test
# prints a loud SKIP naming the command it would have run, and this CMake target
# does not exist at all (the warning below names the missing path and command).
set(VEDIT_QT_ABI_GUARD_SOURCE
  "${CMAKE_CURRENT_SOURCE_DIR}/crates/vd-ffi/tests/qt_abi_guard.cpp")
set(VEDIT_QT_ABI_GUARD_HEADER
  "${CMAKE_CURRENT_SOURCE_DIR}/crates/vd-ffi/include/vd_ffi.h")
if(EXISTS "${VEDIT_QT_ABI_GUARD_SOURCE}" AND EXISTS "${VEDIT_QT_ABI_GUARD_HEADER}")
  add_library(vedit_qt_abi_guard OBJECT "${VEDIT_QT_ABI_GUARD_SOURCE}")
  set_target_properties(vedit_qt_abi_guard PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    # The in-tree generated header is not a CMake dependency; without this the
    # guard would keep a stale object when build.rs rewrites the header.
    OBJECT_DEPENDS "${VEDIT_QT_ABI_GUARD_HEADER}")
  target_include_directories(vedit_qt_abi_guard PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/crates/vd-ffi/include")
  # Qt6::Core gives the REAL Qt headers (QObject/QString); compile time only.
  target_link_libraries(vedit_qt_abi_guard PRIVATE Qt6::Core)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit_qt_abi_guard PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  message(STATUS "VEdit: Qt/ABI compile-only guard enabled (in-tree header: ${VEDIT_QT_ABI_GUARD_HEADER})")
else()
  message(WARNING
    "VEdit: Qt/ABI compile guard SKIPPED - missing "
    "${VEDIT_QT_ABI_GUARD_SOURCE} or ${VEDIT_QT_ABI_GUARD_HEADER}. "
    "When both exist the guard compiles: c++ -std=c++17 -fsyntax-only -fPIC "
    "$(pkg-config --cflags Qt6Core) -I crates/vd-ffi/include "
    "crates/vd-ffi/tests/qt_abi_guard.cpp")
endif()

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
