#pragma once

// Syntax-highlighting mode selection for the kernel's documents (task T81).
//
// The user-visible defect behind T81: the kernel loads a file with
// `KTextEditor::Document::setText()` and never tells the document which syntax
// definition to use. KTextEditor only auto-detects a mode from the document URL,
// and this open path deliberately does not use KParts::openUrl() (it starts a
// KIO job that can spin headless — see document_io.h). So the document stayed in
// the "None" mode and the view had nothing to color, even though the editor's
// repository already held VEdit's definitions.
//
// This file owns the fix: map the file to a definition name and set it on the
// document. The name comes from the EDITOR's repository (the one
// KTextEditor::View renders with), preferring definitions authored by VEdit;
// the shipped `resources/syntax/vedit-*.xml` files are the source of truth for
// the extension mapping.
#include <QString>

namespace KSyntaxHighlighting {
class Definition;
}

namespace KTextEditor {
class Document;
}

namespace vedit::editor {

// The definition name VEdit ships for this file name, e.g. "Verilog" for `.v`,
// "SystemVerilog" for `.sv`; empty when the file is not a VEdit language.
// The editor repository is consulted first (a definition that already claims the
// file name wins when it is authored by VEdit); the extension table is the
// fallback used when the editor's repository resolves the file to a host
// definition (e.g. the built-in "V"/vlang definition also claims `*.v`).
QString highlightModeForFile(const QString &absolutePath);

// Resolves `mode` in the editor's repository. The returned definition is invalid
// when the editor or the definition is unavailable.
KSyntaxHighlighting::Definition editorDefinitionForMode(const QString &mode);

// Sets the document's syntax-highlighting mode for `absolutePath`.
// Returns true when a non-empty mode was set; `diagnostic` explains failures and
// notable fallbacks (e.g. "the repository resolves this to a non-VEdit
// definition"), and is empty on the plain success path.
bool applyHighlightingMode(KTextEditor::Document *document, const QString &absolutePath,
                           QString *diagnostic = nullptr);

// The observable form of "the user sees colors": the number of format ranges and
// folding regions the KSyntaxHighlighting engine applies to `text` with
// `definition`. The engine is the one KTextEditor::View uses internally.
struct HighlightStats {
    int formats = 0;
    int foldingBegins = 0;
    int foldingEnds = 0;
};

HighlightStats measureHighlighting(const KSyntaxHighlighting::Definition &definition,
                                   const QString &text);

} // namespace vedit::editor
