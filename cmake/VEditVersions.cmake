#[============================================================================]
# VEditVersions.cmake — the ONE place the C++ lane's locked versions enter CMake.
#
# The numbers are NOT duplicated here: they are read from
# `scripts/versions-qt.env`, the same file `scripts/lib-qt.sh` and the pinned
# build image (`scripts/docker/Dockerfile.build`, stage `qt6-build`) consume.
# Every dependency is then requested with `EXACT`, so a machine that resolves a
# different Qt6 / KF6 / tree-sitter fails this configuration loudly instead of
# quietly building against a version no evidence covers.
# =============================================================================]
#[============================================================================]

set(VEDIT_VERSIONS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/versions-qt.env")
if(NOT EXISTS "${VEDIT_VERSIONS_FILE}")
  message(FATAL_ERROR
    "VEdit: ${VEDIT_VERSIONS_FILE} is missing. It is the single source of truth "
    "for the Qt6/KF6 version matrix; restore it before configuring.")
endif()

# Read `KEY="value"` (quotes optional) from the pinned env file.
function(vedit_pinned_version key out_var)
  file(STRINGS "${VEDIT_VERSIONS_FILE}" _lines REGEX "^${key}=")
  if(NOT _lines)
    message(FATAL_ERROR "VEdit: '${key}' is not pinned in ${VEDIT_VERSIONS_FILE}")
  endif()
  list(LENGTH _lines _count)
  if(NOT _count EQUAL 1)
    message(FATAL_ERROR "VEdit: '${key}' is pinned ${_count} times in ${VEDIT_VERSIONS_FILE}; expected exactly once")
  endif()
  string(REGEX REPLACE "^${key}=\"?([^\"]*)\"?$" "\\1" _value "${_lines}")
  if(_value STREQUAL "")
    message(FATAL_ERROR "VEdit: '${key}' is empty in ${VEDIT_VERSIONS_FILE}")
  endif()
  set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

foreach(_key IN ITEMS
    VEDIT_QT6_VERSION
    VEDIT_QT6_MIN_VERSION
    VEDIT_KF6_VERSION
    VEDIT_KF6_MIN_VERSION
    VEDIT_KF6_REQUIRED_QT_VERSION
    VEDIT_KTEXTEDITOR_VERSION
    VEDIT_KSYNTAXHIGHLIGHTING_VERSION
    VEDIT_TREE_SITTER_VERSION
    VEDIT_QT6_DISTRO
    VEDIT_QT6_DISTRO_GLIBC
    VEDIT_QT6_MIN_KERNEL)
  vedit_pinned_version(${_key} _pinned)
  set(${_key} "${_pinned}" CACHE INTERNAL "pinned by scripts/versions-qt.env")
endforeach()

# The floors are product requirements (C5): KF6 starts at Qt >= 6.5. The locked
# matrix must stay above them; if a future edit breaks that, fail here instead of
# discovering it during a release.
if(VEDIT_QT6_VERSION VERSION_LESS VEDIT_QT6_MIN_VERSION)
  message(FATAL_ERROR "VEdit: locked Qt ${VEDIT_QT6_VERSION} is below the C5 floor ${VEDIT_QT6_MIN_VERSION}")
endif()
if(VEDIT_KF6_VERSION VERSION_LESS VEDIT_KF6_MIN_VERSION)
  message(FATAL_ERROR "VEdit: locked KF6 ${VEDIT_KF6_VERSION} is below the C5 floor ${VEDIT_KF6_MIN_VERSION}")
endif()

# Qt 6.10 / KF6 6.24 are built against C++20; fix the language level in one
# place so every target in the tree agrees.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS "VEdit version matrix (scripts/versions-qt.env):")
message(STATUS "  distro:            Ubuntu 26.04.1 LTS build image, glibc ${VEDIT_QT6_DISTRO_GLIBC} (min kernel ${VEDIT_QT6_MIN_KERNEL})")
message(STATUS "  Qt6:               ${VEDIT_QT6_VERSION} (floor ${VEDIT_QT6_MIN_VERSION})")
message(STATUS "  KF6:               ${VEDIT_KF6_VERSION} (floor ${VEDIT_KF6_MIN_VERSION}, upstream needs Qt >= ${VEDIT_KF6_REQUIRED_QT_VERSION})")
message(STATUS "  KTextEditor:       ${VEDIT_KTEXTEDITOR_VERSION}")
message(STATUS "  KSyntaxHighlighting: ${VEDIT_KSYNTAXHIGHLIGHTING_VERSION}")
message(STATUS "  tree-sitter:       ${VEDIT_TREE_SITTER_VERSION}")
