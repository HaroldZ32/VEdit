#pragma once

// The product consumer of SearchPanel::jumpRequested (verdict ws-search H1).
//
// SearchPanel only emits jumpRequested() and forwards it to an installed
// IJumpTarget; until this file existed nobody installed one, so a click on a
// result had no effect in vedit-app. installSearch() (search_integration.cpp)
// now installs exactly one EditorJumpTarget per shell, and the panel factory
// hands it to every panel instance it creates.
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
#include "search_types.h"

#include <QObject>
#include <QString>

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace::search {

// Opens the hit in the shell's active editor pane and puts the cursor on the
// hit line (1-based) and column (1-based). Failures (missing file, no editor
// seam, no file tree lane) are reported in lastError() and on the status bar
// through openFileInEditor, and never throw.
class EditorJumpTarget final : public QObject, public IJumpTarget {
    Q_OBJECT

public:
    // Becomes a child of `shell`, so one target serves every panel instance the
    // factory creates and outlives all of them.
    explicit EditorJumpTarget(vedit::app::ApplicationShell *shell);
    ~EditorJumpTarget() override;

    EditorJumpTarget(const EditorJumpTarget &) = delete;
    EditorJumpTarget &operator=(const EditorJumpTarget &) = delete;

    void jumpTo(const JumpRequest &request) override;

    vedit::app::ApplicationShell *shell() const;
    JumpRequest lastRequest() const;
    bool lastJumpSucceeded() const;
    QString lastError() const;

private:
    vedit::app::ApplicationShell *shell_ = nullptr;
    JumpRequest lastRequest_;
    bool lastJumpSucceeded_ = false;
    QString lastError_;
};

} // namespace vedit::workspace::search
