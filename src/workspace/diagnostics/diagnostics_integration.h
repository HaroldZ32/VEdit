#pragma once

// Entry point of the diagnostics lane, called by src/workspace/integration.cpp
// (the generic workspace-lane seam; see src/workspace/integration.h):
//
//   * CMake target: `vedit_workspace_diagnostics`
//   * entry point:  `void installDiagnostics(ApplicationShell &, const IntegrationOptions &)`
//
// It registers the "outline" panel factory, a "project.reparse" command, and
// starts the project-model job that feeds the controller with the effective file
// set (A3c). Navigation entry points are exposed by DiagnosticsController for
// the editor lane; the controller is reachable from the shell as the property
// `vedit.diagnosticsController` (a `QObject *` holder).

#include "../integration.h"

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

void installDiagnostics(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
