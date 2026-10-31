#include "multi_cursor.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <algorithm>

namespace vedit::editor {
namespace {

// Sort descending so an insert never shifts a position that is still to come.
QList<int> descendingOrder(const QList<KTextEditor::Cursor> &positions)
{
    QList<int> order;
    order.reserve(positions.size());
    for (int index = 0; index < positions.size(); ++index) {
        order.append(index);
    }
    std::sort(order.begin(), order.end(), [&positions](int left, int right) {
        const KTextEditor::Cursor &a = positions.at(left);
        const KTextEditor::Cursor &b = positions.at(right);
        if (a.line() != b.line()) {
            return a.line() > b.line();
        }
        return a.column() > b.column();
    });
    return order;
}

} // namespace

MultiCursorResult setViewCursors(KTextEditor::View *view,
                                 const QList<KTextEditor::Cursor> &positions)
{
    MultiCursorResult result;
    if (view == nullptr) {
        result.error = QStringLiteral("multi-cursor: no view");
        return result;
    }
    if (positions.isEmpty()) {
        result.error = QStringLiteral("multi-cursor: no positions given");
        return result;
    }
    view->setCursorPositions(positions);
    const QList<KTextEditor::Cursor> actual = view->cursorPositions();
    result.cursors = static_cast<int>(actual.size());
    if (result.cursors != positions.size()) {
        result.error = QStringLiteral("multi-cursor: asked for %1 cursors, the view reports %2")
                           .arg(positions.size())
                           .arg(result.cursors);
        return result;
    }
    result.ok = true;
    return result;
}

QList<KTextEditor::Cursor> viewCursors(KTextEditor::View *view)
{
    return view != nullptr ? view->cursorPositions() : QList<KTextEditor::Cursor>();
}

MultiCursorResult insertAtCursors(KTextEditor::Document *document,
                                  const QList<KTextEditor::Cursor> &positions,
                                  const QString &text)
{
    MultiCursorResult result;
    if (document == nullptr) {
        result.error = QStringLiteral("multi-cursor: no document");
        return result;
    }
    if (positions.isEmpty()) {
        result.error = QStringLiteral("multi-cursor: no positions given");
        return result;
    }
    const QList<int> order = descendingOrder(positions);
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        for (const int index : order) {
            if (document->insertText(positions.at(index), text)) {
                ++result.inserted;
            }
        }
    }
    result.cursors = static_cast<int>(positions.size());
    if (result.inserted != positions.size()) {
        result.error = QStringLiteral("multi-cursor: inserted at %1 of %2 cursors")
                           .arg(result.inserted)
                           .arg(positions.size());
        return result;
    }
    result.ok = true;
    return result;
}

MultiCursorResult viewInsertText(KTextEditor::View *view, const QString &text)
{
    MultiCursorResult result;
    if (view == nullptr) {
        result.error = QStringLiteral("multi-cursor: no view");
        return result;
    }
    result.cursors = static_cast<int>(view->cursorPositions().size());
    if (!view->insertText(text)) {
        result.error = QStringLiteral("multi-cursor: View::insertText() failed");
        return result;
    }
    result.inserted = 1;
    result.ok = true;
    return result;
}

} // namespace vedit::editor
