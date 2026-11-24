#pragma once

// Rename lane (batch 20261110c, AC-T13.1): the ENTRY of the engineering-wide
// (slang AST) rename — a menu command with its keyboard binding, the one-line
// gating rule and a visible index progress. The preview / atomic apply / cancel
// UI is the next card (rename-ui-preview) and extends this lane; the engine and
// its frozen C ABI (`vd_rename_index_start` / `_plan_start` / `_apply_start`)
// already exist in crates/vd-deeplint + crates/vd-ffi, so this card adds NO
// engine code.
//
// The command is NOT the LSP `diagnostics.rename` action: that one asks the
// language server for a file-scoped rename. This lane's command is the
// engineering-wide rename the `Project` menu entry announces, gated by the same
// inputs AC-2.10/AC-2.1 name (slang present + the current file is Verilog/SV).
//
// A workspace lane is an OPTIONAL subdirectory: src/app/CMakeLists.txt lists
// `rename` in VEDIT_WORKSPACE_LANES and src/workspace/integration.cpp calls
// installRename() behind VEDIT_WORKSPACE_HAVE_RENAME, so this directory existing
// is the whole wiring step. Neither of those files is edited by this lane.
#include "../integration.h"

#include <QString>
#include <QStringList>

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

// The inputs of the gating rule AC-T13.1 states: "enabled only when slang can
// parse and the current file is Verilog/SystemVerilog, otherwise disabled with
// a one-line reason". The rule is a PURE function of these values (no widget,
// no I/O), so the evidence log's gate matrix can assert every branch without
// opening an editor.
struct RenameEntryInput {
    QString activeFile;       // absolute path of the active tab; empty = no document
    QString projectRoot;      // the shell's workspace root; empty = no project
    QStringList projectFiles; // the project's effective (HDL) file set
    bool slangFound = false;  // the product-wide toolchain resolution found `slang`
};

struct RenameEntryGate {
    bool available = false;
    QString reason; // ONE user-visible line; empty while the command is available
};

// The pure gating rule, with the same precedence in every build.
RenameEntryGate renameEntryGate(const RenameEntryInput &input);

// Registers the command, keeps its enabled state (and one-line reason) in sync
// with the live shell, and runs the project index with visible progress. Same
// convention as every workspace lane.
void installRename(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
