#pragma once

// One shared resolution of the syntax-definition directory (tasks T109/T110).
//
// T84 fixed the contract for `VEDIT_SYNTAX_DIR`: it is the **data root** (the
// directory that holds a `syntax/` child with the KSyntaxHighlighting XML files),
// not the `syntax` subdirectory itself. Packaging historically pointed it at the
// subdirectory, so both spellings must be accepted. This is the same
// normalisation T84 applied inline in `ksyntax_data.cpp`; it lives in this neutral
// layer so the KDE data loader, the folding source (`KsyntaxFoldSource`) and the
// live binding (`FoldBreadcrumbBinding`) share ONE implementation and cannot drift
// apart again (T110: the old three-way duplication was the root cause of the
// `VEDIT_SYNTAX_DIR` fold bug).
#include <QString>

namespace vedit::syntax {

/// The directory that actually contains `vedit-*.xml`.
///
/// Accepts either spelling and normalises to `<root>/syntax`:
///   * `value` is a data root (it has a `syntax/` child) -> `<value>/syntax`;
///   * `value` is already a `syntax` subdirectory -> `value`;
///   * anything else (missing path, no `syntax/` child) -> empty string.
QString resolveSyntaxDefinitionDirectory(const QString &dataRootOrSyntaxDir);

/// The shipped definition file for a KTextEditor mode name:
/// `vedit-verilog.xml` for "Verilog", otherwise `vedit-systemverilog.xml`.
QString syntaxDefinitionFileName(const QString &mode);

/// `VEDIT_SYNTAX_DIR` (through resolveSyntaxDefinitionDirectory), falling back to
/// the source-checkout `resources/syntax` next to the running binary (bare build).
/// Empty when neither is available.
QString applicationSyntaxDefinitionDirectory();

} // namespace vedit::syntax
