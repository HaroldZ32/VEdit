# Edit-core kernel lane (Slice 1, task T10): KTextEditor as the product's
# editing kernel, with no Rust/C-ABI involvement on the edit path.
#
# Two ways to build it, like every lane in this repository:
#   * standalone  -- cmake -S src/editor -B <build> ; ctest --test-dir <build>
#   * product     -- src/app/CMakeLists.txt add_subdirectory()s this directory
#                    and links vedit_editor_core into vedit-app (the root
#                    CMakeLists.txt is owned by the build-base lane and is not
#                    edited by this lane).
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VEditEditCore LANGUAGES CXX)
  set(VEDIT_EDIT_CORE_STANDALONE ON)
  enable_testing()
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
  find_package(KF6TextEditor REQUIRED)
else()
  set(VEDIT_EDIT_CORE_STANDALONE OFF)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(VEDIT_EDIT_CORE_SOURCES
  kernel/atomic_save.cpp
  kernel/cross_boundary.cpp
  kernel/document_io.cpp
  kernel/edit_core_selftest.cpp
  kernel/find_replace.cpp
  kernel/goto_line.cpp
  kernel/multi_cursor.cpp
  kernel/undo_ops.cpp
)

# The EditorProvider seam lives in src/app (the shell owns the interface); the
# kernel only needs the header, never the shell implementation. When src/app is
# absent (a bare kernel checkout) the provider adapter is left out.
set(VEDIT_EDIT_CORE_HAVE_APP_SEAM OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../app/editor_provider.h")
  set(VEDIT_EDIT_CORE_HAVE_APP_SEAM ON)
  list(APPEND VEDIT_EDIT_CORE_SOURCES kernel/ktexteditor_provider.cpp)
endif()

add_library(vedit_editor_core STATIC ${VEDIT_EDIT_CORE_SOURCES})
target_include_directories(vedit_editor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_editor_core PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  KF6::TextEditor
)
if(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
  target_include_directories(vedit_editor_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../app")
  target_compile_definitions(vedit_editor_core PUBLIC VEDIT_EDIT_CORE_HAVE_APP_SEAM=1)
  message(STATUS "VEdit edit-core: src/app EditorProvider seam found -> provider adapter built")
else()
  message(STATUS "VEdit edit-core: no src/app seam -> provider adapter not built")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_editor_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-edit-core-selftest kernel/edit_core_selftest_main.cpp)
target_link_libraries(vedit-edit-core-selftest PRIVATE vedit_editor_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-edit-core-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vedit-edit-core-selftest COMMAND vedit-edit-core-selftest)
set_tests_properties(vedit-edit-core-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 300
)
