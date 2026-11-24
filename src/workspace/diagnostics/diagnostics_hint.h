#pragma once

// AC-3 (batch-20261117): the Outline's Verible preprocessing-error hint as a
// PURE function.
//
// The pinned verible-verilog-ls (v0.0-4298-g085eef05) cannot expand the macros a
// design pulls in through `include` / `+define+`. When the syntax does not
// parse AND such a macro is present it reports ONE
// `preprocessing error at "<MACRO>" ... might not be defined before.`, anchored
// at the first macro it could not expand, and that row hides the real syntax
// error (for the user's 8051 project: deleting any semicolon produced the same
// row at the same macro, wherever the semicolon was removed). slang-server
// expands those macros, so switching the LSP backend shows the real diagnostic.
//
// The Outline therefore says what happened instead of letting the macro row read
// as the defect. Pure on purpose: no widget and no I/O, so the exact sentence
// and its trigger are assertable headless, and the panel renders only what this
// returns.

#include "diagnostics_types.h"

#include <QString>
#include <QVector>

namespace vedit::workspace::diagnostics {

/// The frozen English sentence (translated under the `VEdit` context), or an
/// empty string when the hint does not apply.
///
/// It applies only when the LSP backend that produced the displayed rows is
/// Verible AND at least one of those rows is Verible's preprocessing row: a
/// message carrying both `preprocessing error at` and
/// `might not be defined before`. Any other backend (slang-server parses the
/// macros) never shows it, because there the sentence would be false.
QString veriblePreprocessHint(const QString &backendName,
                              const QVector<DiagnosticEntry> &rows);

} // namespace vedit::workspace::diagnostics
