#include "search_jump_target.h"

#include "app_shell.h"
#include "editor_area.h"

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
#include "kernel/ktexteditor_provider.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include <QDebug>
#include <QStatusBar>
#include <QString>

namespace vedit::workspace::search {

EditorJumpTarget::EditorJumpTarget(vedit::app::ApplicationShell *shell)
    : QObject(shell)
    , shell_(shell)
{
}

EditorJumpTarget::~EditorJumpTarget() = default;

void EditorJumpTarget::jumpTo(const JumpRequest &request)
{
    lastRequest_ = request;
    lastJumpSucceeded_ = false;
    lastError_.clear();

    const auto fail = [this](const QString &message) {
        lastError_ = message;
        qWarning("search: jump failed: %s", qPrintable(message));
        if (shell_ != nullptr && shell_->statusBar() != nullptr) {
            shell_->statusBar()->showMessage(message, 5000);
        }
    };

    if (shell_ == nullptr) {
        fail(QStringLiteral("no application shell"));
        return;
    }

#if !defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
    // No edit-core lane in this build means no provider that can place a cursor
    // in the product editor; report it instead of growing a second open/aspect
    // path for the demo widget.
    fail(QStringLiteral("no edit-core provider: the search jump needs the KTextEditor editor lane"));
#else
    vedit::app::EditorPane *pane =
        shell_->editorArea() != nullptr ? shell_->editorArea()->activePane() : nullptr;
    if (pane == nullptr) {
        fail(QStringLiteral("the shell has no active editor pane"));
        return;
    }

    // ONE open seam (regression T69): EditorPane::openFile() dispatches to the
    // installed EditorProvider (KTextEditor in the product, the demo editor in a
    // provider-less build). Casting the pane widget to QPlainTextEdit here is
    // what made a result click silently do nothing in the real product after
    // the editor lane replaced the demo widget with KTextEditor::View.
    QString openError;
    if (!pane->openFile(request.filePath, &openError)) {
        fail(openError.isEmpty() ? QStringLiteral("cannot open %1").arg(request.filePath)
                                 : openError);
        return;
    }

    KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget());
    KTextEditor::Document *document = view != nullptr ? view->document() : nullptr;
    if (view == nullptr || document == nullptr) {
        fail(QStringLiteral("the active editor pane is not a KTextEditor view "
                            "(the edit-core provider is not installed)"));
        return;
    }

    const int lineCount = document->lines();
    if (lineCount <= 0) {
        fail(QStringLiteral("the opened document has no lines"));
        return;
    }
    const int line = qBound(1, request.line, lineCount);
    const int column = qBound(1, request.column, document->lineLength(line - 1) + 1);
    const KTextEditor::Cursor target(line - 1, column - 1);
    if (!view->setCursorPosition(target)) {
        fail(QStringLiteral("setCursorPosition(%1:%2) failed").arg(line).arg(column));
        return;
    }
    // KTextEditor 6.24's public View interface has no centerCursor() (that is a
    // QPlainTextEdit/Kate-private API); setScrollPosition(cursor) is the public
    // "scroll so the cursor is visible" counterpart (since 5.33).
    view->setScrollPosition(target);
    const KTextEditor::Cursor actual = view->cursorPosition();
    if (actual.line() != target.line() || actual.column() != target.column()) {
        fail(QStringLiteral("the cursor landed at %1:%2 instead of %3:%4")
                 .arg(actual.line() + 1)
                 .arg(actual.column() + 1)
                 .arg(line)
                 .arg(column));
        return;
    }

    shell_->statusBar()->showMessage(
        QStringLiteral("Opened %1 line %2").arg(request.filePath).arg(line), 5000);
    lastJumpSucceeded_ = true;
    qInfo("search: jump opened '%s' line=%d column=%d (cursor=%d:%d)",
          qPrintable(request.filePath), line, column, actual.line() + 1, actual.column() + 1);
#endif
}

vedit::app::ApplicationShell *EditorJumpTarget::shell() const
{
    return shell_;
}

JumpRequest EditorJumpTarget::lastRequest() const
{
    return lastRequest_;
}

bool EditorJumpTarget::lastJumpSucceeded() const
{
    return lastJumpSucceeded_;
}

QString EditorJumpTarget::lastError() const
{
    return lastError_;
}

} // namespace vedit::workspace::search
