#pragma once

// App-facing entry point of the project lane (workspace lane, task T6), called
// by `src/workspace/integration.cpp`:
//
//   * CMake target: `vedit_workspace_project`
//   * entry point:  `void installProject(ApplicationShell &, const IntegrationOptions &)`
//
// `installProject` resolves the project root (command line argument, then the
// state persisted by this lane, then the shell's saved session), creates the
// model and inserts the panel as its own dock (`dock-project`).
//
// AC-2.1: the dock is inserted at install time but only shown once a project
// exists; opening a folder in the file tree (`directoryOpened`) makes the panel
// appear and become the project root. The file tree is bound by signal NAME, so
// this lane keeps building standalone (see project_subscription.h).
//
// The panel is NOT registered in the shell's PanelRegistry: the shell already
// owns a fixed panel set, and registering a new one would mean editing
// `src/app` (shared by every lane). Consequence, recorded in README.md and in
// the task evidence: there is no View-menu toggle for the project panel yet;
// the dock is inserted whenever a project root is known.
#include "../integration.h"

#include <QString>

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

namespace project {
class ProjectModel;
}

void installProject(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

// The installed model, or nullptr when the seam is not active. Consumers (the
// lint pass, the settings page echo) read the effective file set, `-I`/`-D` and
// the diagnostics from it.
project::ProjectModel *projectModelForShell(const vedit::app::ApplicationShell &shell);

} // namespace vedit::workspace
