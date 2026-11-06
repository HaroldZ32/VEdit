#pragma once

// Module-tree lane entry point (task T17, rebuilt for UX batch 2 item 1/2).
// src/workspace/integration.cpp calls this once per shell; a build without the
// lane simply skips it.
//
// The lane registers TWO docks: "module-tree" (the pure module hierarchy) and
// "interface-panel" (the focused module's port table). Both read the same
// asynchronous `vd_catalog_*` result and both navigate only through
// `vedit::workspace::jump::jumpTo()`.
#include <QString>

class QObject;

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

struct IntegrationOptions;

/// The slang binary the module tree hands to the catalog service.
///
/// `VEDIT_SLANG` keeps its existing precedence: it is the explicit override and
/// wins when set (the Rust resolver treats the explicit option as highest too).
/// Otherwise the path is the one the diagnostics lane resolved and published on
/// the shell as `vedit.diagnostics.toolchainSlang` — the SAME report its own
/// passes use (`ToolchainReport::slang.path`). Empty means "let the Rust service
/// resolve it", which includes the AppDir-root `bin/slang` layout.
///
/// Separate from the holder so the headless self-test can exercise it.
QString moduleTreeSlangBinary(const QObject *shell);

/// Adds the "module-tree" and "interface-panel" docks (PanelSpec + factories),
/// starts the asynchronous catalog chain and makes the panels' double-clicks go
/// through the application-wide jump contract.
void installModuleTree(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
