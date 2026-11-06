# Module tree lane (task T17; rebuilt for UX batch 2 item 1/2).
#
# Two docks, one asynchronous data source:
#   * "module-tree"    — a PURE MODULE HIERARCHY: `is_top` modules as roots, the
#                        modules each body instantiates as children. No net /
#                        driver / load / chain rows exist anywhere (user ruling).
#   * "interface-panel"— the focused module's ports: Name / Direction / Type /
#                        Width / Location, plus the port activation gestures.
# Data comes from the Rust module catalog through the C ABI (vd_catalog_start +
# poll, never on the GUI thread); every jump goes through the application-wide
# `jump::jumpTo()` contract.
#
# Two ways to build it, like every lane in this repository:
#   standalone  cmake -S src/workspace/module_tree -B target/ws-module-tree -G Ninja
#               cmake --build target/ws-module-tree && ctest --test-dir target/ws-module-tree
#   product     src/app/CMakeLists.txt lists `module_tree` in VEDIT_WORKSPACE_LANES
#               and src/workspace/integration.cpp calls installModuleTree().
cmake_minimum_required(VERSION 3.25)

get_property(VEDIT_WORKSPACE_MODULE_TREE_ADDED GLOBAL PROPERTY VEDIT_WORKSPACE_MODULE_TREE_ADDED)
if(VEDIT_WORKSPACE_MODULE_TREE_ADDED)
  return()
endif()
set_property(GLOBAL PROPERTY VEDIT_WORKSPACE_MODULE_TREE_ADDED TRUE)

set(VEDIT_MODULE_TREE_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VEditWorkspaceModuleTree LANGUAGES CXX)
  enable_testing()
  set(VEDIT_MODULE_TREE_STANDALONE ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(VEDIT_MODULE_TREE_STANDALONE)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)
else()
  find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

# The jump contract: the panels' only way to open a file position. The lane may
# be configured before src/app lists it, so make sure the target exists.
if(NOT TARGET vedit_workspace_jump)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../jump/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../jump" "${CMAKE_BINARY_DIR}/jump-lane")
  else()
    message(FATAL_ERROR "the module-tree lane needs the jump lane (src/workspace/jump)")
  endif()
endif()

add_library(vedit_workspace_module_tree STATIC
  module_catalog.cpp
  module_hierarchy.cpp
  catalog_client.cpp
  module_tree_panel.cpp
  interface_panel.cpp
  module_tree_integration.cpp
)
# Consumers include `module_tree/module_tree_integration.h` (the spelling
# src/workspace/integration.cpp uses), so the workspace directory is a public
# include root next to the lane's own directory.
target_include_directories(vedit_workspace_module_tree PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(vedit_workspace_module_tree PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  vedit_workspace_jump
)

# The application shell: only the integration entry point needs it (the panels
# and the models are pure Qt). A standalone lane build without the shell still
# compiles; installModuleTree() is then a documented no-op.
if(TARGET vedit_app_shell)
  target_link_libraries(vedit_workspace_module_tree PUBLIC vedit_app_shell)
  target_compile_definitions(vedit_workspace_module_tree PUBLIC VEDIT_MODULE_TREE_HAVE_APP_SHELL=1)
endif()

# The project model: the ABSOLUTE source of truth for the project root and for
# the effective file set (the union of the imported .f files and the folder
# scan). Without it the client falls back to a `vd_project_expand_v2` job.
if(TARGET vedit_workspace_project)
  target_link_libraries(vedit_workspace_module_tree PUBLIC vedit_workspace_project)
  target_compile_definitions(vedit_workspace_module_tree PUBLIC VEDIT_MODULE_TREE_HAVE_PROJECT=1)
endif()

# The Rust module catalog service over the C ABI (ux-batch-2 A1).
if(TARGET vd_ffi)
  target_link_libraries(vedit_workspace_module_tree PUBLIC vd_ffi)
  target_compile_definitions(vedit_workspace_module_tree PUBLIC VEDIT_MODULE_TREE_HAVE_FFI=1)
elseif(VEDIT_RUST_FFI_LIB AND EXISTS "${VEDIT_RUST_FFI_LIB}")
  target_link_libraries(vedit_workspace_module_tree PUBLIC "${VEDIT_RUST_FFI_LIB}")
  if(VEDIT_RUST_FFI_INCLUDE)
    target_include_directories(vedit_workspace_module_tree PUBLIC "${VEDIT_RUST_FFI_INCLUDE}")
  endif()
  target_compile_definitions(vedit_workspace_module_tree PUBLIC VEDIT_MODULE_TREE_HAVE_FFI=1)
endif()

# Reading the active editor's caret needs the KTextEditor interface. The panels
# themselves never include it (only the integration's caret probe does).
if(TARGET KF6::TextEditor)
  target_link_libraries(vedit_workspace_module_tree PUBLIC KF6::TextEditor)
  target_compile_definitions(vedit_workspace_module_tree PUBLIC
    VEDIT_MODULE_TREE_HAVE_KTEXTEDITOR=1)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_module_tree PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# --- the deterministic model/panel self-test --------------------------------
# No slang needed: the model checks use a synthetic catalog with the ABI's shape
# and the panels use a recording jump target in the real registry.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/module_tree_selftest.cpp")
  add_executable(vedit-module-tree-selftest module_tree_selftest.cpp)
  target_link_libraries(vedit-module-tree-selftest PRIVATE
    vedit_workspace_module_tree
    Qt6::Test)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-module-tree-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-module-tree-selftest COMMAND vedit-module-tree-selftest)
  set_tests_properties(vedit-module-tree-selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-module-tree"
    TIMEOUT 180
  )
endif()

# --- the REAL-catalog acceptance test (ux-batch-2 B1) -----------------------
# The fixture is the lane's own project: `shared` is instantiated by BOTH
# `top_a` and `top_b`, `orphan` is defined but never instantiated, `bus_if` is
# an interface (not a module), and `reflink` has a `Ref` port. The test drives
# the product's real catalog client (slang), the two real docks and the
# application-wide jump registry.
set(VEDIT_MODULE_TREE_CATALOG_FIXTURE
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ux-batch-2")
set(VEDIT_MODULE_TREE_SLANG
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/slang/out/slang")
if(NOT EXISTS "${VEDIT_MODULE_TREE_CATALOG_FIXTURE}")
  set(VEDIT_MODULE_TREE_CATALOG_FIXTURE "")
endif()
if(NOT EXISTS "${VEDIT_MODULE_TREE_SLANG}")
  set(VEDIT_MODULE_TREE_SLANG "")
endif()

# The acceptance test drives the product shell, the real KTextEditor provider and
# the real jump target, so it is a product-build test: a standalone lane build
# (no shell) still configures and runs the deterministic self-test above.
if(TARGET vedit_app_shell AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/module_tree_catalog_test.cpp")
  add_executable(vedit-module-tree-catalog-test module_tree_catalog_test.cpp)
  target_link_libraries(vedit-module-tree-catalog-test PRIVATE
    vedit_workspace_module_tree
    Qt6::Test)
  # The product editor provider, so the jump is verified on the REAL KTextEditor
  # document the application uses (same reason as the search lane's T69 check).
  if(TARGET vedit_editor_core)
    target_link_libraries(vedit-module-tree-catalog-test PRIVATE vedit_editor_core)
    target_compile_definitions(vedit-module-tree-catalog-test PRIVATE
      VEDIT_MODULE_TREE_HAVE_EDITOR_CORE=1)
  endif()
  # The product's ONE jump target lives in the search lane today; linking it in
  # the test only (never into this lane's library) keeps the cursor-landing
  # assertions on the real product path instead of a test-only target.
  if(TARGET vedit_workspace_search AND TARGET vedit_app_shell)
    target_link_libraries(vedit-module-tree-catalog-test PRIVATE vedit_workspace_search)
    target_compile_definitions(vedit-module-tree-catalog-test PRIVATE
      VEDIT_MODULE_TREE_HAVE_PRODUCT_JUMP=1)
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-module-tree-catalog-test PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vedit-module-tree-catalog-test
           COMMAND vedit-module-tree-catalog-test
                   "${VEDIT_MODULE_TREE_CATALOG_FIXTURE}"
                   "${VEDIT_MODULE_TREE_SLANG}")
  # A missing fixture/slang is a FAILURE, never a SKIP-as-pass (review M3): the
  # fixture is in-tree and slang is a build dependency, so the test exits 1 with
  # `0 check(s), 1 failure(s)` and ctest turns red instead of green-with-nothing.
  set_tests_properties(vedit-module-tree-catalog-test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-module-tree"
    TIMEOUT 240
  )
endif()
