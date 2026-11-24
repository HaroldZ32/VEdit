#pragma once

// KTextEditor-backed EditorProvider (the shell's editor seam) — Slice 1.
//
// Each editor pane gets one KTextEditor::View backed by a KTextEditor::Document;
// split view creates a second View over an EXISTING document, so both panes
// share one buffer. The provider also implements the document seam that the
// shell and the workspace lanes use to open/save the file shown in a pane; the
// demo QPlainTextEdit provider keeps its own default implementation of the same
// seam, so callers do not care which provider is installed.
#include "editor_provider.h"
#include "status_relay.h"

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
    // batch-20261113 T3: intentionally NOT `= default`. The shell destroys the
    // provider BEFORE the editor area's views (src/app/app_shell.h declares
    // provider_ before editorArea_), while every view's FoldBreadcrumbBinding
    // still stores the forwarding lambda. Resetting the shared relay here turns
    // any later invocation of those lambdas into a no-op: they only hold a
    // weak_ptr to it, never the provider's own `this`.
    ~EditCoreProvider() override;

    QString id() const override;
    QString displayName() const override;
    QWidget *createEditor(QWidget *parent) override;

    // Batch 20261110d (split view): a second View over the SAME document.
    // Returns nullptr for a widget that is not one of this provider's views.
    // The document is released only when its LAST view is destroyed, so
    // closing one pane keeps the other pane's buffer (and its unsaved flag)
    // alive.
    QWidget *createEditorForExistingDocument(QWidget *sourceEditor, QWidget *parent) override;

    // UI-Mosaic F2: the editor owns the syntax-theme rendering. The name comes
    // from vedit::app::ThemeManager; it is applied to every open view and to
    // every view created later (KTextEditor keeps the theme per view).
    void applySyntaxTheme(const QString &themeName) override;

    bool openFile(QWidget *editor, const QString &absolutePath, QString *error) override;
    QString currentFilePath(QWidget *editor) const override;
    bool saveFile(QWidget *editor, QString *error) override;
    bool isModified(QWidget *editor) const override;
    QString comparisonText(QWidget *editor) const override;
    bool replaceComparisonText(QWidget *editor, qsizetype start, qsizetype length,
                               const QString &text) override;
    void setComparisonModified(QWidget *editor, bool modified) override;
    void setRevisionMarks(QWidget *editor, const QList<vedit::app::RevisionMark> &marks) override;

    // Breadcrumb seam (T94): the pane renders the semantic path of the cursor
    // for this editor widget; an empty string hides the bar.
    QString statusText(QWidget *editor) const override;
    void setStatusListener(QWidget *editor, std::function<void()> listener) override;

    // TEST SEAM (batch-20261113 T3): the number of live status subscriptions in
    // the shared relay. The lifetime probe asserts that BOTH an unsubscribe and
    // a view Qt destroys without one leave the registry empty; product code
    // never reads it. See .mpd/plans/batch-20261113-fim-detect.md §8 T3.
    int statusListenerCount() const;

    // Dirty seam (T12): the pane's tab marker, the window title and the status
    // bar subscribe here; the KTextEditor document's modifiedChanged drives it.
    void setModifiedListener(QWidget *editor, std::function<void(bool)> listener) override;

    // Session-restore seam (T19): caret + first displayed REAL line. The demo
    // fallback keeps the base class's QPlainTextEdit implementation.
    ViewState viewState(QWidget *editor) const override;
    void setViewState(QWidget *editor, const ViewState &state) override;

private:
    // Batch 20261110d: the per-view wiring every View gets, whether it was
    // created with a fresh document (createEditor) or shared from another
    // pane's document (createEditorForExistingDocument).
    void configureView(KTextEditor::View *view);

    // UI-Mosaic F2: the last theme name KTextEditor accepted, so a view created
    // after the switch starts out with the right colors instead of the library
    // default. Empty until the shell hands one over (or when KTextEditor is
    // unavailable in this build).
    QString syntaxTheme_;

    // batch-20261113 T3: the status subscriptions live in this shared,
    // non-QObject relay, NOT in a provider-owned QHash. The lambdas the provider
    // stores in a view's child object capture only a weak_ptr to the relay plus
    // the view pointer, so they never reach a provider that is already gone.
    std::shared_ptr<StatusRelay> statusRelay_ = std::make_shared<StatusRelay>();
    QHash<QWidget *, QMetaObject::Connection> modifiedConnections_;
};

std::unique_ptr<vedit::app::EditorProvider> makeEditCoreProvider();

} // namespace vedit::editor
