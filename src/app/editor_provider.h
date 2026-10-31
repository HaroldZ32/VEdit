#pragma once

// The editor seam of the application shell.
//
// The shell owns window, menus, docks, splits and theme; it does NOT own an
// editor. src/editor (edit-core) implements this interface and hands the shell
// one widget per editor pane. Until that lane lands, DemoEditorProvider supplies
// a QPlainTextEdit-based placeholder so the layout is runnable and demonstrable;
// it is explicitly not the product editor.
//
// The shell never includes a KTextEditor header, so adding the real editor does
// not change this lane's build.
#include <QString>
#include <QWidget>

namespace vedit::app {

class EditorProvider {
public:
    virtual ~EditorProvider() = default;

    // Stable id, persisted in the session layout (e.g. "demo", "ktexteditor").
    virtual QString id() const = 0;

    // Human-readable description shown in pane headers and the About dialog.
    virtual QString displayName() const = 0;

    // Creates the editor widget for one pane. Ownership passes to the caller
    // (the shell reparents it into the pane). Must return a non-null widget.
    virtual QWidget *createEditor(QWidget *parent) = 0;
};

// Placeholder provider: one read/write QPlainTextEdit per pane, labelled
// "demo" everywhere so nobody mistakes it for the product editor.
class DemoEditorProvider final : public EditorProvider {
public:
    QString id() const override;
    QString displayName() const override;
    QWidget *createEditor(QWidget *parent) override;
};

} // namespace vedit::app
