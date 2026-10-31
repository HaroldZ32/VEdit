# Application shell (app-shell lane, T13).
#
# The root CMakeLists.txt adds this directory automatically once this file
# exists (docs/BUILD.md §12.4), so this lane never edits the root build.
#
# Targets:
#   vedit_app_shell          static library with the shell implementation
#   vedit-app                the shell executable (layout/menu/theme/docks)
#   vedit-app-shell-selftest ctest entry running `vedit-app --selfcheck`
#
# The shell links Qt6 Widgets + KSyntaxHighlighting only. It deliberately does
# NOT link KTextEditor: the editor is a seam (src/app/editor_provider.h) owned by
# src/editor (edit-core). That keeps the shell buildable and testable before the
# editor lane lands, and keeps the editor lane free to pick its own widgets.

set(CMAKE_AUTOMOC ON)

set(VEDIT_APP_SHELL_SOURCES
  app_shell.cpp
  command_registry.cpp
  editor_area.cpp
  editor_provider.cpp
  panel_registry.cpp
  session_store.cpp
  theme_manager.cpp
)

add_library(vedit_app_shell STATIC ${VEDIT_APP_SHELL_SOURCES})
target_include_directories(vedit_app_shell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_app_shell PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  KF6::SyntaxHighlighting
)
# Same C++ static-check policy as the M0 smoke target (docs/BUILD.md §12.4/12.5):
# warnings in our own code are errors.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_app_shell PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-app main.cpp)
target_link_libraries(vedit-app PRIVATE vedit_app_shell)
# Terminal panel lane (T35): the shell registers the "terminal" panel id but the
# factory and the widget live in this lane. Guarded so a tree without the lane
# still configures (same "missing lane => skip" promise as the root lane list).
if(TARGET vedit_terminal)
  target_link_libraries(vedit-app PRIVATE vedit_terminal)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-app PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vedit-app-shell-selftest
         COMMAND vedit-app --selfcheck --state-dir "${CMAKE_CURRENT_BINARY_DIR}/selftest-state")
set_tests_properties(vedit-app-shell-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 120
)

# --- Workspace lanes (ws-filetree / ws-palette / ws-search) -----------------
# A workspace lane is an OPTIONAL subdirectory: it exists only once the lane has
# landed, and the unified entry point src/workspace/integration.cpp calls the
# install<Lane>() functions it can see and silently skips the missing ones.
# src/app therefore never learns which lanes are present, and landing a new
# workspace lane does not edit this file. Convention: evidence/ws-palette/SEAM.md.
set(VEDIT_WORKSPACE_LANES filetree palette search)
foreach(lane IN LISTS VEDIT_WORKSPACE_LANES)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/${lane}/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../workspace/${lane}"
                     "${CMAKE_CURRENT_BINARY_DIR}/workspace-lane-${lane}")
    message(STATUS "VEdit: workspace lane '${lane}' included")
  endif()
endforeach()

# Library target per lane. The convention is `vedit_workspace_<lane>`; a lane
# that landed with its own target name is mapped here (one line each).
set(VEDIT_WORKSPACE_FILETREE_TARGET vedit_file_tree)
set(VEDIT_WORKSPACE_PALETTE_TARGET vedit_workspace_palette)
set(VEDIT_WORKSPACE_SEARCH_TARGET vedit_workspace_search)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/integration.cpp")
  add_library(vedit_workspace_integration STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/integration.cpp")
  target_include_directories(vedit_workspace_integration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../workspace")
  target_link_libraries(vedit_workspace_integration PUBLIC vedit_app_shell)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit_workspace_integration PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  foreach(lane IN LISTS VEDIT_WORKSPACE_LANES)
    string(TOUPPER "${lane}" lane_upper)
    set(lane_target "${VEDIT_WORKSPACE_${lane_upper}_TARGET}")
    if(NOT lane_target)
      set(lane_target "vedit_workspace_${lane}")
    endif()
    # A lane is only wired when BOTH its library and its integration entry file
    # exist; adding <lane>/<lane>_integration.cpp is the whole step a lane takes.
    if(TARGET ${lane_target} AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/${lane}/${lane}_integration.cpp")
      target_link_libraries(vedit_workspace_integration PUBLIC ${lane_target})
      target_compile_definitions(vedit_workspace_integration PRIVATE
        "VEDIT_WORKSPACE_HAVE_${lane_upper}=1")
      message(STATUS "VEdit: workspace lane '${lane}' wired (${lane_target})")
    endif()
  endforeach()
  target_link_libraries(vedit-app PRIVATE vedit_workspace_integration)
  message(STATUS "VEdit: workspace integration enabled (src/workspace/integration.cpp)")
endif()
