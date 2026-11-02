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
