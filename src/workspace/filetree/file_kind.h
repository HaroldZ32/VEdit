#pragma once

// File kind classification for the workspace file tree (lane: workspace, T21).
//
// This mapping is deliberately independent of KSyntaxHighlighting. On the
// pinned image, KSyntaxHighlighting::Repository::definitionForFileName("x.v")
// resolves to the "V" (vlang) definition because vlang also claims "*.v"
// (docs/BUILD.md §12.6, evidence/m0-build/12-ksyntax-definitions-probe.txt).
// A file *tree* must not inherit that ambiguity: for a Verilog project "*.v"
// is Verilog. The highlighter lane owns its own name-based mapping; the tree
// only ever uses the table below.

#include <QString>

namespace vedit::workspace {

enum class FileKind {
    Directory,
    Verilog,       // *.v, *.vh
    SystemVerilog, // *.sv, *.svh
    FileList,      // *.f, *.filelist  (Verilog file lists, requirement C9)
    Other,
};

// `isDirectory` is passed in because the tree already knows it from the
// directory scan; re-stat'ing through QFileInfo would silently follow symlinks.
FileKind fileKindForPath(const QString &path, bool isDirectory);

// Stable machine-readable id ("verilog", "systemverilog", ...) for evidence
// logs and tests; never translated.
QString fileKindId(FileKind kind);

// Human-readable label used by the panel status line and tooltips.
QString fileKindLabel(FileKind kind);

} // namespace vedit::workspace
