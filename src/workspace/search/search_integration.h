#pragma once

// Entry point of the ws-search lane, called by src/workspace/integration.cpp
// (the captain-approved generic seam; see evidence/ws-palette/SEAM.md once it
// lands, and src/workspace/integration.h for the convention):
//
//   * CMake target: `vedit_workspace_search`
//   * entry point:  `void installSearch(ApplicationShell &, const IntegrationOptions &)`
//
// The function only registers the panel factory; it never touches the shell's
// layout, so the search dock stays "on demand" (View menu / Ctrl+Alt+S).
#include "../integration.h"

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

void installSearch(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
