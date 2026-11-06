#include "autofix.h"

#include <KTextEditor/Document>

namespace vedit::workspace::diagnostics {

bool applyAutofixToDocument(KTextEditor::Document *document, const QString &fixedText)
{
    if (document == nullptr) {
        return false;
    }
    // ONE editing transaction = ONE undo command. `Document::setText()` is NOT
    // usable here: it is implemented as a clear() plus an insert, i.e. TWO undo
    // steps (measured: one undo leaves the old text plus an empty line, a second
    // undo empties the document). replaceText() over the whole document is the
    // single edit operation the ruling asks for.
    KTextEditor::Document::EditingTransaction transaction(document);
    const KTextEditor::Range wholeDocument(KTextEditor::Cursor(0, 0), document->documentEnd());
    // A read-only (or otherwise refusing) document must not be reported as a
    // successful fix: the user is told the truth by the caller.
    const bool replaced = document->replaceText(wholeDocument, fixedText);
    transaction.finish();
    return replaced;
}

} // namespace vedit::workspace::diagnostics
