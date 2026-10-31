#pragma once

// App-facing adapter of the file tree panel (workspace lane, task T21).
//
// The panel itself (model / view / operations) never depends on src/app: this
// adapter is the only file that includes the application shell, and it follows
// the workspace-lane convention declared in src/workspace/integration.h:
//
//   * target `vedit_workspace_filetree`
//   * entry  `installFileTree(ApplicationShell &, const IntegrationOptions &)`
//
// When the lane is built on its own (`cmake -S src/workspace/filetree ...`) the
// app shell is not part of the build and installFileTree() is a no-op stub; the
// panel is still fully exercised by the lane's own demo and ctest suite.

#include "../integration.h"

class QWidget;

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

class FileTreeWidget;

// Registers the `file-tree` panel factory and replaces the shell's placeholder
// content when the dock was already inserted at first start. Idempotent.
void installFileTree(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

// The installed panel, or nullptr when the seam is not active (standalone build,
// or the shell never inserted the dock).
FileTreeWidget *fileTreeWidgetForShell(const vedit::app::ApplicationShell &shell);

} // namespace vedit::workspace
