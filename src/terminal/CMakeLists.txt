# Terminal panel (task T35): local shell sessions, dockable/closable/rebuildable,
# exit code visible, no remote transport.
#
# Two ways to build it:
#   * as part of the product  -- the parent CMakeLists adds this directory
#     (add_subdirectory(src/terminal)); the root project already found Qt6 and
#     set C++20 through cmake/VEditVersions.cmake;
#   * standalone             -- `cmake -S src/terminal -B <build>` for the lane's
#     own verification, which is exactly what evidence/term-panel/ records.
#
# There is no QTermWidget in the pinned image (and it is GPL-2.0+ while VEdit is
# MIT), so the panel carries its own pty + VT emulator; the only dependencies are
# Qt6 Core/Gui/Widgets and the POSIX pty API in libc. See README.md.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.25)
  project(VEditTerminal VERSION 0.2.0 LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  enable_testing()
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
endif()

set(CMAKE_AUTOMOC ON)

set(VEDIT_TERMINAL_SOURCES
  pty_session.cpp
  terminal_panel.cpp
  terminal_widget.cpp
  vt_screen.cpp
)

add_library(vedit_terminal STATIC ${VEDIT_TERMINAL_SOURCES})
target_include_directories(vedit_terminal PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_terminal PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_terminal PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vedit-terminal-selftest terminal_selftest.cpp)
target_link_libraries(vedit-terminal-selftest PRIVATE vedit_terminal)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-terminal-selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Compile-time proof that the panel factory fits PanelRegistry::setFactory()
# (seam_check.cpp). It is BUILT but never linked: the shell's implementation stays
# out of this lane. Skipped when src/app has not landed.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../app/panel_registry.h")
  add_library(vedit_terminal_seam_check OBJECT seam_check.cpp)
  target_include_directories(vedit_terminal_seam_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vedit_terminal_seam_check PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit_terminal_seam_check PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endif()

add_test(NAME vedit-terminal-selftest COMMAND vedit-terminal-selftest --selfcheck)
set_tests_properties(vedit-terminal-selftest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime;LANG=C.UTF-8"
  TIMEOUT 180
)
