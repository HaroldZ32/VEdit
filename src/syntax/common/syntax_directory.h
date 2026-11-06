#pragma once

// One shared resolution of the syntax-definition directory (tasks T109/T110).
//
// T84 fixed the contract for `VEDIT_SYNTAX_DIR`: it is the **data root** (the
// directory that holds a `syntax/` child with the KSyntaxHighlighting XML files),
// not the `syntax` subdirectory itself. Packaging historically pointed it at the
// subdirectory, so both spellings must be accepted. This is the same
// normalisation T84 applied inline in `ksyntax_data.cpp`; it lives in this neutral
// layer so every consumer shares ONE implementation and cannot drift apart again
// (T110: the old duplication was the root cause of the `VEDIT_SYNTAX_DIR` fold
// bug). T49: of the consumers once named here, only the KDE data loader
// (`ksyntax_data.cpp`) is left — the folding lane was deleted by T29, so the two
// helpers that served it (the definition-file-name mapper and the
// application-directory fallback) had no call site left and were removed.
#include <QString>

namespace vedit::syntax {

/// The directory that actually contains `vedit-*.xml`.
///
/// Accepts either spelling and normalises to `<root>/syntax`:
///   * `value` is a data root (it has a `syntax/` child) -> `<value>/syntax`;
///   * `value` is already a `syntax` subdirectory -> `value`;
///   * anything else (missing path, no `syntax/` child) -> empty string.
QString resolveSyntaxDefinitionDirectory(const QString &dataRootOrSyntaxDir);

} // namespace vedit::syntax
