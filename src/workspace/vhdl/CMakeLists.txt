# VHDL workspace lane (batch 20261110c, AC-N7 / AC-6.7).
#
# src/app/CMakeLists.txt already lists `vhdl` in VEDIT_WORKSPACE_LANES and
# src/workspace/integration.cpp already calls installVhdl() behind
# VEDIT_WORKSPACE_HAVE_VHDL, so creating this directory IS the wiring step:
# neither of those two files is edited (the frozen contract forbids it).
#
# The lane resolves NOTHING itself: `vhdl_ls` / `ghdl` / `vsg` come from the
# diagnostics lane's ONE resolution order (bundled > local > PATH > env,
# `diagnostics::resolveToolchain`), and a missing tool is rendered as a single
# line — never as per-file errors.
if(NOT TARGET vedit_app_shell)
  message(STATUS
    "workspace-vhdl: no app-shell target; the lane is only built inside the product")
  return()
endif()

add_library(vedit_workspace_vhdl STATIC
  vhdl_integration.cpp
  vhdl_lsp.cpp
  vhdl_lint.cpp
  vhdl_format.cpp)
target_include_directories(vedit_workspace_vhdl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit_workspace_vhdl PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  vedit_app_shell
)
# AC-6.6: the format command dispatches on the ACTIVE KTextEditor document
# (the one undoable edit is applied through the diagnostics lane's helper).
if(TARGET KF6::TextEditor)
  target_link_libraries(vedit_workspace_vhdl PUBLIC KF6::TextEditor)
endif()
if(TARGET vedit_workspace_diagnostics)
  # AC-6.7: the missing-tool line reuses the diagnostics lane's resolver, so this
  # lane must not grow a second toolchain probe.
  target_link_libraries(vedit_workspace_vhdl PUBLIC vedit_workspace_diagnostics)
  target_compile_definitions(vedit_workspace_vhdl PRIVATE VEDIT_VHDL_HAVE_TOOLCHAIN=1
                             VEDIT_VHDL_HAVE_MERGE_KEY=1)
else()
  message(STATUS
    "workspace-vhdl: diagnostics lane absent; the toolchain line reports it cannot resolve")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_vhdl PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# AC-6.2 evidence generator: the REAL `vhdl_ls` session, driven end to end.
#
# It is compiled from vhdl_lsp.cpp itself with VEDIT_VHDL_LSP_PROBE_MAIN, so the
# lane needs no extra source file (its write scope stays exact) and the library
# carries none of the probe code. It links Qt6::Core only and is NOT registered
# with ctest: the acceptance-count baseline is the split's, and this probe is run
# where a real vhdl_ls binary exists (the evidence log records the run).
add_executable(vedit-vhdl-lsp-probe vhdl_lsp.cpp)
target_compile_definitions(vedit-vhdl-lsp-probe PRIVATE VEDIT_VHDL_LSP_PROBE_MAIN=1)
target_include_directories(vedit-vhdl-lsp-probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vedit-vhdl-lsp-probe PRIVATE Qt6::Core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-vhdl-lsp-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# AC-6.4 evidence generator: the REAL GHDL + VSG merge, driven end to end over
# seven fixtures (clean, clean-before, ghdl-only, vsg-only, mixed, syntax,
# two-errors). Compiled from vhdl_lint.cpp with VEDIT_VHDL_LINT_PROBE_MAIN, like
# the lane's LSP probe, so the lane needs no extra source file and none of the
# probe code links into the product. Not registered with ctest: it runs where the
# real GHDL/VSG binaries exist, and the evidence log records the run.
add_executable(vedit-vhdl-lint-probe vhdl_lint.cpp)
target_compile_definitions(vedit-vhdl-lint-probe PRIVATE VEDIT_VHDL_LINT_PROBE_MAIN=1)
target_include_directories(vedit-vhdl-lint-probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(TARGET vedit_workspace_diagnostics)
  # The merge reuses the panel's OWN identity function (diagnosticMergeKey), so
  # the two can never disagree about "the same finding".
  target_link_libraries(vedit-vhdl-lint-probe
    PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets vedit_workspace_diagnostics)
  target_compile_definitions(vedit-vhdl-lint-probe PRIVATE VEDIT_VHDL_HAVE_MERGE_KEY=1)
else()
  target_link_libraries(vedit-vhdl-lint-probe PRIVATE Qt6::Core)
  target_compile_definitions(vedit-vhdl-lint-probe
    PRIVATE VEDIT_VHDL_LINT_PROBE_NO_MERGE_KEY=1)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-vhdl-lint-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# AC-6.4 evidence generator #2: what the LANE's rows do in the product's ONE
# display view. A GUI run proves the rows arrive; this proves the publish
# SEMANTICS (replace / clear / upper-layer suppression / per-source independence /
# the dot), which the harness cannot drive through the real window.
if(TARGET vedit_workspace_diagnostics)
  add_executable(vedit-vhdl-lane-probe vhdl_lint.cpp)
  target_compile_definitions(vedit-vhdl-lane-probe PRIVATE VEDIT_VHDL_LANE_PROBE_MAIN=1
                             VEDIT_VHDL_HAVE_MERGE_KEY=1)
  target_include_directories(vedit-vhdl-lane-probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vedit-vhdl-lane-probe
    PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets vedit_workspace_diagnostics)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-vhdl-lane-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endif()

# AC-6.6 evidence generator: the REAL `vsg --fix` protocol over a real
# KTextEditor document (temp copy -> fix -> GHDL re-check -> diff -> ONE undo;
# failure = zero change; the file on disk is never written). Compiled from
# vhdl_format.cpp with VEDIT_VHDL_FORMAT_PROBE_MAIN, exactly like the lane's LSP
# and lint probes, so no extra source file is added and none of the probe code
# links into the product. Not registered with ctest: it runs where the real
# VSG/GHDL binaries and KTextEditor exist, and the evidence log records the run.
if(TARGET vedit_workspace_diagnostics)
  add_executable(vedit-vhdl-format-probe vhdl_format.cpp vhdl_lint.cpp)
  target_compile_definitions(vedit-vhdl-format-probe PRIVATE VEDIT_VHDL_FORMAT_PROBE_MAIN=1
                             VEDIT_VHDL_HAVE_MERGE_KEY=1)
  target_include_directories(vedit-vhdl-format-probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vedit-vhdl-format-probe
    PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets vedit_workspace_diagnostics)
  if(TARGET KF6::TextEditor)
    target_link_libraries(vedit-vhdl-format-probe PRIVATE KF6::TextEditor)
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vedit-vhdl-format-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endif()
