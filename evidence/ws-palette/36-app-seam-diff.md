collected_at: unknown (header added retroactively at 2026-10-31T10:19:00Z)
git_head: 9957f5fe4d63c17c681e8753288262fa2c00955e
git_status_porcelain:
 M CMakeLists.txt
 M Cargo.lock
 M Cargo.toml
 M THIRD_PARTY_NOTICES.md
 M docs/decisions/r2-license.md
 M docs/handover/README.md
 M evidence/m0-build/build-logs/21-qt6-image-id.txt
 M evidence/m0-build/build-logs/31-qt6-configure.log
 M evidence/m0-build/build-logs/32-qt6-build.log
 M evidence/m0-build/build-logs/33-qt6-ctest.log
 M evidence/m0-build/build-logs/34-qt6-elf.txt
 M evidence/syn-grammars/00-pins.txt
 M evidence/syn-grammars/01-build.log
 M evidence/syn-grammars/02-tests.log
 M evidence/syn-grammars/03-manifest.txt
 M evidence/syn-grammars/04-manifest-none.txt
 M evidence/syn-grammars/04-manifest-static.txt
 M evidence/syn-grammars/04-manifest-systemverilog.txt
 M evidence/syn-grammars/04-manifest-verilog.txt
 M evidence/syn-grammars/04-swap-proof.txt
 M evidence/syn-grammars/05-layering.txt
 M evidence/syn-grammars/06-licensing.txt
 M evidence/syn-grammars/07-conclusion.md
 M evidence/syn-grammars/09-result.txt
 M evidence/term-panel/00-pins.txt
 M evidence/term-panel/01-configure.log
 M evidence/term-panel/02-build.log
 M evidence/term-panel/03-ctest.log
 M evidence/term-panel/03b-checks.log
 M evidence/term-panel/04-elf-needed.txt
 M evidence/term-panel/05-no-network.txt
 M evidence/term-panel/06-xcb-and-screenshots.log
 M evidence/term-panel/07-conclusion.md
 M evidence/term-panel/08-structure.txt
 M evidence/term-panel/09-root-build-with-lane.log
 M evidence/ws-filetree/00-pins.txt
 M evidence/ws-filetree/00-steps.txt
 M evidence/ws-filetree/01-configure.log
 M evidence/ws-filetree/02-build.log
 M evidence/ws-filetree/03-ctest.log
 M evidence/ws-filetree/03a-tests-full.log
 M evidence/ws-filetree/04-fixture.log
 M evidence/ws-filetree/05-screenshots.log
 M evidence/ws-filetree/06-permissions-nonroot.log
 M evidence/ws-filetree/07a-gate-qt6-cxx-pre-seam.log
 M evidence/ws-filetree/08-real-shell-seamcheck.log
 M evidence/ws-filetree/09-real-app-install.log
 M evidence/ws-filetree/10-rootcheck-configure.log
 M evidence/ws-filetree/11-rootcheck-build.log
 M evidence/ws-filetree/12-gate-build-qt-final.log
 M evidence/ws-filetree/12a-gate-build-qt-first.log
 M evidence/ws-filetree/13-gate-qt6-cxx-final.log
 M evidence/ws-filetree/14-real-shell-check-run.log
 M evidence/ws-filetree/REPORT.md
 M evidence/ws-palette/00-pins.txt
 M evidence/ws-palette/30-build-qt.log
 M evidence/ws-palette/31-bench.log
 M evidence/ws-palette/32-ctest-palette.log
 M evidence/ws-palette/33-elf.txt
 M evidence/ws-palette/34-artifacts.txt
 M evidence/ws-palette/36-app-seam-diff.md
 M evidence/ws-palette/37-gate-qt6-cxx.log
 M evidence/ws-palette/REPORT.md
 M evidence/ws-palette/SEAM.md
 M evidence/ws-search/04-selfcheck-root.log
 M evidence/ws-search/05-selfcheck-nonroot.log
 M evidence/ws-search/06-screenshots.log
 M evidence/ws-search/07-screenshots-nonroot.log
 M evidence/ws-search/08-build-qt.log
 M evidence/ws-search/09-structure.txt
 M evidence/ws-search/10-ctest-root-search.log
 M evidence/ws-search/13-integration-proof.log
 M evidence/ws-search/14-standalone.log
 M evidence/ws-search/REPORT.md
 M scripts/build-qt-in-container.sh
 M scripts/build-qt.sh
 M src/app/CMakeLists.txt
 M src/app/main.cpp
?? cmake/VEditRustFfi.cmake
?? crates/vd-ffi/
?? crates/vd-search/
?? crates/vd-settings/
?? crates/vd-vcs/
?? docs/handover/appendix/amendments-rev2.md
?? evidence/docs-amendments/
?? evidence/ffi-abi/
?? evidence/fixture/
?? evidence/m0-build/build-logs/37-qt6-rust-abi.txt
?? evidence/verdicts/
?? scripts/verify-fixture.sh
?? tests/fixtures/
backfill: 补记 —— C9 头为事后补齐；git_head / git_status_porcelain 为补记时刻快照，非原始采集时刻；原始采集时刻不可回溯，文件 mtime 2026-10-31T04:12:20Z 仅供参考
---
# app-side generic seam changes (T22, captain-authorized 2026-10-31)

Baseline: the T13 app-shell snapshot before the workspace integration. The
tree is not committed (src/ is untracked), so these unified diffs were
generated from the on-disk files with the added lines removed; the
'before' copies are build scratch under target/ and are not committed.

## 1) src/app/main.cpp — exactly two added lines (include + one call)
```diff
--- a/src/app/main.cpp
+++ b/src/app/main.cpp
@@ -12,6 +12,7 @@
 
 #include "command_registry.h"
 #include "editor_area.h"
+#include "integration.h"
 #include "panel_registry.h"
 #include "theme_manager.h"
 
@@ -388,6 +389,7 @@
     }
 
     ApplicationShell shell(options);
+    vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir});
     shell.show();
 
     const int holdMs = parser.value(QStringLiteral("hold")).toInt();
```

## 2) src/app/CMakeLists.txt — workspace-lane section appended (guarded by if(EXISTS))
```diff
--- a/src/app/CMakeLists.txt
+++ b/src/app/CMakeLists.txt
@@ -52,3 +52,53 @@
   TIMEOUT 120
 )
 
+# --- Workspace lanes (ws-filetree / ws-palette / ws-search) -----------------
+# A workspace lane is an OPTIONAL subdirectory: it exists only once the lane has
+# landed, and the unified entry point src/workspace/integration.cpp calls the
+# install<Lane>() functions it can see and silently skips the missing ones.
+# src/app therefore never learns which lanes are present, and landing a new
+# workspace lane does not edit this file. Convention: evidence/ws-palette/SEAM.md.
+set(VEDIT_WORKSPACE_LANES filetree palette search)
+foreach(lane IN LISTS VEDIT_WORKSPACE_LANES)
+  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/${lane}/CMakeLists.txt")
+    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../workspace/${lane}"
+                     "${CMAKE_CURRENT_BINARY_DIR}/workspace-lane-${lane}")
+    message(STATUS "VEdit: workspace lane '${lane}' included")
+  endif()
+endforeach()
+
+# Library target per lane. The convention is `vedit_workspace_<lane>`; a lane
+# that landed with its own target name is mapped here (one line each).
+set(VEDIT_WORKSPACE_FILETREE_TARGET vedit_file_tree)
+set(VEDIT_WORKSPACE_PALETTE_TARGET vedit_workspace_palette)
+set(VEDIT_WORKSPACE_SEARCH_TARGET vedit_workspace_search)
+
+if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/integration.cpp")
+  add_library(vedit_workspace_integration STATIC
+    "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/integration.cpp")
+  target_include_directories(vedit_workspace_integration PUBLIC
+    "${CMAKE_CURRENT_SOURCE_DIR}/../workspace")
+  target_link_libraries(vedit_workspace_integration PUBLIC vedit_app_shell)
+  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
+    target_compile_options(vedit_workspace_integration PRIVATE
+      -Wall -Wextra -Wpedantic -Werror)
+  endif()
+  foreach(lane IN LISTS VEDIT_WORKSPACE_LANES)
+    string(TOUPPER "${lane}" lane_upper)
+    set(lane_target "${VEDIT_WORKSPACE_${lane_upper}_TARGET}")
+    if(NOT lane_target)
+      set(lane_target "vedit_workspace_${lane}")
+    endif()
+    # A lane is only wired when BOTH its library and its integration entry file
+    # exist; adding <lane>/<lane>_integration.cpp is the whole step a lane takes.
+    if(TARGET ${lane_target} AND
+       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../workspace/${lane}/${lane}_integration.cpp")
+      target_link_libraries(vedit_workspace_integration PUBLIC ${lane_target})
+      target_compile_definitions(vedit_workspace_integration PRIVATE
+        "VEDIT_WORKSPACE_HAVE_${lane_upper}=1")
+      message(STATUS "VEdit: workspace lane '${lane}' wired (${lane_target})")
+    endif()
+  endforeach()
+  target_link_libraries(vedit-app PRIVATE vedit_workspace_integration)
+  message(STATUS "VEdit: workspace integration enabled (src/workspace/integration.cpp)")
+endif()
```

## 3) src/workspace/integration.h — NEW FILE (unified entry point)
```cpp
#pragma once

// Unified entry point for the workspace lanes (ws-filetree / ws-palette /
// ws-search) into the real `vedit-app` shell (task T22 seam, captain-approved).
//
// main.cpp calls installPanels() exactly once, after the ApplicationShell is
// constructed. src/workspace/integration.cpp then calls the install<Lane>()
// function of every lane whose CMake target is part of the build and silently
// skips the lanes that have not landed yet - src/app never learns lane names.
//
// Lane convention (documented in evidence/ws-palette/SEAM.md):
//   * directory:   src/workspace/<lane>/
//   * library:     target `vedit_workspace_<lane>`
//   * entry:       `void install<Lane>(vedit::app::ApplicationShell &,
//                                      const IntegrationOptions &)`
//                  declared in src/workspace/<lane>/<lane>_integration.h
#include <QString>

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

struct IntegrationOptions {
    QString projectRoot; // empty => the shell starts without a project
    QString stateDir;    // effective session/shortcut directory (may be empty)
};

// Registers every workspace lane that is part of this build. Safe to call with
// no lane present (the app then behaves exactly as before).
void installPanels(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
```

## 4) src/workspace/integration.cpp — NEW FILE (lane dispatch)
```cpp
#include "integration.h"

#include "app_shell.h"

#if defined(VEDIT_WORKSPACE_HAVE_PALETTE)
#include "palette/palette_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_FILETREE)
#include "filetree/filetree_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SEARCH)
#include "search/search_integration.h"
#endif

namespace vedit::workspace {

void installPanels(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    (void)shell;
    (void)options;

#if defined(VEDIT_WORKSPACE_HAVE_PALETTE)
    installPalette(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_FILETREE)
    installFileTree(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SEARCH)
    installSearch(shell, options);
#endif
}

} // namespace vedit::workspace
```

## 5) src/workspace/palette/palette_integration.{h,cpp} — NEW FILES (palette adapter)
Full files: src/workspace/palette/palette_integration.h / .cpp (sha256 in 00-pins.txt).

## Guard-rail evidence
- vedit-app NEEDED (KTextEditor|tree-sitter) count = 0: see 33-elf.txt.
- src/app/main.cpp untouched otherwise; palette content stays in src/workspace/palette/.
