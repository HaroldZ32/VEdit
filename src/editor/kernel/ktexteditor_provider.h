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

    bool openFile(QWidget *editor, const QString &absolutePath, QString *error) override;
    QString currentFilePath(QWidget *editor) const override;
    bool saveFile(QWidget *editor, QString *error) override;
    bool isModified(QWidget *editor) const override;

    // Breadcrumb seam (T94): the pane renders the semantic path of the cursor
    // for this editor widget; an empty string hides the bar.
    QString statusText(QWidget *editor) const override;
    void setStatusListener(QWidget *editor, std::function<void()> listener) override;

private:
    void notifyStatus(QWidget *editor);

    QHash<QWidget *, std::function<void()>> statusListeners_;
};

std::unique_ptr<vedit::app::EditorProvider> makeEditCoreProvider();

} // namespace vedit::editor
