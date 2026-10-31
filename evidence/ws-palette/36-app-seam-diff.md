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
