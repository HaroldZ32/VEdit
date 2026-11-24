# Settings panel lane (Slice 3): theme + key binding persistence on top of the
# Rust settings store (crates/vd-settings through crates/vd-ffi).
#
# The panel itself is part of the product build: src/app/CMakeLists.txt lists
# `settings` in VEDIT_WORKSPACE_LANES and src/workspace/integration.cpp calls
# installSettings() behind VEDIT_WORKSPACE_HAVE_SETTINGS, exactly like the other
# workspace lanes. This file also builds stand-alone (CMAKE_SOURCE_DIR == here)
# for the lane selftest, in which case it compiles the two shell sources the
# panel consumes (ThemeManager, CommandRegistry).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(VEditWorkspaceSettings LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
  find_package(KF6 REQUIRED COMPONENTS SyntaxHighlighting)
  enable_testing()
endif()

set(CMAKE_AUTOMOC ON)

set(VEDIT_WORKSPACE_SETTINGS_SOURCES
  format_options.cpp
  lint_options.cpp
  settings_client.cpp
  settings_dialog.cpp
  settings_panel.cpp
  ui_scale.cpp
)
if(TARGET vedit_app_shell)
  list(APPEND VEDIT_WORKSPACE_SETTINGS_SOURCES settings_integration.cpp)
endif()

add_library(vedit_workspace_settings STATIC ${VEDIT_WORKSPACE_SETTINGS_SOURCES})
target_include_directories(vedit_workspace_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_settings PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
if(TARGET vedit_app_shell)
  # The shell provides ThemeManager / CommandRegistry (the settings lane consumes
  # them; it never duplicates them).
  target_link_libraries(vedit_workspace_settings PUBLIC vedit_app_shell)
else()
  # Stand-alone lane build: compile exactly the two shell sources this lane uses.
  target_include_directories(vedit_workspace_settings PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app")
  target_sources(vedit_workspace_settings PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app/theme_manager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app/command_registry.cpp"
  )
  target_link_libraries(vedit_workspace_settings PUBLIC KF6::SyntaxHighlighting)
endif()

# Optional C ABI: the panel consumes the Rust settings store (load/save jobs).
# Resolution order: the root build's cache variables, then the checkout defaults.
set(VEDIT_SETTINGS_FFI_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../../../crates/vd-ffi/include/vd_ffi.h")
set(VEDIT_SETTINGS_FFI_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../../target/release/libvd_ffi.a")
if(DEFINED VEDIT_RUST_FFI_INCLUDE AND VEDIT_RUST_FFI_INCLUDE)
  set(VEDIT_SETTINGS_FFI_HEADER "${VEDIT_RUST_FFI_INCLUDE}/vd_ffi.h")
endif()
if(DEFINED VEDIT_RUST_FFI_LIB AND VEDIT_RUST_FFI_LIB)
  set(VEDIT_SETTINGS_FFI_LIB "${VEDIT_RUST_FFI_LIB}")
endif()
if(EXISTS "${VEDIT_SETTINGS_FFI_HEADER}" AND EXISTS "${VEDIT_SETTINGS_FFI_LIB}")
  find_package(Threads REQUIRED)
  get_filename_component(VEDIT_SETTINGS_FFI_INCLUDE_DIR "${VEDIT_SETTINGS_FFI_HEADER}" DIRECTORY)
  target_compile_definitions(vedit_workspace_settings PUBLIC VEDIT_SETTINGS_HAVE_FFI=1)
  target_include_directories(vedit_workspace_settings PUBLIC "${VEDIT_SETTINGS_FFI_INCLUDE_DIR}")
  target_link_libraries(vedit_workspace_settings PUBLIC "${VEDIT_SETTINGS_FFI_LIB}" Threads::Threads ${CMAKE_DL_LIBS} m)
  message(STATUS "workspace-settings: linking the vd-ffi settings ABI (${VEDIT_SETTINGS_FFI_LIB})")
else()
  message(STATUS "workspace-settings: no vd-ffi library found; the panel reports missing storage")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_settings PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-workspace-settings-selftest settings_selftest.cpp)
# batch-20261113 T5 Pass B: the Detect selftest runs a REAL local QTcpServer
# (127.0.0.1, ephemeral port) so the button is proven against a real socket. Only
# the TEST needs Qt Network; the product code uses the Rust C ABI.
find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(vedit-workspace-settings-selftest PRIVATE
  vedit_workspace_settings
  Qt6::Network
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-workspace-settings-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# UX batch 2 / A4, acceptance ⑥: when the product binary is part of this build,
# the appearance scale is asserted through the REAL restart path — the ctest runs
# `vedit-app --selfcheck` with a settings.toml and reads the UI-SCALE line the
# pre-QApplication code printed. A standalone lane build has no vedit-app and the
# check reports an explicit SKIP instead of faking it.
if(TARGET vedit-app)
  target_compile_definitions(vedit-workspace-settings-selftest PRIVATE
    "VEDIT_APP_BINARY_PATH=\"$<TARGET_FILE:vedit-app>\"")
endif()
# The Format page's argv is asserted through the REAL formatVerilog the product
# command runs (a fake formatter echoes its argv), so this lane's self-test links
# the diagnostics lane when the whole tree is being built.
if(TARGET vedit_workspace_diagnostics)
  target_link_libraries(vedit-workspace-settings-selftest PRIVATE vedit_workspace_diagnostics)
  target_compile_definitions(vedit-workspace-settings-selftest PRIVATE
    VEDIT_HAVE_DIAGNOSTICS_FORMAT=1)
endif()
# batch-20261114 E2: the Environment candidate filter is asserted against the
# REAL project model (a real `.f` expansion producing the referenced names and
# their layers), never a hand-written map, so the self-test consumes the same
# `expansionInputs()` payload the product publishes on the shell property.
if(TARGET vedit_workspace_project)
  target_link_libraries(vedit-workspace-settings-selftest PRIVATE vedit_workspace_project)
  target_compile_definitions(vedit-workspace-settings-selftest PRIVATE
    VEDIT_HAVE_PROJECT_LANE=1)
endif()
add_test(NAME vedit-workspace-settings-selftest COMMAND vedit-workspace-settings-selftest --selfcheck)
set_tests_properties(vedit-workspace-settings-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-settings"
  TIMEOUT 300
)
# The dedicated row for the restart switch (acceptance ⑥), so the acceptance
# matrix can point at a test whose name says exactly what it proves.
add_test(NAME vedit-ui-scale-restart
  COMMAND vedit-workspace-settings-selftest --scale-restart
          "${CMAKE_BINARY_DIR}/a4-ui-scale-restart")
set_tests_properties(vedit-ui-scale-restart PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-settings"
  TIMEOUT 300
)

# Batch 20261110c (AC-4.3/4.4, AC-N5): the Preferences → Environment page under a
# REAL window. Every interaction in `--env-ui-probe` is a hit-test click
# (QApplication::widgetAt + sendEvent on the widget under the point), following
# the `vedit-fold-gutter-click` pattern; without xvfb-run the row SKIPs with its
# reason (exit 77, the ctest SKIP_RETURN_CODE) instead of passing silently.
find_program(VEDIT_ENV_UI_XVFB_RUN xvfb-run)
if(VEDIT_ENV_UI_XVFB_RUN)
  add_test(NAME vedit-env-import-ui
    COMMAND ${VEDIT_ENV_UI_XVFB_RUN} -a -s "-screen 0 1600x1200x24"
            $<TARGET_FILE:vedit-workspace-settings-selftest>
            --env-ui-probe "${CMAKE_CURRENT_BINARY_DIR}/env-ui-probe")
  set_tests_properties(vedit-env-import-ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=xcb;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime-env-ui"
    SKIP_RETURN_CODE 77
    TIMEOUT 300
  )
else()
  add_test(NAME vedit-env-import-ui
    COMMAND bash -c "echo 'SKIP: xvfb-run not available'; exit 77")
  set_tests_properties(vedit-env-import-ui PROPERTIES SKIP_RETURN_CODE 77)
endif()
