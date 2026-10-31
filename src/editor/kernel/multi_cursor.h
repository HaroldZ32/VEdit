#pragma once

// Multiple cursors (Open Q3 / A1b).
//
// Verified against KTextEditor 6.24 (see evidence/edit-core-v2/ for the probe
// output): the VIEW has a native multiple-cursor API — View::setCursorPositions()
// and View::cursorPositions(), present since 5.95. The kernel therefore uses the
// native view API for the interactive cursor set, and provides a deterministic
// document-level batch insert for the acceptance check "N cursors, one edit,
// ONE undo": all inserts run inside a single EditingTransaction, bottom-up so
// earlier positions stay valid.
#include <QList>
#include <QString>

namespace KTextEditor {
class Cursor;
class Document;
class View;
} // namespace KTextEditor

namespace vedit::editor {

struct MultiCursorResult {
    bool ok = false;
    int cursors = 0;
    int inserted = 0;
    QString error;
};

// Native view API: installs `positions` as the view's cursor set and reads the
// set back (the first position becomes the primary cursor). The count is
// asserted, so a framework regression is a red check.
MultiCursorResult setViewCursors(KTextEditor::View *view,
                                 const QList<KTextEditor::Cursor> &positions);
QList<KTextEditor::Cursor> viewCursors(KTextEditor::View *view);

// Deterministic headless multi-cursor edit: inserts `text` at every position in
// one editing transaction. One documentUndo() reverts all of them.
MultiCursorResult insertAtCursors(KTextEditor::Document *document,
                                  const QList<KTextEditor::Cursor> &positions,
                                  const QString &text);

// The view's own "insert text at the current cursor(s)" path. Used by the
// selfcheck to record what the native view actually does with several cursors.
MultiCursorResult viewInsertText(KTextEditor::View *view, const QString &text);

} // namespace vedit::editor
