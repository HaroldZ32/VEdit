#pragma once

// VHDL workspace lane (batch 20261110c, AC-N7 / AC-6.7 / AC-6.2).
//
// This card gives the lane its product-side status: `vhdl_ls` / `ghdl` / `vsg`
// are resolved through the diagnostics lane's ONE resolution order
// (bundled > local > PATH > env) and a missing tool is shown as ONE line — what
// is missing, what it disables and how to fix it. A healthy toolchain is silent,
// and a missing tool is never rendered as per-file errors.
//
// T13 (AC-6.2) adds the real session: opening a `.vhd` / `.vhdl` document starts
// vhdl_ls (its own child process, `languageId: "vhdl"`) and gives the lane
// diagnostics, hover and definition. A VHDL document never reaches the Verilog
// backends — the lane owns the child, and the Rust session's language mapping
// would send it as `verilog`.
//
// Same convention as every workspace lane: installVhdl(ApplicationShell &,
// const IntegrationOptions &). src/workspace/integration.cpp calls it behind
// VEDIT_WORKSPACE_HAVE_VHDL once this directory exists, so no other file is
// edited for the wiring and the shell never learns the lane's name.
#include "../integration.h"

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

class VhdlLspSession;

/// AC-6.7: the ONE status-bar line for the VHDL toolchain. Empty when every tool
/// is present (healthy is silent); otherwise it names the missing tools, what
/// they disable and how to supply them. Exposed so the lane's probe and a later
/// VHDL card can read the exact text without constructing a shell.
QString vhdlToolchainStatusLine();

/// The `vhdl_ls` program the lane will spawn (resolved through the diagnostics
/// lane's ONE order), or an empty string when it is not installed. Exposed so the
/// session probe starts the SAME binary the product would start.
QString vhdlLspPath();

/// The live session this lane installed on `shell` (nullptr when the lane was
/// not installed). The probe reads it to assert the product really opened a
/// session; nothing else holds a raw pointer.
VhdlLspSession *vhdlSessionForShell(vedit::app::ApplicationShell &shell);

void installVhdl(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
