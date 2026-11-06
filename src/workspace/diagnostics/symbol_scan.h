#pragma once

// Local pre-checks for the rename target (B5).
//
// The C ABI has no `prepareRename` request, so the app cannot ask the server
// "can this position be renamed?" before showing the name dialog. What it CAN
// do locally is the cheap half of that answer: the cursor must be on an
// identifier, and that identifier must not be a language keyword. This avoids
// the worst UX the ruling names - making the user type a new name and only then
// failing - without inventing a server capability.
//
// The list is used only to REFUSE, never to accept: a keyword the list misses
// still goes to the server, which stays the authority.

#include <QString>

namespace vedit::workspace::diagnostics {

/// True for a Verilog-2001 / SystemVerilog reserved word (case-sensitive, the
/// language is case-sensitive).
bool isVerilogKeyword(const QString &identifier);

/// The local answer to "can the token at this position be renamed?".
///
/// `renameTargetAt()` walks the document up to `(line, column)` far enough to
/// know whether the position is inside a `//`/`/* */` comment or a string, then
/// classifies the identifier run under the cursor:
///
///   * a number literal (`123`, `1'b0`, `32'hFF`) is refused - the run next to
///     the base marker is not an identifier;
///   * a system task/function (`$display`) is refused;
///   * a language keyword is refused;
///   * only a run starting with a letter or `_` may pass.
///
/// This is deliberately the cheap half of `prepareRename` (the C ABI exposes no
/// prepare request). It only REFUSES: anything it cannot classify still goes to
/// the server, which remains the authority.
struct RenameTarget {
    QString symbol; ///< the identifier run under the cursor (empty when none)
    bool valid = false;
    QString reason; ///< why it is not renameable (empty when `valid`)
};

/// `line` and `column` are 0-based, like the editor and the LSP.
RenameTarget renameTargetAt(const QString &text, int line, int column);

} // namespace vedit::workspace::diagnostics
