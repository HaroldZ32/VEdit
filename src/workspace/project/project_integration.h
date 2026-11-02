#pragma once

// App-facing entry point of the project lane (workspace lane, Slice 1 / task T6),
// called by `src/workspace/integration.cpp`:
//
//   * CMake target: `vedit_workspace_project`
//   * entry point:  `void installProject(ApplicationShell &, const IntegrationOptions &)`
//
// `installProject` resolves the project root (command line argument, then the
// state persisted by this lane, then the shell's saved session), creates the
// model and inserts the panel as its own dock (`dock-project`).
//
// The panel is NOT registered in the shell's PanelRegistry: the shell already
// owns a fixed panel set, and registering a new one would mean editing
// `src/app` (shared by every lane). Consequence, recorded in README.md and in
// the task evidence: there is no View-menu toggle for the project panel yet;
// the dock is inserted whenever a project root is known.
#include "../integration.h"

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

void installProject(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
