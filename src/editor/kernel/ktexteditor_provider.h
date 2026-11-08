#pragma once

// KTextEditor-backed EditorProvider (the shell's editor seam) — Slice 1.
//
// Each editor pane gets one KTextEditor::Document plus its View. The provider
// also implements the document seam that the shell and the workspace lanes use
// to open/save the file shown in a pane; the demo QPlainTextEdit provider keeps
// its own default implementation of the same seam, so callers do not care which
// provider is installed.
#include "editor_provider.h"

#include <QHash>
#include <QMetaObject>
#include <QString>

#include <functional>
#include <memory>

namespace KTextEditor {
class Document;
class Editor;
class View;
}

namespace vedit::editor {

// The runtime widget of this provider is a KTextEditor::View (nullptr for any
// other provider's widget), so callers can detect the edit-core editor without
// including KTextEditor themselves.
KTextEditor::View *viewFor(QWidget *editorWidget);
KTextEditor::Document *documentFor(QWidget *editorWidget);
bool isEditCoreWidget(const QWidget *editorWidget);

class EditCoreProvider final : public vedit::app::EditorProvider
{
public:
    EditCoreProvider() = default;
    ~EditCoreProvider() override = default;

    QString id() const override;
    QString displayName() const override;
    QWidget *createEditor(QWidget *parent) override;

    // UI-Mosaic F2: the editor owns the syntax-theme rendering. The name comes
    // from vedit::app::ThemeManager; it is applied to every open view and to
    // every view created later (KTextEditor keeps the theme per view).
    void applySyntaxTheme(const QString &themeName) override;

    bool openFile(QWidget *editor, const QString &absolutePath, QString *error) override;
    QString currentFilePath(QWidget *editor) const override;
    bool saveFile(QWidget *editor, QString *error) override;
    bool isModified(QWidget *editor) const override;

    // Breadcrumb seam (T94): the pane renders the semantic path of the cursor
    // for this editor widget; an empty string hides the bar.
    QString statusText(QWidget *editor) const override;
    void setStatusListener(QWidget *editor, std::function<void()> listener) override;

    // Dirty seam (T12): the pane's tab marker, the window title and the status
    // bar subscribe here; the KTextEditor document's modifiedChanged drives it.
    void setModifiedListener(QWidget *editor, std::function<void(bool)> listener) override;

    // Session-restore seam (T19): caret + first displayed REAL line. The demo
    // fallback keeps the base class's QPlainTextEdit implementation.
    ViewState viewState(QWidget *editor) const override;
    void setViewState(QWidget *editor, const ViewState &state) override;

private:
    void notifyStatus(QWidget *editor);

    // UI-Mosaic F2: the last theme name KTextEditor accepted, so a view created
    // after the switch starts out with the right colors instead of the library
    // default. Empty until the shell hands one over (or when KTextEditor is
    // unavailable in this build).
    QString syntaxTheme_;

    QHash<QWidget *, std::function<void()>> statusListeners_;
    QHash<QWidget *, QMetaObject::Connection> modifiedConnections_;
};

std::unique_ptr<vedit::app::EditorProvider> makeEditCoreProvider();

} // namespace vedit::editor
