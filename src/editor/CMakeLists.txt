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
  find_package(KF6SyntaxHighlighting REQUIRED)
else()
  set(VEDIT_EDIT_CORE_STANDALONE OFF)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(VEDIT_EDIT_CORE_SOURCES
  kernel/atomic_save.cpp
  kernel/cross_boundary.cpp
  kernel/diagnostics_annotations.cpp
  kernel/document_io.cpp
  kernel/edit_core_selftest.cpp
  kernel/find_replace.cpp
  kernel/goto_line.cpp
  kernel/highlight_diagnosis.cpp
  kernel/highlight_mode.cpp
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

# --- T94: the live consumers of the folding + breadcrumb lanes --------------
# The editor is the consumer of both lanes, so it wires them (the root
# CMakeLists.txt is owned by the build-base lane). Guarded: a tree without the
# lanes still configures and builds, just without the wiring.
set(VEDIT_EDIT_CORE_HAVE_FOLDING OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../syntax/folding/CMakeLists.txt"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/breadcrumb/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../syntax/folding"
                   "${CMAKE_CURRENT_BINARY_DIR}/folding-lane")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../workspace/breadcrumb"
                   "${CMAKE_CURRENT_BINARY_DIR}/breadcrumb-lane")
  list(APPEND VEDIT_EDIT_CORE_SOURCES kernel/fold_breadcrumb.cpp)
  set(VEDIT_EDIT_CORE_HAVE_FOLDING ON)
  message(STATUS "VEdit edit-core: folding + breadcrumb lanes wired into the live editor (T94)")
endif()

# --- T104: FIM inline completion (the editor is the lane's consumer) --------
# The lane (src/editor/fim, T27) owns the state machine and the Rust C-ABI
# backend; the kernel only needs the per-view binding. Guarded: a tree without
# the lane still configures and builds.
set(VEDIT_EDIT_CORE_HAVE_FIM OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fim/CMakeLists.txt")
  # The FIM lane names its FFI inputs VEDIT_FFI_STATICLIB / VEDIT_FFI_INCLUDE_DIR.
  # Map the checkout build (the same libvd_ffi.a the other lanes link) into them;
  # without this the lane would build without the Rust chain and the product
  # could never reach the service (the wiring would only ever report "disabled").
  set(VEDIT_FFI_CHECKOUT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../target/release/libvd_ffi.a")
  set(VEDIT_FFI_CHECKOUT_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../crates/vd-ffi/include")
  if(NOT VEDIT_FFI_STATICLIB AND EXISTS "${VEDIT_FFI_CHECKOUT_LIB}")
    set(VEDIT_FFI_STATICLIB "${VEDIT_FFI_CHECKOUT_LIB}" CACHE FILEPATH
        "libvd_ffi.a (release) for the in-process FIM service" FORCE)
  endif()
  if(NOT VEDIT_FFI_INCLUDE_DIR AND EXISTS "${VEDIT_FFI_CHECKOUT_INCLUDE}/vd_ffi.h")
    set(VEDIT_FFI_INCLUDE_DIR "${VEDIT_FFI_CHECKOUT_INCLUDE}" CACHE PATH
        "directory holding the cbindgen vd_ffi.h" FORCE)
  endif()
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/fim"
                   "${CMAKE_CURRENT_BINARY_DIR}/fim-lane")
  list(APPEND VEDIT_EDIT_CORE_SOURCES kernel/fim_wiring.cpp)
  set(VEDIT_EDIT_CORE_HAVE_FIM ON)
  message(STATUS "VEdit edit-core: FIM inline-completion lane wired into the live editor (T104)")
endif()

add_library(vedit_editor_core STATIC ${VEDIT_EDIT_CORE_SOURCES})
target_include_directories(vedit_editor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_editor_core PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  KF6::TextEditor
  # The highlighting diagnosis (T81) counts format ranges with the same engine
  # KTextEditor::View uses; the editor target already depends on it transitively,
  # but the dependency is real and stated here.
  KF6::SyntaxHighlighting
)
if(VEDIT_EDIT_CORE_HAVE_FOLDING)
  target_link_libraries(vedit_editor_core PUBLIC
    vedit_syntax_folding
    vedit_workspace_breadcrumb
  )
  target_compile_definitions(vedit_editor_core PUBLIC VEDIT_EDIT_CORE_HAVE_FOLDING=1)
endif()
if(VEDIT_EDIT_CORE_HAVE_FIM)
  target_link_libraries(vedit_editor_core PUBLIC vedit_editor_fim)
  target_compile_definitions(vedit_editor_core PUBLIC VEDIT_EDIT_CORE_HAVE_FIM=1)
endif()

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
# The delivery-like highlighting diagnosis needs the shipped XML files to build
# its private XDG share directory (the same two files packaging installs).
target_compile_definitions(vedit-edit-core-selftest PRIVATE
  VEDIT_SYNTAX_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../resources")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-edit-core-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-native-fold-tests kernel/native_fold_tests.cpp)
target_link_libraries(vedit-native-fold-tests PRIVATE vedit_editor_core)
if(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
  target_include_directories(vedit-native-fold-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../app")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-native-fold-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME vedit-native-fold-tests COMMAND vedit-native-fold-tests)
set_tests_properties(vedit-native-fold-tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 120
)

add_test(NAME vedit-edit-core-selftest COMMAND vedit-edit-core-selftest)
set_tests_properties(vedit-edit-core-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 300
)

# T104: the FIM wiring has its own headless entry (real provider + real lane).
if(VEDIT_EDIT_CORE_HAVE_FIM)
  add_executable(vedit-fim-wiring-selftest kernel/fim_wiring_selftest.cpp)
  target_link_libraries(vedit-fim-wiring-selftest PRIVATE vedit_editor_core)
  if(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
    target_include_directories(vedit-fim-wiring-selftest PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/../app")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-fim-wiring-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-fim-wiring-selftest COMMAND vedit-fim-wiring-selftest --selfcheck)
  set_tests_properties(vedit-fim-wiring-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-fim"
    TIMEOUT 120
  )
endif()
