#include "goto_line.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

namespace vedit::editor {

GotoResult gotoLine(KTextEditor::View *view, int oneBasedLine)
{
    GotoResult result;
    if (view == nullptr || view->document() == nullptr) {
        result.error = QStringLiteral("goto-line: no view/document");
        return result;
    }
    KTextEditor::Document *document = view->document();
    const int lineCount = document->lines();
    if (lineCount <= 0) {
        result.error = QStringLiteral("goto-line: the document has no lines");
        return result;
    }

    int targetLine = oneBasedLine;
    if (targetLine < 1) {
        targetLine = 1;
    }
    if (targetLine > lineCount) {
        targetLine = lineCount;
    }
    const KTextEditor::Cursor target(targetLine - 1, 0);
    if (!view->setCursorPosition(target)) {
        result.error = QStringLiteral("goto-line: setCursorPosition(%1) failed").arg(targetLine);
        return result;
    }
    const KTextEditor::Cursor actual = view->cursorPosition();
    result.line = actual.line();
    if (actual.line() != target.line() || actual.column() != 0) {
        result.error = QStringLiteral("goto-line: cursor landed at %1:%2 instead of %3:0")
                           .arg(actual.line())
                           .arg(actual.column())
                           .arg(target.line());
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace vedit::editor
