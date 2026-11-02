#pragma once

// Breadcrumb wiring of the live editor (originally T94; folding changed to
// KDE-native in T128).
//
// FOLDING (T128): VEdit no longer injects folds. KTextEditor computes folding
// regions from the KSyntaxHighlighting definition and the user folds/unfolds
// through the gutter. The T127 xcb experiment proved that path works in both
// directions with our build, and every VEdit-side command injection / registry /
// cursor-moving unfold was removed because it fought that native state and
// caused the T117/T121 crashes. A file therefore opens with NO auto-folds, which
// matches VS Code/Kate/CLion.
//
// BREADCRUMB: a Model fed by the syntax stack's structure query on cursor moves
// (debounced after text edits). With the runtime switch OFF the source reports
// "no semantics" and the path is EMPTY - no text-heuristic path is invented.
#include "breadcrumb_model.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QTimer;

namespace KTextEditor {
class Document;
class View;
}

namespace vedit::breadcrumb {
class SyntaxBreadcrumbSource;
}

namespace vedit::editor {

class FoldBreadcrumbBinding final : public QObject {
    Q_OBJECT
public:
    explicit FoldBreadcrumbBinding(KTextEditor::View *view, QObject *parent = nullptr);
    ~FoldBreadcrumbBinding() override;

    FoldBreadcrumbBinding(const FoldBreadcrumbBinding &) = delete;
    FoldBreadcrumbBinding &operator=(const FoldBreadcrumbBinding &) = delete;

    // The binding the provider installed on this editor widget, or nullptr.
    static FoldBreadcrumbBinding *instanceFor(QWidget *editorWidget);

    // The file was just loaded into this view: seed the breadcrumb.
    void onDocumentOpened();

    // Runtime tree-sitter switch: affects the breadcrumb's semantic source only.
    void setTreeSitterEnabled(bool enabled);
    bool treeSitterEnabled() const;

    // A caller reports a completed reparse: refresh the breadcrumb.
    void notifyReparsed();

    // The breadcrumb the pane bar renders ("m0 > inc") and the diagnostic form
    // the evidence prints ("module:m0 > function:inc"). Empty when no semantics.
    QString breadcrumbDisplay() const;
    QString breadcrumbDescribe() const;
    quint64 breadcrumbUpdates() const;
    quint64 breadcrumbChanges() const;
    bool semanticActive() const;

    // The pane registers a listener so a cursor move repaints the bar.
    void setStatusListener(std::function<void()> listener);

signals:
    void breadcrumbChanged(const QString &display);

private:
    void ensureSources();
    void refreshBreadcrumb();
    QString grammarId() const;

    KTextEditor::View *view_ = nullptr;
    std::unique_ptr<vedit::breadcrumb::SyntaxBreadcrumbSource> breadcrumbSource_;
    vedit::breadcrumb::Model breadcrumbModel_;
    QTimer *breadcrumbDebounce_ = nullptr;
    std::function<void()> statusListener_;
    QString textCache_;
    bool sourcesReady_ = false;
    bool opened_ = false;
};

} // namespace vedit::editor
