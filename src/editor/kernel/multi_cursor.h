#pragma once

// Multiple cursors and the AC-4.4 editing actions.
//
// Verified against KTextEditor 6.24 (see evidence/edit-core-v2/ for the probe
// output): the VIEW has a native multiple-cursor API — View::setCursorPositions()
// and View::cursorPositions(), present since 5.95 — and public coordinate mapping
// (View::cursorToCoordinate() / View::coordinatesToCursor()), which the block
// selection uses so Alt+drag works on real mouse coordinates instead of a
// heuristic. The kernel therefore uses the native view API for the interactive
// cursor set, and provides a deterministic document-level batch edit for the
// acceptance check "N cursors, one edit, ONE undo": all edits run inside a single
// EditingTransaction, bottom-up so earlier positions stay valid.
//
// T14 places both controllers in THIS file because the lane's frozen write scope
// grants only multi_cursor.{h,cpp} for implementation (ktexteditor_provider.* is
// the wiring, not a home for behaviour):
//   * MultiCursorController — block selection and add/remove cursor.
//     RM-6: the PRIMARY gestures are Shift+Alt+drag (block) and Ctrl+Alt+click
//     (cursor) because KDE/GNOME grab plain Alt+drag/click for moving a window
//     before the application sees it; plain Alt+drag / Alt+click stay as the
//     SECONDARY gestures. Ctrl+click alone is "go to definition"
//     (UX batch 2, item 13 moved add-cursor off it). Simultaneous typing /
//     Backspace / Delete / Return at every cursor.
//   * EditorEnhancements    — AC-4.4: auto-indent, bracket/quote auto-pair,
//     Ctrl+/ comment toggle, Alt+↑/↓ move line, Shift+Alt+↓ duplicate line,
//     Ctrl+Shift+K delete line, case conversion, word wrap, font zoom. The
//     bracket-match highlight is the kernel's native one (the view's config key
//     `disable-bracket-match-highlight-if-inactive`, KF6 ≥ 6.23); this lane
//     verifies/enables it instead of drawing a second highlight.
#include <KTextEditor/Cursor>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QString>

class QAction;
class QKeyEvent;
class QMouseEvent;
class QWidget;

namespace KTextEditor {
class Document;
class View;
} // namespace KTextEditor

namespace vedit::editor {

// --- shared event-filter contract for the editor kernel ---------------------
//
// A KTextEditor pane's real keyboard focus is KatePart's inner widget, not the
// public KTextEditor::View. Two rules follow, and every key handler in this
// kernel must follow them:
//   1. install the filter on the widget TREE below the view (`installEditorEventFilter`
//      does it); the filter must install itself on QEvent::ChildAdded children too;
//   2. claim QEvent::ShortcutOverride only for the keys it will handle, on
//      whichever widget actually receives them.
// A filter installed only on the public View still works for keys the inner
// widget ignores (Qt propagates unhandled key presses to the parent), but it
// silently loses any key the kernel consumes — which is how a dead shortcut can
// pass a lane test that addresses the View directly.
void installEditorEventFilter(KTextEditor::View *view, QObject *filter);
bool isEditorOwnedWidget(const KTextEditor::View *view, const QObject *watched);

struct MultiCursorResult {
    bool ok = false;
    int cursors = 0;
    int inserted = 0;
    int deleted = 0;
    QString error;
};

// Native view API: installs `positions` as the view's cursor set and reads the
// set back (the first position becomes the primary cursor). The count is
// asserted, so a framework regression is a red check.
MultiCursorResult setViewCursors(KTextEditor::View *view,
                                 const QList<KTextEditor::Cursor> &positions);
QList<KTextEditor::Cursor> viewCursors(KTextEditor::View *view);

// Deterministic headless multi-cursor edit: inserts `text` at every position in
// one editing transaction. One documentUndo() reverts all of them. When
// `newPositions` is given it receives the cursor position after each insertion
// (same order as `positions`, tracked by KTextEditor moving cursors so several
// insertions on one line cannot shift each other's result).
MultiCursorResult insertAtCursors(KTextEditor::Document *document,
                                  const QList<KTextEditor::Cursor> &positions,
                                  const QString &text,
                                  QList<KTextEditor::Cursor> *newPositions = nullptr);

// The view's own "insert text at the current cursor(s)" path. Used by the
// selfcheck to record what the native view actually does with several cursors.
MultiCursorResult viewInsertText(KTextEditor::View *view, const QString &text);

// Delete one character at every position (backwards = Backspace), merged so two
// cursors can never delete the same character twice, in ONE transaction.
// `newPositions`, when given, receives the cursor position after the deletion.
MultiCursorResult deleteAtCursors(KTextEditor::Document *document,
                                  const QList<KTextEditor::Cursor> &positions, bool backwards,
                                  QList<KTextEditor::Cursor> *newPositions = nullptr);

// --- interactive multi-cursor controller ----------------------------------

class MultiCursorController final : public QObject
{
    Q_OBJECT
public:
    static MultiCursorController *install(KTextEditor::View *view);
    static MultiCursorController *instanceFor(QWidget *editorWidget);

    // The live native cursor set (what the view reports).
    QList<KTextEditor::Cursor> cursors() const;
    int cursorCount() const;
    bool blockSelectionActive() const;

    // Actions — the same code the mouse and keys run (the lane test drives them
    // directly where a key/mouse event would only re-test Qt's event routing).
    void addCursor(const KTextEditor::Cursor &position);
    void beginBlock(const KTextEditor::Cursor &anchor);
    void extendBlock(const KTextEditor::Cursor &end);
    void finishBlock();
    bool insertTextAtCursors(const QString &text);
    bool backspaceAtCursors();
    bool deleteAtCursors();
    bool newlineAtCursors();

    KTextEditor::View *view() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit MultiCursorController(KTextEditor::View *view);

    void installCursors(const QList<KTextEditor::Cursor> &positions);
    /// The event filter watches the view AND its descendants (a real click is
    /// delivered to KatePart's inner widget in CHILD-local coordinates), while
    /// the KTextEditor coordinate APIs work in VIEW coordinates. Map the point
    /// through the receiving widget before asking for a cursor.
    QPoint viewPointFor(QWidget *watched, const QMouseEvent *event) const;
    bool handleMouse(QWidget *watched, QMouseEvent *event);
    bool handleKey(QKeyEvent *event);
    bool claimsKey(const QKeyEvent *event) const;
    QString indentationForLine(int line) const;
    /// One status-bar reason per unavailable episode (UX batch 2, item 13):
    /// the user must learn why Ctrl+click did a plain click, but a stream of
    /// clicks must not flood the status bar. An episode ends when the action
    /// reports available again (any path) or the reason changes.
    void reportDefinitionUnavailable(const QString &reason);
    void watchDefinitionAvailability();
    void refreshDefinitionNotice();

    KTextEditor::View *view_ = nullptr;
    KTextEditor::Cursor blockAnchor_;
    KTextEditor::Cursor blockExtent_;
    bool blockDragging_ = false;
    bool blockActive_ = false;
    KTextEditor::Cursor altPressAnchor_;
    bool altPressActive_ = false;
    bool definitionClickActive_ = false;
    bool definitionNoticeShown_ = false;
    QString definitionNoticeReason_;
    QWidget *availabilityHost_ = nullptr;
    QAction *definitionAction_ = nullptr;
};

// --- AC-4.4 editing actions ------------------------------------------------

class EditorEnhancements final : public QObject
{
    Q_OBJECT
public:
    static EditorEnhancements *install(KTextEditor::View *view);
    static EditorEnhancements *instanceFor(QWidget *editorWidget);

    // Actions (the keys run exactly these).
    void toggleComment();
    void moveLines(int delta);
    void duplicateLines();
    void deleteLines();
    void convertCase(bool upper);
    void toggleWordWrap();
    void zoomFont(int delta);

    // Observables.
    bool wordWrapEnabled() const;
    double fontPointSize() const;

    KTextEditor::View *view() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit EditorEnhancements(KTextEditor::View *view);

    bool handleKey(QKeyEvent *event, bool overridePhase);
    bool handleReturn();
    bool handleAutoPair(QKeyEvent *event);
    bool handlePairBackspace();
    bool pairBackspaceAvailable() const;
    void clearCompetingActions();
    void setWordWrap(bool enabled);
    void applyCase(bool upper);

    KTextEditor::View *view_ = nullptr;
};

} // namespace vedit::editor
