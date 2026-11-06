#pragma once

// The product's ONE jump target (T20/A-1, AC-7.7).
//
// Every panel that can send the user to a file position - search results,
// outline/diagnostics rows, the module tree, the project file list, the file
// tree and the breadcrumb bar - calls the application-wide seam
// `vedit::workspace::jump::jumpTo(file, line, column)` (src/workspace/jump).
// This class is the target behind that seam: installSearch()
// (search_integration.cpp) registers exactly one instance per shell through
// `jump::setTarget()`, and the registry weak-guards it (a destroyed QObject
// target drops out; the destructor also clears it explicitly).
//
// The jump itself opens the hit through the ONE shell-level open seam
// (EditorPane::openFile(), src/app/editor_area.h - the same seam the file tree's
// double click and the command palette use), then positions the cursor in the
// product's editing kernel (KTextEditor) on the hit line/column. Regression T69:
// it must NOT cast the pane widget to the demo QPlainTextEdit - the product
// installs KTextEditor, so that cast silently stopped every jump. There is no
// second file-open code path.
//
// When VEDIT_SEARCH_HAVE_EDITOR_CORE is not defined (a build without the
// edit-core lane) the target reports the missing provider through lastError()
// instead of opening the file itself or falling back to another widget type.
#include "jump/jump_target.h"

#include <QObject>
#include <QString>

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace::search {

// Opens the requested position in the shell's ACTIVE editor pane and puts the
// cursor on the 1-based line and column. Failures (missing file, no editor
// seam, no provider) are reported in lastError() and on the status bar, never
// thrown; jumpTo() returns true only when the cursor really moved.
class EditorJumpTarget final : public QObject, public vedit::workspace::jump::IJumpTarget {
    Q_OBJECT

public:
    // Becomes a child of `shell`, so one target serves every panel instance the
    // factory creates and outlives all of them.
    explicit EditorJumpTarget(vedit::app::ApplicationShell *shell);
    ~EditorJumpTarget() override;

    EditorJumpTarget(const EditorJumpTarget &) = delete;
    EditorJumpTarget &operator=(const EditorJumpTarget &) = delete;

    bool jumpTo(const QString &filePath, int line, int column) override;

    vedit::app::ApplicationShell *shell() const;
    bool lastJumpSucceeded() const;
    QString lastError() const;
    // The request the last jump attempt carried (evidence / tests).
    QString lastFilePath() const;
    int lastLine() const;
    int lastColumn() const;

private:
    vedit::app::ApplicationShell *shell_ = nullptr;
    QString lastFilePath_;
    int lastLine_ = 0;
    int lastColumn_ = 0;
    bool lastJumpSucceeded_ = false;
    QString lastError_;
};

} // namespace vedit::workspace::search
