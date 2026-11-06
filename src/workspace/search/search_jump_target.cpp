#include "search_jump_target.h"

#include "app_shell.h"
#include "editor_area.h"

#include "jump/jump_registry.h"

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
#include "kernel/ktexteditor_provider.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include <QDebug>
#include <QStatusBar>
#include <QString>
#include <QCoreApplication>

namespace vedit::workspace::search {

EditorJumpTarget::EditorJumpTarget(vedit::app::ApplicationShell *shell)
    : QObject(shell)
    , shell_(shell)
{
}

EditorJumpTarget::~EditorJumpTarget()
{
    // The registry never owns the target; drop this one before it dies (the
    // registry's QObject weak guard would also do it, this is the explicit half).
    vedit::workspace::jump::clearTargetIf(this);
}

bool EditorJumpTarget::jumpTo(const QString &filePath, int line, int column)
{
    lastFilePath_ = filePath;
    lastLine_ = line;
    lastColumn_ = column;
    lastJumpSucceeded_ = false;
    lastError_.clear();

    const auto fail = [this](const QString &message) {
        lastError_ = message;
        qWarning("search: jump failed: %s", qPrintable(message));
        if (shell_ != nullptr && shell_->statusBar() != nullptr) {
            shell_->statusBar()->showMessage(message, 5000);
        }
        return false;
    };

    if (shell_ == nullptr) {
        return fail(QStringLiteral("no application shell"));
    }
    if (filePath.isEmpty() || line < 1 || column < 1) {
        return fail(QStringLiteral("invalid jump request: '%1' %2:%3 (1-based positions)")
                        .arg(filePath)
                        .arg(line)
                        .arg(column));
    }

#if !defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
    // No edit-core lane in this build means no provider that can place a cursor
    // in the product editor; report it instead of growing a second open/aspect
    // path for the demo widget.
    return fail(
        QStringLiteral("no edit-core provider: the jump needs the KTextEditor editor lane"));
#else
    vedit::app::EditorPane *pane =
        shell_->editorArea() != nullptr ? shell_->editorArea()->activePane() : nullptr;
    if (pane == nullptr) {
        return fail(QStringLiteral("the shell has no active editor pane"));
    }

    // ONE open seam (regression T69): EditorPane::openFile() dispatches to the
    // installed EditorProvider (KTextEditor in the product, the demo editor in a
    // provider-less build). Casting the pane widget to QPlainTextEdit here is
    // what made a result click silently do nothing in the real product after
    // the editor lane replaced the demo widget with KTextEditor::View.
    QString openError;
    if (!pane->openFile(filePath, &openError)) {
        return fail(openError.isEmpty() ? QCoreApplication::translate("VEdit", "cannot open %1").arg(filePath)
                                        : openError);
    }

    KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget());
    KTextEditor::Document *document = view != nullptr ? view->document() : nullptr;
    if (view == nullptr || document == nullptr) {
        return fail(QStringLiteral("the active editor pane is not a KTextEditor view "
                                   "(the edit-core provider is not installed)"));
    }

    const int lineCount = document->lines();
    if (lineCount <= 0) {
        return fail(QStringLiteral("the opened document has no lines"));
    }
    const int targetLine = qBound(1, line, lineCount);
    const int targetColumn = qBound(1, column, document->lineLength(targetLine - 1) + 1);
    const KTextEditor::Cursor target(targetLine - 1, targetColumn - 1);
    if (!view->setCursorPosition(target)) {
        return fail(QCoreApplication::translate("VEdit", "setCursorPosition(%1:%2) failed")
                        .arg(targetLine)
                        .arg(targetColumn));
    }
    // KTextEditor 6.24's public View interface has no centerCursor() (that is a
    // QPlainTextEdit/Kate-private API); setScrollPosition(cursor) is the public
    // "scroll so the cursor is visible" counterpart (since 5.33).
    view->setScrollPosition(target);
    const KTextEditor::Cursor actual = view->cursorPosition();
    if (actual.line() != target.line() || actual.column() != target.column()) {
        return fail(QStringLiteral("the cursor landed at %1:%2 instead of %3:%4")
                        .arg(actual.line() + 1)
                        .arg(actual.column() + 1)
                        .arg(targetLine)
                        .arg(targetColumn));
    }

    shell_->statusBar()->showMessage(
        QCoreApplication::translate("VEdit", "Opened %1 line %2").arg(filePath).arg(targetLine), 5000);
    lastJumpSucceeded_ = true;
    qInfo("search: jump opened '%s' line=%d column=%d (cursor=%d:%d)", qPrintable(filePath),
          targetLine, targetColumn, actual.line() + 1, actual.column() + 1);
    return true;
#endif
}

vedit::app::ApplicationShell *EditorJumpTarget::shell() const
{
    return shell_;
}

bool EditorJumpTarget::lastJumpSucceeded() const
{
    return lastJumpSucceeded_;
}

QString EditorJumpTarget::lastError() const
{
    return lastError_;
}

QString EditorJumpTarget::lastFilePath() const
{
    return lastFilePath_;
}

int EditorJumpTarget::lastLine() const
{
    return lastLine_;
}

int EditorJumpTarget::lastColumn() const
{
    return lastColumn_;
}

} // namespace vedit::workspace::search
