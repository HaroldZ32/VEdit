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

// The lane's shell-facing open seam: loads `path` into the shell's active editor
// pane, records it as the widget property `vedit.openFilePath`, and reports it in
// the status bar. Until the edit-core lane provides its provider, the pane holds
// the demo QPlainTextEdit, so this is the same path the command palette proves
// in PaletteIntegration::openInEditor; edit-core replaces the widget behind the
// EditorProvider seam without changing this call site. Returns false (with
// `error`, when given) for a missing/special file, an unreadable file, a binary
// file, or a pane with no text editor; the failure is also shown in the status
// bar. This is what a double click on a file row ends up calling.
bool openFileInEditor(vedit::app::ApplicationShell &shell, const QString &path,
                      QString *error = nullptr);

// The installed panel, or nullptr when the seam is not active (standalone build,
// or the shell never inserted the dock).
FileTreeWidget *fileTreeWidgetForShell(const vedit::app::ApplicationShell &shell);

} // namespace vedit::workspace
