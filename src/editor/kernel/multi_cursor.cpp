#include "multi_cursor.h"

#include "lsp_actions.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/MovingCursor>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QAction>
#include <QEvent>
#include <QFont>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMainWindow>
#include <QMouseEvent>
#include <QStatusBar>
#include <QStringList>

#include <algorithm>
#include <QCoreApplication>

namespace vedit::editor {
namespace {

constexpr int kIndentWidth = 4;

// Sort descending so an insert never shifts a position that is still to come.
QList<int> descendingOrder(const QList<KTextEditor::Cursor> &positions)
{
    QList<int> order;
    order.reserve(positions.size());
    for (int index = 0; index < static_cast<int>(positions.size()); ++index) {
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

QString leadingIndentation(const QString &line)
{
    qsizetype end = 0;
    while (end < line.size() &&
           (line.at(end) == QLatin1Char(' ') || line.at(end) == QLatin1Char('\t'))) {
        ++end;
    }
    return line.left(end);
}

// KTextEditor moving cursors track a position THROUGH our edits, so a
// multi-cursor insertion never has to predict how an earlier insertion moved a
// later cursor (two cursors on one line is exactly that case).
class MarkerGuard
{
public:
    explicit MarkerGuard(KTextEditor::Document *document)
        : document_(document)
    {
    }
    ~MarkerGuard()
    {
        for (KTextEditor::MovingCursor *marker : markers_) {
            delete marker;
        }
    }
    MarkerGuard(const MarkerGuard &) = delete;
    MarkerGuard &operator=(const MarkerGuard &) = delete;

    void add(const KTextEditor::Cursor &position)
    {
        markers_.append(
            document_->newMovingCursor(position, KTextEditor::MovingCursor::MoveOnInsert));
    }

    QList<KTextEditor::Cursor> positions() const
    {
        QList<KTextEditor::Cursor> result;
        result.reserve(markers_.size());
        for (const KTextEditor::MovingCursor *marker : markers_) {
            result.append(KTextEditor::Cursor(marker->line(), marker->column()));
        }
        return result;
    }

private:
    KTextEditor::Document *document_;
    QList<KTextEditor::MovingCursor *> markers_;
};

void installOnWidgetTree(QObject *root, QObject *filter)
{
    root->installEventFilter(filter);
    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        if (child->isWidgetType()) {
            installOnWidgetTree(child, filter);
        }
    }
}

bool hasSelection(KTextEditor::View *view)
{
    if (view == nullptr) {
        return false;
    }
    const KTextEditor::Range selection = view->selectionRange();
    return selection.isValid() && selection.start() != selection.end();
}

void selectedLineSpan(KTextEditor::View *view, int *first, int *last)
{
    *first = view != nullptr ? view->cursorPosition().line() : 0;
    *last = *first;
    if (view == nullptr) {
        return;
    }
    const KTextEditor::Range selection = view->selectionRange();
    if (selection.isValid() && selection.start() != selection.end()) {
        *first = selection.start().line();
        *last = selection.end().line();
    }
}

} // namespace

void installEditorEventFilter(KTextEditor::View *view, QObject *filter)
{
    if (view == nullptr || filter == nullptr) {
        return;
    }
    installOnWidgetTree(view, filter);
}

bool isEditorOwnedWidget(const KTextEditor::View *view, const QObject *watched)
{
    if (view == nullptr || watched == nullptr) {
        return false;
    }
    const auto *viewWidget = static_cast<const QWidget *>(view);
    if (watched == viewWidget) {
        return true;
    }
    const auto *widget = qobject_cast<const QWidget *>(watched);
    return widget != nullptr && viewWidget->isAncestorOf(widget);
}

MultiCursorResult setViewCursors(KTextEditor::View *view,
                                 const QList<KTextEditor::Cursor> &positions)
{
    MultiCursorResult result;
    if (view == nullptr) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: no view");
        return result;
    }
    if (positions.isEmpty()) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: no positions given");
        return result;
    }
    view->setCursorPositions(positions);
    const QList<KTextEditor::Cursor> actual = view->cursorPositions();
    result.cursors = static_cast<int>(actual.size());
    if (result.cursors != positions.size()) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: asked for %1 cursors, the view reports %2")
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
                                  const QList<KTextEditor::Cursor> &positions, const QString &text,
                                  QList<KTextEditor::Cursor> *newPositions)
{
    MultiCursorResult result;
    if (newPositions != nullptr) {
        newPositions->clear();
    }
    if (document == nullptr) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: no document");
        return result;
    }
    if (positions.isEmpty()) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: no positions given");
        return result;
    }
    const QList<int> order = descendingOrder(positions);
    MarkerGuard markers(document);
    for (const KTextEditor::Cursor &position : positions) {
        markers.add(position);
    }
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        for (const int index : order) {
            if (document->insertText(positions.at(index), text)) {
                ++result.inserted;
            }
        }
    }
    result.cursors = static_cast<int>(positions.size());
    if (newPositions != nullptr) {
        *newPositions = markers.positions();
    }
    if (result.inserted != result.cursors) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: inserted at %1 of %2 cursors")
                           .arg(result.inserted)
                           .arg(result.cursors);
        return result;
    }
    result.ok = true;
    return result;
}

MultiCursorResult deleteAtCursors(KTextEditor::Document *document,
                                  const QList<KTextEditor::Cursor> &positions, bool backwards,
                                  QList<KTextEditor::Cursor> *newPositions)
{
    MultiCursorResult result;
    if (newPositions != nullptr) {
        newPositions->clear();
    }
    if (document == nullptr) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: no document");
        return result;
    }
    if (positions.isEmpty()) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: no positions given");
        return result;
    }

    QList<KTextEditor::Cursor> unique;
    for (const KTextEditor::Cursor &position : positions) {
        if (!unique.contains(position)) {
            unique.append(position);
        }
    }
    MarkerGuard markers(document);
    for (const KTextEditor::Cursor &position : unique) {
        markers.add(position);
    }

    QList<KTextEditor::Range> ranges;
    for (const KTextEditor::Cursor &position : unique) {
        const int line = position.line();
        const int column = position.column();
        if (backwards) {
            if (column > 0) {
                ranges.append(KTextEditor::Range(KTextEditor::Cursor(line, column - 1),
                                                 KTextEditor::Cursor(line, column)));
            } else if (line > 0) {
                ranges.append(KTextEditor::Range(
                    KTextEditor::Cursor(line - 1, document->lineLength(line - 1)),
                    KTextEditor::Cursor(line, 0)));
            }
        } else {
            if (column < document->lineLength(line)) {
                ranges.append(KTextEditor::Range(KTextEditor::Cursor(line, column),
                                                 KTextEditor::Cursor(line, column + 1)));
            } else if (line + 1 < document->lines()) {
                ranges.append(KTextEditor::Range(KTextEditor::Cursor(line, column),
                                                 KTextEditor::Cursor(line + 1, 0)));
            }
        }
    }
    result.cursors = static_cast<int>(unique.size());
    if (ranges.isEmpty()) {
        if (newPositions != nullptr) {
            *newPositions = markers.positions();
        }
        result.ok = true;
        return result;
    }

    // Two cursors can describe the same character (Ctrl+click neighbours); the
    // merged range must be deleted once. Then delete bottom-up so earlier ranges
    // stay valid.
    QList<KTextEditor::Range> merged;
    for (const KTextEditor::Range &range : ranges) {
        if (!merged.contains(range)) {
            merged.append(range);
        }
    }
    std::sort(merged.begin(), merged.end(),
              [](const KTextEditor::Range &left, const KTextEditor::Range &right) {
                  if (left.start().line() != right.start().line()) {
                      return left.start().line() > right.start().line();
                  }
                  return left.start().column() > right.start().column();
              });
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        for (const KTextEditor::Range &range : merged) {
            if (document->removeText(range)) {
                ++result.deleted;
            }
        }
    }
    if (newPositions != nullptr) {
        *newPositions = markers.positions();
    }
    if (result.deleted != static_cast<int>(merged.size())) {
        result.error = QStringLiteral("multi-cursor: deleted %1 of %2 ranges")
                           .arg(result.deleted)
                           .arg(merged.size());
        return result;
    }
    result.ok = true;
    return result;
}

MultiCursorResult viewInsertText(KTextEditor::View *view, const QString &text)
{
    MultiCursorResult result;
    if (view == nullptr) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: no view");
        return result;
    }
    result.cursors = static_cast<int>(view->cursorPositions().size());
    if (!view->insertText(text)) {
        result.error = QCoreApplication::translate("VEdit", "multi-cursor: View::insertText() failed");
        return result;
    }
    result.inserted = 1;
    result.ok = true;
    return result;
}

// --- MultiCursorController ------------------------------------------------

MultiCursorController::MultiCursorController(KTextEditor::View *view)
    : QObject(view)
    , view_(view)
{
    setObjectName(QStringLiteral("vedit-multi-cursor"));
}

MultiCursorController *MultiCursorController::install(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    if (MultiCursorController *existing = instanceFor(view)) {
        return existing;
    }
    auto *controller = new MultiCursorController(view);
    installEditorEventFilter(view, controller);
    controller->watchDefinitionAvailability();
    return controller;
}

MultiCursorController *MultiCursorController::instanceFor(QWidget *editorWidget)
{
    if (editorWidget == nullptr) {
        return nullptr;
    }
    return editorWidget->findChild<MultiCursorController *>(
        QStringLiteral("vedit-multi-cursor"));
}

KTextEditor::View *MultiCursorController::view() const
{
    return view_;
}

QList<KTextEditor::Cursor> MultiCursorController::cursors() const
{
    return view_ != nullptr ? view_->cursorPositions() : QList<KTextEditor::Cursor>();
}

int MultiCursorController::cursorCount() const
{
    return static_cast<int>(cursors().size());
}

bool MultiCursorController::blockSelectionActive() const
{
    return blockDragging_ || (view_ != nullptr && view_->blockSelection());
}

void MultiCursorController::installCursors(const QList<KTextEditor::Cursor> &positions)
{
    if (view_ == nullptr || positions.isEmpty()) {
        return;
    }
    view_->setCursorPositions(positions);
}

void MultiCursorController::beginBlock(const KTextEditor::Cursor &anchor)
{
    if (view_ == nullptr || !anchor.isValid()) {
        return;
    }
    blockAnchor_ = anchor;
    blockExtent_ = anchor;
    blockDragging_ = true;
    view_->setBlockSelection(true);
    view_->setCursorPosition(anchor);
}

void MultiCursorController::extendBlock(const KTextEditor::Cursor &end)
{
    if (view_ == nullptr || !blockDragging_ || !end.isValid()) {
        return;
    }
    blockExtent_ = end;
    // With block selection enabled the kernel renders this range as a rectangle.
    view_->setSelection(KTextEditor::Range(blockAnchor_, end));
}

void MultiCursorController::finishBlock()
{
    if (view_ == nullptr || !blockDragging_) {
        return;
    }
    blockDragging_ = false;
    const int firstLine = std::min(blockAnchor_.line(), blockExtent_.line());
    const int lastLine = std::max(blockAnchor_.line(), blockExtent_.line());
    const int column = std::min(blockAnchor_.column(), blockExtent_.column());
    QList<KTextEditor::Cursor> positions;
    for (int line = firstLine; line <= lastLine; ++line) {
        positions.append(
            KTextEditor::Cursor(line, std::min(column, view_->document()->lineLength(line))));
    }
    view_->setBlockSelection(false);
    view_->removeSelection();
    installCursors(positions);
}

void MultiCursorController::addCursor(const KTextEditor::Cursor &position)
{
    if (view_ == nullptr || !position.isValid()) {
        return;
    }
    QList<KTextEditor::Cursor> positions = view_->cursorPositions();
    bool removed = false;
    for (int index = 0; index < static_cast<int>(positions.size()); ++index) {
        if (positions.at(index) == position) {
            positions.removeAt(index);
            removed = true;
            break;
        }
    }
    if (!removed) {
        positions.append(position);
    }
    if (positions.isEmpty()) {
        positions.append(position); // Alt+click on the only cursor keeps it
    }
    installCursors(positions);
}

bool MultiCursorController::insertTextAtCursors(const QString &text)
{
    if (view_ == nullptr || view_->document() == nullptr || text.isEmpty()) {
        return false;
    }
    QList<KTextEditor::Cursor> updated;
    const MultiCursorResult result =
        insertAtCursors(view_->document(), view_->cursorPositions(), text, &updated);
    if (!result.ok) {
        return false;
    }
    installCursors(updated);
    return true;
}

bool MultiCursorController::backspaceAtCursors()
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return false;
    }
    QList<KTextEditor::Cursor> updated;
    const MultiCursorResult result =
        vedit::editor::deleteAtCursors(view_->document(), view_->cursorPositions(), true,
                                     &updated);
    if (!result.ok) {
        return false;
    }
    installCursors(updated);
    return true;
}

bool MultiCursorController::deleteAtCursors()
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return false;
    }
    QList<KTextEditor::Cursor> updated;
    const MultiCursorResult result =
        vedit::editor::deleteAtCursors(view_->document(), view_->cursorPositions(), false,
                                     &updated);
    if (!result.ok) {
        return false;
    }
    installCursors(updated);
    return true;
}

bool MultiCursorController::newlineAtCursors()
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return false;
    }
    KTextEditor::Document *document = view_->document();
    const QList<KTextEditor::Cursor> positions = view_->cursorPositions();
    if (positions.isEmpty()) {
        return false;
    }
    const QList<int> order = descendingOrder(positions);
    MarkerGuard markers(document);
    for (const KTextEditor::Cursor &position : positions) {
        markers.add(position);
    }
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        for (const int index : order) {
            const KTextEditor::Cursor position = positions.at(index);
            document->insertText(position, QStringLiteral("\n") +
                                               indentationForLine(position.line()));
        }
    }
    installCursors(markers.positions());
    return true;
}

QString MultiCursorController::indentationForLine(int line) const
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return QString();
    }
    return leadingIndentation(view_->document()->line(line));
}

bool MultiCursorController::handleKey(QKeyEvent *event)
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return false;
    }
    if (view_->cursorPositions().size() <= 1) {
        return false; // single cursor: the editor's own handling is correct
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const int key = event->key();
    if (key == Qt::Key_Backspace && modifiers == Qt::NoModifier) {
        backspaceAtCursors();
        return true;
    }
    if (key == Qt::Key_Delete && modifiers == Qt::NoModifier) {
        deleteAtCursors();
        return true;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == Qt::NoModifier) {
        newlineAtCursors();
        return true;
    }
    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint() &&
        !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        insertTextAtCursors(text);
        return true;
    }
    return false;
}

QPoint MultiCursorController::viewPointFor(QWidget *watched, const QMouseEvent *event) const
{
    const QPoint local = event->position().toPoint();
    if (view_ == nullptr || watched == nullptr || watched == static_cast<QWidget *>(view_)) {
        return local;
    }
    // The filter is installed on the view's descendants too, and that is where
    // a real click lands (KatePart's inner widget): QMouseEvent::position() is
    // then in the RECEIVING widget's coordinates, not the view's.
    return watched->mapTo(view_, local);
}

bool MultiCursorController::handleMouse(QWidget *watched, QMouseEvent *event)
{
    if (view_ == nullptr) {
        return false;
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        if (event->button() != Qt::LeftButton) {
            return false;
        }
        // A fresh press supersedes a release that never arrived: Ctrl+click can
        // navigate to another view synchronously, so the matching release may
        // be delivered elsewhere.
        definitionClickActive_ = false;
        altPressActive_ = false;
        const bool alt = event->modifiers().testFlag(Qt::AltModifier);
        const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
        const KTextEditor::Cursor clicked =
            view_->coordinatesToCursor(viewPointFor(watched, event));
        if (alt) {
            if (!clicked.isValid()) {
                return false;
            }
            // UX batch 2 (item 13): Alt+CLICK adds a cursor (the gesture that
            // used to live on Ctrl+click); Alt+DRAG is still the block
            // selection, so the block starts lazily on the first move — a click
            // without a drag must not become a one-line block that replaces the
            // cursor set.
            altPressAnchor_ = clicked;
            altPressActive_ = true;
            return true;
        }
        if (ctrl) {
            if (!clicked.isValid()) {
                return false;
            }
            // UX batch 2 (item 13): Ctrl+click is "go to definition". The seam
            // below is the SAME one F12 / the Project menu use (the LSP action
            // channel's shell action), so there is no second request path here.
            //
            // Re-arm the one-time reason here as well: availability can be
            // restored by F12 / a backend switch without a successful Ctrl+click,
            // and the host-side watch below only sees the transitions it was
            // installed in time for.
            watchDefinitionAvailability();
            refreshDefinitionNotice();
            const ActionDispatch dispatch = dispatchDefinitionAt(view_, clicked);
            if (!dispatch.dispatched) {
                // The backend cannot do definitions (or nothing is wired):
                // behave like a plain click — do NOT consume the press — and
                // say why once.
                reportDefinitionUnavailable(dispatch.reason);
                return false;
            }
            definitionNoticeShown_ = false; // a working backend may fail later
            definitionNoticeReason_.clear();
            definitionClickActive_ = true;
            return true;
        }
        return false;
    }
    case QEvent::MouseMove: {
        if (definitionClickActive_) {
            // Ctrl+click is a click, not a drag-select: the press was consumed,
            // so the editor must not start a selection from the small moves
            // between the press and its release.
            return true;
        }
        if (altPressActive_) {
            const KTextEditor::Cursor extent =
                view_->coordinatesToCursor(viewPointFor(watched, event));
            if (!extent.isValid()) {
                return false;
            }
            if (!blockDragging_) {
                if (extent == altPressAnchor_) {
                    return false; // still a click, not a drag
                }
                beginBlock(altPressAnchor_);
            }
            extendBlock(extent);
            return true;
        }
        if (!blockDragging_) {
            return false;
        }
        const KTextEditor::Cursor extent =
            view_->coordinatesToCursor(viewPointFor(watched, event));
        if (!extent.isValid()) {
            return false;
        }
        extendBlock(extent);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (event->button() != Qt::LeftButton) {
            return false;
        }
        if (definitionClickActive_) {
            // The press was consumed (the cursor was placed and the definition
            // dispatch ran); a release without a matching press must not reach
            // the kernel, or it can reset the caret to the clicked position.
            definitionClickActive_ = false;
            return true;
        }
        if (altPressActive_) {
            altPressActive_ = false;
            if (blockDragging_) {
                const KTextEditor::Cursor extent =
                    view_->coordinatesToCursor(viewPointFor(watched, event));
                if (extent.isValid()) {
                    extendBlock(extent);
                }
                finishBlock();
            } else {
                // Alt+click (no drag): add the cursor, or remove it when the
                // click lands exactly on one that is already there.
                addCursor(altPressAnchor_);
            }
            return true;
        }
        if (!blockDragging_) {
            return false;
        }
        const KTextEditor::Cursor extent =
            view_->coordinatesToCursor(viewPointFor(watched, event));
        if (extent.isValid()) {
            extendBlock(extent);
        }
        finishBlock();
        return true;
    }
    default:
        break;
    }
    return false;
}

void MultiCursorController::watchDefinitionAvailability()
{
    if (view_ == nullptr) {
        return;
    }
    QWidget *host = view_->window();
    if (host != nullptr && host != availabilityHost_) {
        if (availabilityHost_ != nullptr) {
            availabilityHost_->removeEventFilter(this);
        }
        host->installEventFilter(this);
        availabilityHost_ = host;
        QObject::connect(host, &QObject::destroyed, this,
                         [this]() { availabilityHost_ = nullptr; });
    }
    QAction *action = shellActionFor(host, EditorAction::Definition);
    if (action == nullptr || action == definitionAction_) {
        return;
    }
    definitionAction_ = action;
    QObject::connect(action, &QObject::destroyed, this,
                     [this]() { definitionAction_ = nullptr; });
    QObject::connect(action, &QAction::changed, this,
                     [this]() { refreshDefinitionNotice(); });
}

void MultiCursorController::refreshDefinitionNotice()
{
    LspActionChannel *channel = LspActionChannel::instanceFor(view_);
    if (channel != nullptr
        && channel->availability(EditorAction::Definition).enabled) {
        definitionNoticeShown_ = false;
        definitionNoticeReason_.clear();
    }
}

void MultiCursorController::reportDefinitionUnavailable(const QString &reason)
{
    // One message per unavailable episode; a CHANGED reason is new information
    // and is surfaced even while the latch is set.
    if (definitionNoticeShown_ && definitionNoticeReason_ == reason) {
        return;
    }
    definitionNoticeShown_ = true;
    definitionNoticeReason_ = reason;
    const QString message =
        QCoreApplication::translate("VEdit",
                                    "Go to Definition is unavailable here: %1 - Ctrl+click now "
                                    "behaves like a plain click")
            .arg(reason);
    if (QMainWindow *window = qobject_cast<QMainWindow *>(view_->window());
        window != nullptr && window->statusBar() != nullptr) {
        window->statusBar()->showMessage(message, 8000);
    }
    logEditorActionLine(QStringLiteral("DEFINITION-CLICK unavailable=1 reason='%1'").arg(reason));
}

bool MultiCursorController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded && isEditorOwnedWidget(view_, watched)) {
        auto *child = static_cast<QChildEvent *>(event)->child();
        if (child != nullptr && child->isWidgetType()) {
            installOnWidgetTree(child, this);
        }
    }
    // The shell publishes the capability gate as a dynamic property and then
    // enables/disables the QAction in a separate step; watch the host itself so
    // "availability restored" is seen whichever order the shell uses.
    if (watched == availabilityHost_ && event->type() == QEvent::DynamicPropertyChange) {
        refreshDefinitionNotice();
        return QObject::eventFilter(watched, event);
    }
    if (!isEditorOwnedWidget(view_, watched)) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        // KatePart gives Backspace/Delete/Return action shortcuts; without
        // claiming the override they are consumed before any KeyPress handler
        // runs. Claim exactly the keys this controller will handle.
        if (!claimsKey(static_cast<QKeyEvent *>(event))) {
            return QObject::eventFilter(watched, event);
        }
        event->accept();
        return true;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
        if (auto *widget = qobject_cast<QWidget *>(watched);
            widget != nullptr && handleMouse(widget, static_cast<QMouseEvent *>(event))) {
            return true;
        }
        break;
    case QEvent::KeyPress:
        if (handleKey(static_cast<QKeyEvent *>(event))) {
            return true;
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

bool MultiCursorController::claimsKey(const QKeyEvent *event) const
{
    if (view_ == nullptr || view_->cursorPositions().size() <= 1) {
        return false;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const int key = event->key();
    if (modifiers == Qt::NoModifier &&
        (key == Qt::Key_Backspace || key == Qt::Key_Delete || key == Qt::Key_Return ||
         key == Qt::Key_Enter)) {
        return true;
    }
    const QString text = event->text();
    return !text.isEmpty() && text.at(0).isPrint() &&
           !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
}

// --- EditorEnhancements (AC-4.4) ------------------------------------------

namespace {

enum class EditCommand {
    None,
    ToggleComment,
    MoveLineUp,
    MoveLineDown,
    DuplicateLine,
    DeleteLine,
    UpperCase,
    LowerCase,
    ZoomIn,
    ZoomOut,
    ToggleWrap,
};

EditCommand commandForKey(const QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers =
        event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier |
                              Qt::MetaModifier);
    const int key = event->key();
    if (modifiers == Qt::ControlModifier && key == Qt::Key_Slash) {
        return EditCommand::ToggleComment;
    }
    if (modifiers == Qt::AltModifier && key == Qt::Key_Up) {
        return EditCommand::MoveLineUp;
    }
    if (modifiers == Qt::AltModifier && key == Qt::Key_Down) {
        return EditCommand::MoveLineDown;
    }
    if (modifiers == (Qt::ShiftModifier | Qt::AltModifier) && key == Qt::Key_Down) {
        return EditCommand::DuplicateLine;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_K) {
        return EditCommand::DeleteLine;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_U) {
        return EditCommand::UpperCase;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_L) {
        return EditCommand::LowerCase;
    }
    if (modifiers == Qt::ControlModifier && (key == Qt::Key_Equal || key == Qt::Key_Plus)) {
        return EditCommand::ZoomIn;
    }
    if (modifiers == Qt::ControlModifier && (key == Qt::Key_Minus || key == Qt::Key_Underscore)) {
        return EditCommand::ZoomOut;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_W) {
        return EditCommand::ToggleWrap;
    }
    return EditCommand::None;
}

bool isPairChar(QChar character)
{
    return character == QLatin1Char('(') || character == QLatin1Char(')') ||
           character == QLatin1Char('[') || character == QLatin1Char(']') ||
           character == QLatin1Char('{') || character == QLatin1Char('}') ||
           character == QLatin1Char('"') || character == QLatin1Char('\'');
}

QChar closingPartner(QChar character)
{
    if (character == QLatin1Char('(')) {
        return QLatin1Char(')');
    }
    if (character == QLatin1Char('[')) {
        return QLatin1Char(']');
    }
    if (character == QLatin1Char('{')) {
        return QLatin1Char('}');
    }
    if (character == QLatin1Char('"') || character == QLatin1Char('\'')) {
        return character;
    }
    return QChar();
}

} // namespace

EditorEnhancements::EditorEnhancements(KTextEditor::View *view)
    : QObject(view)
    , view_(view)
{
    setObjectName(QStringLiteral("vedit-editor-enhancements"));
}

EditorEnhancements *EditorEnhancements::install(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    if (EditorEnhancements *existing = instanceFor(view)) {
        return existing;
    }
    auto *enhancements = new EditorEnhancements(view);
    enhancements->clearCompetingActions();
    installEditorEventFilter(view, enhancements);
    // The bracket-match highlight is KatePart's own renderer and is always on:
    // KF6 6.24.0 documents a `disable-bracket-match-highlight-if-inactive` config
    // key in view.h but the View interface does not accept it (configKeys() does
    // not list it, and a set/get round-trip stays invalid), so there is nothing
    // to enable here — and no second renderer is added. The lane test proves the
    // native highlight with a pixel differential on a real view.
    return enhancements;
}

EditorEnhancements *EditorEnhancements::instanceFor(QWidget *editorWidget)
{
    if (editorWidget == nullptr) {
        return nullptr;
    }
    return editorWidget->findChild<EditorEnhancements *>(
        QStringLiteral("vedit-editor-enhancements"));
}

void EditorEnhancements::clearCompetingActions()
{
    if (view_ == nullptr) {
        return;
    }
    // The AC-4.4 sequences are owned by this filter; a kernel QAction that also
    // advertises one would either win on context priority or make the key
    // "ambiguous", so the reachable ones lose their sequence here (the same
    // single-owner rule the find/replace bar uses).
    const QList<QKeySequence> owned{
        QKeySequence(QStringLiteral("Ctrl+/")),      QKeySequence(QStringLiteral("Alt+Up")),
        QKeySequence(QStringLiteral("Alt+Down")),    QKeySequence(QStringLiteral("Shift+Alt+Down")),
        QKeySequence(QStringLiteral("Ctrl+Shift+K")), QKeySequence(QStringLiteral("Ctrl+Shift+U")),
        QKeySequence(QStringLiteral("Ctrl+Shift+L")), QKeySequence(QStringLiteral("Ctrl+=")),
        QKeySequence(QStringLiteral("Ctrl+-")),      QKeySequence(QStringLiteral("Ctrl+Shift+W")),
    };
    const QList<QAction *> actions = view_->findChildren<QAction *>();
    for (QAction *action : actions) {
        if (action == nullptr || action->shortcuts().isEmpty()) {
            continue;
        }
        for (const QKeySequence &sequence : action->shortcuts()) {
            if (owned.contains(sequence)) {
                action->setShortcuts(QList<QKeySequence>());
                break;
            }
        }
    }
}

KTextEditor::View *EditorEnhancements::view() const
{
    return view_;
}

bool EditorEnhancements::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded && isEditorOwnedWidget(view_, watched)) {
        auto *child = static_cast<QChildEvent *>(event)->child();
        if (child != nullptr && child->isWidgetType()) {
            installOnWidgetTree(child, this);
        }
    }
    if (!isEditorOwnedWidget(view_, watched)) {
        return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ShortcutOverride) {
        return handleKey(static_cast<QKeyEvent *>(event), true);
    }
    if (event->type() == QEvent::KeyPress) {
        return handleKey(static_cast<QKeyEvent *>(event), false);
    }
    return QObject::eventFilter(watched, event);
}

bool EditorEnhancements::handleKey(QKeyEvent *event, bool overridePhase)
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return false;
    }
    const EditCommand command = commandForKey(event);
    if (command != EditCommand::None) {
        if (overridePhase) {
            event->accept();
            return true;
        }
        switch (command) {
        case EditCommand::ToggleComment:
            toggleComment();
            break;
        case EditCommand::MoveLineUp:
            moveLines(-1);
            break;
        case EditCommand::MoveLineDown:
            moveLines(1);
            break;
        case EditCommand::DuplicateLine:
            duplicateLines();
            break;
        case EditCommand::DeleteLine:
            deleteLines();
            break;
        case EditCommand::UpperCase:
            convertCase(true);
            break;
        case EditCommand::LowerCase:
            convertCase(false);
            break;
        case EditCommand::ZoomIn:
            zoomFont(1);
            break;
        case EditCommand::ZoomOut:
            zoomFont(-1);
            break;
        case EditCommand::ToggleWrap:
            toggleWordWrap();
            break;
        case EditCommand::None:
            break;
        }
        event->accept();
        return true;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const int key = event->key();
    if (overridePhase) {
        // Claim what this filter will handle, so a KatePart action shortcut does
        // not consume the key before the KeyPress arrives (Backspace and Return
        // are action-backed). Everything else stays the kernel's.
        const bool returnKey =
            (key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == Qt::NoModifier;
        if (returnKey || (key == Qt::Key_Backspace && modifiers == Qt::NoModifier &&
                          pairBackspaceAvailable())) {
            event->accept();
            return true;
        }
        return false;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == Qt::NoModifier) {
        return handleReturn();
    }
    if (key == Qt::Key_Backspace && modifiers == Qt::NoModifier) {
        return handlePairBackspace();
    }
    const QString text = event->text();
    if (!text.isEmpty() && text.size() == 1 && text.at(0).isPrint() &&
        !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        isPairChar(text.at(0))) {
        return handleAutoPair(event);
    }
    return false;
}

bool EditorEnhancements::handleReturn()
{
    KTextEditor::Document *document = view_->document();
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    const QString line = document->line(cursor.line());
    QString indent = leadingIndentation(line);
    QString beforeCursor = line.left(cursor.column());
    while (!beforeCursor.isEmpty() && beforeCursor.back().isSpace()) {
        beforeCursor.chop(1);
    }
    if (beforeCursor.endsWith(QLatin1Char('{'))) {
        indent += QString(kIndentWidth, QLatin1Char(' '));
    }
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        document->insertText(cursor, QStringLiteral("\n") + indent);
    }
    view_->setCursorPosition(
        KTextEditor::Cursor(cursor.line() + 1, static_cast<int>(indent.size())));
    return true;
}

bool EditorEnhancements::handleAutoPair(QKeyEvent *event)
{
    const QString text = event->text();
    if (text.size() != 1) {
        return false;
    }
    const QChar character = text.at(0);
    KTextEditor::Document *document = view_->document();
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    const QString line = document->line(cursor.line());
    const QChar next = cursor.column() < line.size() ? line.at(cursor.column()) : QChar();

    // Typing a closer when the same closer is already there only steps over it.
    const bool isCloser = character == QLatin1Char(')') || character == QLatin1Char(']') ||
                          character == QLatin1Char('}') || character == QLatin1Char('"') ||
                          character == QLatin1Char('\'');
    if (isCloser && next == character) {
        view_->setCursorPosition(
            KTextEditor::Cursor(cursor.line(), cursor.column() + 1));
        return true;
    }
    if (character == QLatin1Char('"') || character == QLatin1Char('\'')) {
        // A quote before a word is more likely an apostrophe than a pair start.
        if (!next.isNull() && !next.isSpace() && next != character) {
            return false;
        }
    }
    const QChar partner = closingPartner(character);
    if (partner.isNull()) {
        return false;
    }
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        document->insertText(cursor, QString(character) + partner);
    }
    view_->setCursorPosition(KTextEditor::Cursor(cursor.line(), cursor.column() + 1));
    return true;
}

bool EditorEnhancements::pairBackspaceAvailable() const
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return false;
    }
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    if (cursor.column() == 0) {
        return false;
    }
    const QString line = view_->document()->line(cursor.line());
    if (cursor.column() >= line.size()) {
        return false;
    }
    const QChar partner = closingPartner(line.at(cursor.column() - 1));
    return !partner.isNull() && partner == line.at(cursor.column());
}

bool EditorEnhancements::handlePairBackspace()
{
    if (!pairBackspaceAvailable()) {
        return false;
    }
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    KTextEditor::Document *document = view_->document();
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        document->removeText(KTextEditor::Range(KTextEditor::Cursor(cursor.line(),
                                                                    cursor.column() - 1),
                                                KTextEditor::Cursor(cursor.line(),
                                                                    cursor.column() + 1)));
    }
    view_->setCursorPosition(KTextEditor::Cursor(cursor.line(), cursor.column() - 1));
    return true;
}

void EditorEnhancements::toggleComment()
{
    KTextEditor::Document *document = view_->document();
    int first = 0;
    int last = 0;
    selectedLineSpan(view_, &first, &last);

    bool allCommented = true;
    for (int line = first; line <= last; ++line) {
        const QString text = document->line(line);
        qsizetype start = 0;
        while (start < text.size() && text.at(start).isSpace()) {
            ++start;
        }
        if (!text.mid(start).startsWith(QStringLiteral("//"))) {
            allCommented = false;
            break;
        }
    }
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        for (int line = last; line >= first; --line) {
            const QString text = document->line(line);
            qsizetype start = 0;
            while (start < text.size() && text.at(start).isSpace()) {
                ++start;
            }
            if (allCommented) {
                int removeCount = 2;
                if (start + 2 < text.size() && text.at(start + 2) == QLatin1Char(' ')) {
                    removeCount = 3;
                }
                document->removeText(
                    KTextEditor::Range(KTextEditor::Cursor(line, static_cast<int>(start)),
                                       KTextEditor::Cursor(line,
                                                           static_cast<int>(start) + removeCount)));
            } else {
                document->insertText(
                    KTextEditor::Cursor(line, static_cast<int>(start)),
                    QStringLiteral("// "));
            }
        }
    }
}

void EditorEnhancements::moveLines(int delta)
{
    KTextEditor::Document *document = view_->document();
    if (document == nullptr || delta == 0) {
        return;
    }
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    int first = 0;
    int last = 0;
    selectedLineSpan(view_, &first, &last);
    if (delta < 0 && first == 0) {
        return;
    }
    if (delta > 0 && last + 1 >= document->lines()) {
        return;
    }

    QString block;
    for (int line = first; line <= last; ++line) {
        if (line > first) {
            block += QLatin1Char('\n');
        }
        block += document->line(line);
    }
    KTextEditor::Range range;
    QString replacement;
    if (delta < 0) {
        const QString above = document->line(first - 1);
        range = KTextEditor::Range(KTextEditor::Cursor(first - 1, 0),
                                   KTextEditor::Cursor(last, document->lineLength(last)));
        replacement = block + QLatin1Char('\n') + above;
    } else {
        const QString below = document->line(last + 1);
        range = KTextEditor::Range(KTextEditor::Cursor(first, 0),
                                   KTextEditor::Cursor(last + 1,
                                                       document->lineLength(last + 1)));
        replacement = below + QLatin1Char('\n') + block;
    }
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        document->replaceText(range, replacement);
    }
    KTextEditor::Cursor moved = cursor;
    moved.setLine(cursor.line() + delta);
    view_->setCursorPosition(moved);
}

void EditorEnhancements::duplicateLines()
{
    KTextEditor::Document *document = view_->document();
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    int first = 0;
    int last = 0;
    selectedLineSpan(view_, &first, &last);
    QString block;
    for (int line = first; line <= last; ++line) {
        if (line > first) {
            block += QLatin1Char('\n');
        }
        block += document->line(line);
    }
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        if (last + 1 < document->lines()) {
            document->insertText(KTextEditor::Cursor(last + 1, 0),
                                 block + QLatin1Char('\n'));
        } else {
            document->insertText(KTextEditor::Cursor(last, document->lineLength(last)),
                                 QLatin1Char('\n') + block);
        }
    }
    view_->setCursorPosition(cursor);
}

void EditorEnhancements::deleteLines()
{
    KTextEditor::Document *document = view_->document();
    if (document->lines() <= 1) {
        KTextEditor::Document::EditingTransaction transaction(document);
        document->replaceText(KTextEditor::Range(KTextEditor::Cursor(0, 0),
                                                 KTextEditor::Cursor(0,
                                                                     document->lineLength(0))),
                              QString());
        view_->setCursorPosition(KTextEditor::Cursor(0, 0));
        return;
    }
    int first = 0;
    int last = 0;
    selectedLineSpan(view_, &first, &last);
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        if (last + 1 < document->lines()) {
            document->removeText(KTextEditor::Range(KTextEditor::Cursor(first, 0),
                                                    KTextEditor::Cursor(last + 1, 0)));
        } else {
            document->removeText(
                KTextEditor::Range(KTextEditor::Cursor(first - 1,
                                                       document->lineLength(first - 1)),
                                   KTextEditor::Cursor(last, document->lineLength(last))));
        }
    }
    const int target = std::min(first, document->lines() - 1);
    view_->setCursorPosition(KTextEditor::Cursor(std::max(0, target), 0));
}

void EditorEnhancements::convertCase(bool upper)
{
    applyCase(upper);
}

void EditorEnhancements::applyCase(bool upper)
{
    KTextEditor::Document *document = view_->document();
    if (hasSelection(view_)) {
        const KTextEditor::Range selection = view_->selectionRange();
        const QString text = document->text(selection);
        const QString converted = upper ? text.toUpper() : text.toLower();
        if (converted != text) {
            KTextEditor::Document::EditingTransaction transaction(document);
            document->replaceText(selection, converted);
        }
        return;
    }
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    const QString line = document->line(cursor.line());
    const QString converted = upper ? line.toUpper() : line.toLower();
    if (converted != line) {
        KTextEditor::Document::EditingTransaction transaction(document);
        document->replaceText(
            KTextEditor::Range(KTextEditor::Cursor(cursor.line(), 0),
                               KTextEditor::Cursor(cursor.line(),
                                                   static_cast<int>(line.size()))),
            converted);
    }
}

void EditorEnhancements::toggleWordWrap()
{
    setWordWrap(!wordWrapEnabled());
}

void EditorEnhancements::setWordWrap(bool enabled)
{
    if (view_ != nullptr) {
        view_->setConfigValue(QStringLiteral("dynamic-word-wrap"), enabled);
    }
}

bool EditorEnhancements::wordWrapEnabled() const
{
    return view_ != nullptr && view_->configValue(QStringLiteral("dynamic-word-wrap")).toBool();
}

void EditorEnhancements::zoomFont(int delta)
{
    if (view_ == nullptr) {
        return;
    }
    QFont font = view_->configValue(QStringLiteral("font")).value<QFont>();
    if (font.pointSizeF() <= 0) {
        font = view_->font();
    }
    double size = font.pointSizeF() > 0 ? font.pointSizeF() : 10.0;
    size = std::clamp(size + static_cast<double>(delta), 4.0, 48.0);
    font.setPointSizeF(size);
    view_->setConfigValue(QStringLiteral("font"), font);
}

double EditorEnhancements::fontPointSize() const
{
    if (view_ == nullptr) {
        return 0.0;
    }
    const QFont font = view_->configValue(QStringLiteral("font")).value<QFont>();
    if (font.pointSizeF() > 0) {
        return font.pointSizeF();
    }
    const QFont viewFont = view_->font();
    return viewFont.pointSizeF() > 0 ? viewFont.pointSizeF() : 10.0;
}

} // namespace vedit::editor
